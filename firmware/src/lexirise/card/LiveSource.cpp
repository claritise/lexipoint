#include "LiveSource.h"

#include <Logging.h>
#if LEXIPOINT_DEV_HARNESS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include <Utf8.h>

#include <algorithm>
#include <iterator>
#include <numeric>
#include <utility>

#include "LiveWord.h"
#include "lexirise/api/AccessPolicy.h"
#include "lexirise/api/Requests.h"
#include "lexirise/api/Responses.h"
#include "lexirise/text/Utf8Prefix.h"
#include "lexirise/text/Utf8Units.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

namespace {

constexpr size_t kMirrorReserved = 16;  // mirror updates or written entries a card first makes room for

// What an analysis says of its words' entries, for the vocab mirror (V7a): each word's lemma entry (the save's target)
// and its surface entry, saved or not. An entry the answer doesn't list isn't saved, unless the answer was cut at
// config::kMaxEntries (then only the listed ones count).
std::vector<vocab::LiveState> liveStatesOf(const lookup::AnalyzedSentence& analyzed) {
  const api::AnalyzeResult& analysis = analyzed.analysis;
  const bool whole = analysis.state.size() < config::kMaxEntries;
  std::vector<vocab::LiveState> live;
  live.reserve(2 * analyzed.words.size());
  for (const size_t w : analyzed.words) {
    const api::Occurrence& occ = analysis.occurrences[w];
    for (const uint32_t id : {lookup::entryKeyOf(occ), occ.entryId}) {
      vocab::addLiveState(live, analyzed.language, id, analysis.stateFor(id), whole);
    }
  }
  return live;
}

// The tapped character: phase 0's text and highlight (popup-ui.md §2).
const text::SentenceChar* tappedChar(const text::BuiltSentence& sentence) {
  const auto it = std::find_if(sentence.chars.begin(), sentence.chars.end(),
                               [&sentence](const text::SentenceChar& c) { return c.start >= sentence.tapOffset; });
  return it != sentence.chars.end() ? &*it : nullptr;
}

}  // namespace

LiveSource::LiveSource(api::LexiriseApi& api, text::TapContext tap, ReaderPage page, std::vector<std::string> tags,
                       NextSentence next)
    : api_(api), page_(std::move(page)), next_(std::move(next)), tags_(std::move(tags)) {
  sentences_.push_back({std::move(tap), false});
}

std::optional<size_t> LiveSource::loadingSentence() const {
  for (size_t i = 0; i < sentences_.size(); i++) {
    if (!sentences_[i].analyzed) return i;
  }
  return std::nullopt;
}

const text::BuiltSentence& LiveSource::sentenceFor(const int index) const {
  if (index >= 0 && index < wordCount()) return *sentences_[sentenceOf_[static_cast<size_t>(index)]].tap.sentence;
  return *sentences_[loadingSentence().value_or(0)].tap.sentence;
}

CallFailure callFailure(const api::ApiError error) {
  if (error == api::ApiError::Unauthorized) return CallFailure::KeyRejected;
  if (error == api::ApiError::RateLimited) return CallFailure::RateLimited;
  return CallFailure::Network;
}

std::optional<text::TapContext> LiveSource::nextAskable(const text::TapContext& current) const {
  if (!next_) return std::nullopt;
  // Lexirise can only be asked with a sentence and its language (a switched-off language, a non-CJK line).
  // Each call moves on along the page, so this ends at its last sentence; the cap (one per token on the
  // page) only guards against a builder that stopped moving on.
  const size_t limit = std::accumulate(page_.lines.begin(), page_.lines.end(), size_t{1},
                                       [](const size_t n, const ReaderLine& line) { return n + line.tokens.size(); });
  text::TapContext next = next_(current);
  for (size_t i = 0; i < limit && next.sentence; i++) {
    if (next.language.language) return next;
    next = next_(next);
  }
  return std::nullopt;
}

bool LiveSource::extend(unsigned long) {
  if (pageEnded_ || loadingSentence()) return false;
  extendFailure_.reset();
  std::optional<text::TapContext> next = nextAskable(resumeAfter_ ? *resumeAfter_ : sentences_.back().tap);
  if (!next) {
    pageEnded_ = true;  // nothing more on the page to ask about
    return false;
  }
  sentences_.push_back({std::move(*next), false});
  return true;
}

void LiveSource::queue(const LevelChange& change) {
  const std::vector<int> same = sameWord(change.word);
  // Each save gets its one lookup retry: a Retry after a failure looks the word up again too.
  for (const int w : same) saveRetried_[w] = false;
  for (auto it = writes_.begin(); it != writes_.end(); ++it) {
    if (std::find(same.begin(), same.end(), it->word) == same.end()) continue;
    // Still waiting: one change from where Lexirise is to where the user left it.
    it->to = change.to;
    it->readyAtMs = change.readyAtMs;
    if (it->to == it->from) writes_.erase(it);  // back where it was: nothing to send
    return;
  }
  writes_.push_back(change);
}

bool LiveSource::needsLookupForSave(const int word) const {
  const lookup::LookupCard& card = cards_[word];
  // Its translation goes in the POST (D9): look it up, and once more if that failed.
  return !card.complete || (card.translationUnavailable && !saveRetried_[word]);
}

int LiveSource::lookupDue(const unsigned long nowMs, const bool closing) const {
  if (!closing && focused_ < wordCount() && !cards_[focused_].complete) return focused_;
  // A save needs its word's translation: look that word up first, even after the card moved on, but not
  // inside its Undo window (an Undo there costs nothing, and the toast mustn't wait behind a call).
  if (writeReady(nowMs, closing)) {
    const LevelChange& next = writes_.front();
    const bool saves = next.to != Level::None && !cards_[next.word].saved;
    if (saves && needsLookupForSave(next.word)) return next.word;
  }
  return -1;
}

bool LiveSource::writeReady(const unsigned long nowMs, const bool closing) const {
  return !writes_.empty() && (closing || timing::reached(nowMs, writes_.front().readyAtMs));
}

bool LiveSource::hasWork(const unsigned long nowMs) const {
  return loadingSentence().has_value() || lookupDue(nowMs, false) >= 0 || itemDue(nowMs, false) >= 0 ||
         writeReady(nowMs, false);
}

bool LiveSource::ours(const std::string& id) const {
  return std::find(createdIds_.begin(), createdIds_.end(), id) != createdIds_.end();
}

const LiveSource::Item* LiveSource::itemFor(const std::string& id) const {
  const auto it = std::find_if(items_.begin(), items_.end(), [&id](const Item& item) { return item.id == id; });
  return it != items_.end() ? &*it : nullptr;
}

bool LiveSource::fillFromItem(const int index) {
  if (!cards_[index].saved) return false;
  api::EntryState& saved = *cards_[index].saved;
  const Item* item = itemFor(saved.savedExpressionId);
  if (!item) return false;
  saved.notes = item->sentence;
  saved.userTags = item->tags;
  return true;
}

int LiveSource::itemDue(const unsigned long nowMs, const bool closing) const {
  // Only the word on screen, once its phase B has run (its frame first: the item never delays the lookup), and only
  // an item nobody asked for yet on this card; one saved here already holds what it sent. A sentence loading goes
  // first (a step past the end waits for nothing else; stepped back, the word's item comes after it), and after a
  // refusal (a 429 or a rejected key) none is asked until its retry time.
  if (itemRetryAtMs_) {
    if (!timing::reached(nowMs, *itemRetryAtMs_)) return -1;
    itemRetryAtMs_.reset();  // over: forget it, or 2^31 ms on (the signed compare) it'd read as not reached
  }
  if (closing || focused_ < 0 || focused_ >= wordCount() || loadingSentence()) return -1;
  const lookup::LookupCard& card = cards_[focused_];
  if (!card.complete || !card.saved || card.saved->savedExpressionId.empty()) return -1;
  // Phase B found Lexirise out of reach: another call would only wait out the same failure (no "Met before").
  if (card.translationUnavailable && noMeaningFor(card.translationError) == NoMeaning::Offline) return -1;
  const std::string& id = card.saved->savedExpressionId;
  return ours(id) || itemFor(id) ? -1 : focused_;
}

LiveSource::Fetched LiveSource::savedItem(const int index) const {
  Fetched f;
  f.kind = Fetched::Kind::Item;
  f.index = index;
  f.savedExpressionId = cards_[index].saved->savedExpressionId;
  const std::optional<net::Request> request = api::savedItemRequest(f.savedExpressionId);
  if (!request) {
    f.error = api::ApiError::Malformed;  // an id that can't go into a path: no item
    return f;
  }
  const api::ApiResponse got = api_.savedItem(*request);
  f.error = got.error;
  if (got.error == api::ApiError::RateLimited || got.error == api::ApiError::Unauthorized) {
    // Refused (AccessPolicy, often without the network): not the item's answer. Asked again once the block may be
    // over: a 429's wait, else (a rejected key, lifted only by a key check or a new key) the default, counted from
    // when the answer came (apply()), as AccessPolicy counts its back-off.
    f.refused = true;
    f.retryAfterS = got.retryAfterS;
  }
  if (got.ok() && api::parseSavedItem(got.body, f.item) != api::ParseStatus::Ok) {
    f.error = api::ApiError::Malformed;
    f.unreadable = lookup::bodyHead(got.body);
  }
  return f;
}

void LiveSource::setBookDeck(deck::BookDeck bookDeck, deck::DeckStore& store) {
  savesCarryBookTag_ = std::find(tags_.begin(), tags_.end(), bookDeck.tag()) != tags_.end();
  deckKeys_.clear();
  deckKeys_.reserve(std::size(kLanguages));
  std::transform(std::begin(kLanguages), std::end(kLanguages), std::back_inserter(deckKeys_),
                 [&bookDeck](const Language language) { return deck::deckKey(language, bookDeck.slug); });
  bookDeck_ = std::move(bookDeck);
  decks_ = &store;
  decks_->load();
}

void LiveSource::setBookTitles(BookTagStore& titles) { titles_ = titles.list(); }

void LiveSource::setIgnoredWords(IgnoredWordStore& store) {
  store.load();
  ignoredStore_ = &store;
}

std::optional<IgnoredKey> LiveSource::ignoreKey(const int index) const {
  if (index < 0 || index >= wordCount()) return std::nullopt;
  const lookup::LookupCard& card = cards_[index];
  return ignoredKeyFor(card.language, card.lemmaEntryId, card.headword());
}

bool LiveSource::ignored(const int index) const {
  const std::optional<IgnoredKey> key = ignoreKey(index);
  if (!key) return false;
  const auto here = std::find_if(ignoredHere_.rbegin(), ignoredHere_.rend(),
                                 [&key](const auto& change) { return change.first == *key; });
  if (here != ignoredHere_.rend()) return here->second;  // this card's latest change for it
  return ignoredStore_ && ignoredStore_->contains(*key);
}

bool LiveSource::setIgnored(const int index, const bool ignored) {
  std::optional<IgnoredKey> key = ignoreKey(index);
  if (!key || !ignoredStore_) return false;  // no list to keep it in, or no key
  ignoredHere_.erase(
      std::remove_if(ignoredHere_.begin(), ignoredHere_.end(), [&key](const auto& here) { return here.first == *key; }),
      ignoredHere_.end());
  ignoredHere_.emplace_back(std::move(*key), ignored);
  return true;
}

void LiveSource::rebuild(const int index, const FormName* named) {
  CardWord rebuilt = wordFor(index, named);
  if (index == focused_ && !(rebuilt == words_[index])) shownChanged_ = true;
  words_[index] = std::move(rebuilt);
}

bool LiveSource::takeShownChanged() { return std::exchange(shownChanged_, false); }

CardWord LiveSource::wordFor(const int index, const FormName* named) const {
  const auto i = static_cast<size_t>(index);
  FormName shown;
  if (!named && i < words_.size() && !words_[i].word.empty()) {
    const CardWord& w = words_[i];
    shown = {w.surface, w.word, w.conjugation, w.forms};
    named = &shown;
  }
  // "Met before" compares the sentence as far as the card has it: a long one the cap cut into consecutive pieces
  // (one cut on the right, the next on the left) is joined back, and each piece compared too. sentences_' neighbours
  // are next to each other on the page (extend() adds the one right after), so the pieces join without a gap. Joined
  // as they are: a space the builder would put between a Latin piece and the next isn't added (Japanese and Chinese,
  // the card's languages, have none).
  const auto [first, last] = cutChain(sentenceOf_[i]);
  const auto built = [this](const size_t s) -> const text::BuiltSentence& { return *sentences_[s].tap.sentence; };
  PageSentence page = pageSentence(sentenceOf_[i]);
  page.whole.clear();
  page.whole.reserve(last - first + 2);
  if (first != last) {
    std::string joined;
    for (size_t s = first; s <= last; s++) joined += built(s).text;
    page.whole.push_back({joined, built(first).truncatedLeft, built(last).truncatedRight});
  }
  for (size_t s = first; s <= last; s++)
    page.whole.push_back({built(s).text, built(s).truncatedLeft, built(s).truncatedRight});
  return cardWord(cards_[i], page, &titles_, named);
}

void LiveSource::renameLastWordBefore(const size_t sentence, Fetched& f) const {
  if (sentence == 0) return;
  const text::BuiltSentence& before = *sentences_[sentence - 1].tap.sentence;
  const text::BuiltSentence& cut = *sentences_[sentence].tap.sentence;
  // sentences_[sentence - 1] is the page neighbour only when this one continues it: a cut on the right followed by
  // one cut on the left (a punctuation-only piece replaced by the sentence after it is neither: laterSentenceFailed).
  if (!before.truncatedRight || !cut.truncatedLeft) return;
  // The word ending the cut before: its next character was unknown (the cap cut there), and is this cut's first.
  int last = -1;
  for (int w = 0; w < wordCount(); w++) {
    if (sentenceOf_[w] == sentence - 1 && (last < 0 || cards_[w].charEnd > cards_[last].charEnd)) last = w;
  }
  if (last < 0 || cards_[last].charEnd != text::utf16Length(before.text)) return;
  const std::string joined = before.text + cut.text;
  f.renamed = last;
  f.renamedName = formNameOf(cards_[last], PageSentence::single(joined, cut.truncatedRight));
}

std::pair<size_t, size_t> LiveSource::cutChain(const size_t sentence) const {
  const auto built = [this](const size_t s) -> const text::BuiltSentence& { return *sentences_[s].tap.sentence; };
  size_t first = sentence;
  while (first > 0 && built(first).truncatedLeft && built(first - 1).truncatedRight) first--;
  size_t last = sentence;
  while (last + 1 < sentences_.size() && built(last).truncatedRight && built(last + 1).truncatedLeft) last++;
  return {first, last};
}

PageSentence LiveSource::pageSentence(const size_t sentence) const {
  const text::BuiltSentence& built = *sentences_[sentence].tap.sentence;
  return PageSentence::single(built.text, built.truncatedRight, built.truncatedLeft);
}

void LiveSource::savedWithBookTag(const Language language) {
  if (!decks_ || !savesCarryBookTag_) return;
  for (size_t i = 0; i < std::size(kLanguages); i++) {
    if (kLanguages[i] == language) decks_->want(deckKeys_[i]);
  }
}

std::optional<LiveSource::DueDeck> LiveSource::deckDue() const {
  // The saves go first; a card whose saves don't carry the tag has no say in the deck.
  if (!writes_.empty() || !decks_ || !savesCarryBookTag_ || !decks_->anyWanted()) return std::nullopt;
  for (size_t i = 0; i < std::size(kLanguages); i++) {
    const deck::DeckStep step = decks_->next(deckKeys_[i]);
    if (step != deck::DeckStep::None) return DueDeck{i, step};
  }
  return std::nullopt;
}

deck::DeckCall LiveSource::fetchDeck() {
  const std::optional<DueDeck> due = deckDue();
  if (!due) return {};
  return deck::sendDeckStep(api_, *bookDeck_, kLanguages[due->language], due->step,
                            decks_->state(deckKeys_[due->language]).id);
}

void LiveSource::applyDeck(const deck::DeckCall& call) {
  if (decks_ && call.step != deck::DeckStep::None) decks_->apply(call);
}

void LiveSource::setVocabMirror(vocab::VocabStore& store) {
  vocab_ = &store;
  vocabLanguage_ = sentences_.front().tap.language.language;  // loaded later, on an idle card (flushMirror)
}

void LiveSource::toMirror(const vocab::LiveState& state, const bool ownWrite) {
  if (!vocab_) return;
  const std::pair<Language, uint32_t> entry{state.language, state.entryId};
  // A linear scan, bounded by the entries this card wrote (a handful: one per word the user saved or leveled).
  const bool written = std::find(writtenEntries_.begin(), writtenEntries_.end(), entry) != writtenEntries_.end();
  if (ownWrite && !written) {
    if (writtenEntries_.empty()) writtenEntries_.reserve(kMirrorReserved);
    writtenEntries_.push_back(entry);
  }
  if (!ownWrite && written) return;                                     // this card's write is newer than the analysis
  if (mirrorUpdates_.empty()) mirrorUpdates_.reserve(kMirrorReserved);  // a sentence's words, lemma and surface
  mirrorUpdates_.push_back(state);
  mirrorUpdates_.back().own = ownWrite;  // the reader's write: it outranks a page's snapshot even with no clock
}

void LiveSource::recordMirror() {
  if (!vocab_ || mirrorUpdates_.empty()) return;
  vocab_->record(mirrorUpdates_);
  mirrorUpdates_.clear();
}

bool LiveSource::mirrorFlushDue() const {
  // A write that failed isn't tried again on this card's idle windows (the close tries once more, then the next card).
  return vocab_ && ((vocabLanguage_ && !vocab_->loaded(*vocabLanguage_)) || (vocab_->dirty() && !mirrorWriteFailed_));
}

bool LiveSource::lookupFlushDue() const {
  // As the mirror's: a failed write waits for the close.
  return lookupCache_ && !lookupWrites_.empty() && !lookupWriteFailed_;
}

LiveSource::LookupsFlushed LiveSource::flushLookups(const bool closing) {
  LookupsFlushed out;
  if (!lookupCache_ || lookupWrites_.empty() || (lookupWriteFailed_ && !closing)) return out;
  out.answers = lookupWrites_.size();
  out.written = lookupCache_->write(lookupWrites_);
  if (out.written) {
    lookupWrites_.clear();
  } else {
    lookupWriteFailed_ = true;  // kept for the close's try
  }
  return out;
}

bool LiveSource::flushMirror(const bool load) {
  if (!vocab_) return false;
  recordMirror();
  const bool reads = load && vocabLanguage_ && !vocab_->loaded(*vocabLanguage_);
  if (reads) vocab_->load(*vocabLanguage_);
  // A write that failed on this card waits for the close (an idle step run for the lemma cache doesn't retry it).
  const bool writes = vocab_->dirty() && (!load || !mirrorWriteFailed_);
  lastFlushWriteFailed_ = writes && !vocab_->flush();
  if (lastFlushWriteFailed_) mirrorWriteFailed_ = true;
  return reads || writes;
}

bool LiveSource::hasVocabWork(const unsigned long nowMs, const uint32_t epochS) const {
  return vocab_ && vocabLanguage_ && writes_.empty() && wordCount() > 0 && error_ == api::ApiError::None &&
         vocabPages_ < config::kVocabPagesPerCard && vocab_->next(*vocabLanguage_, nowMs, epochS).has_value();
}

std::optional<vocab::PageCall> LiveSource::fetchVocab(const unsigned long nowMs, const uint32_t epochS,
                                                      const api::VocabPageReader::Cancel cancel) {
  if (!hasVocabWork(nowMs, epochS)) return std::nullopt;
  const std::optional<vocab::PagePlan> plan = vocab_->next(*vocabLanguage_, nowMs, epochS);
  if (!plan) return std::nullopt;
  if (cancel && cancel()) {  // a button already held: given up before the request, which then costs nothing
    vocab::PageCall held;
    held.plan = *plan;
    held.cancelled = true;
    return held;
  }
  return vocab::sendPage(api_, *plan, cancel);
}

bool LiveSource::hasProbeWork(const unsigned long nowMs) const {
  return vocab_ && vocabLanguage_ && writes_.empty() && wordCount() > 0 && error_ == api::ApiError::None &&
         vocab_->cardProbeDue(*vocabLanguage_, nowMs);
}

std::optional<vocab::PageCall> LiveSource::fetchProbe(const unsigned long nowMs,
                                                      const api::VocabPageReader::Cancel cancel) {
  if (!hasProbeWork(nowMs)) return std::nullopt;
  if (cancel && cancel()) return std::nullopt;  // a button already held: no probe now (none spent)
  const std::optional<vocab::PagePlan> plan = vocab_->takeCardProbe(*vocabLanguage_, nowMs);
  if (!plan) return std::nullopt;
  vocab::PageCall call = vocab::sendPage(api_, *plan, cancel);
  call.probe = true;
  return call;
}

std::vector<int> LiveSource::takeMirrorChanges(const std::vector<uint32_t>& changedEntries) {
  std::vector<int> changed;
  if (!vocab_ || changedEntries.empty()) return changed;
  const auto isChanged = [&changedEntries](const uint32_t id) {
    return id != 0 && std::find(changedEntries.begin(), changedEntries.end(), id) != changedEntries.end();
  };
  const auto wroteHere = [this](const Language language, const uint32_t id) {
    return std::find(writtenEntries_.begin(), writtenEntries_.end(), std::make_pair(language, id)) !=
           writtenEntries_.end();
  };
  for (int w = 0; w < wordCount(); w++) {
    lookup::LookupCard& card = cards_[w];
    if (!isChanged(card.lemmaEntryId) && !isChanged(card.entryId)) continue;
    if (wroteHere(card.language, card.lemmaEntryId) || wroteHere(card.language, card.entryId)) continue;
    // As the card reads a state: the lemma's entry if saved, else the surface's.
    std::optional<api::EntryState> saved;
    uint32_t savedEntry = 0;
    for (const uint32_t id : {card.lemmaEntryId, card.entryId}) {
      if (id == 0) continue;
      const std::optional<vocab::Entry> e = vocab_->find(card.language, id);
      if (std::optional<api::EntryState> state = e ? vocab::savedStateOf(*e) : std::nullopt) {
        saved = std::move(state);
        savedEntry = id;
        break;
      }
    }
    if (saved && card.saved) {  // what the card knows beyond the level (notes, tags from the item) stays
      api::EntryState kept = *card.saved;
      kept.savedExpressionId = saved->savedExpressionId;
      kept.proficiency = saved->proficiency;
      saved = std::move(kept);
    }
    const bool same = saved.has_value() == card.saved.has_value() &&
                      (!saved || (saved->savedExpressionId == card.saved->savedExpressionId &&
                                  saved->proficiency == card.saved->proficiency));
    if (same) continue;
    card.saved = std::move(saved);
    card.savedEntryId = card.saved ? savedEntry : 0;
    rebuild(w);
    changed.push_back(w);
  }
  return changed;
}

vocab::PageApplied LiveSource::applyVocab(const vocab::PageCall& call, const unsigned long nowMs,
                                          const uint32_t epochS) {
  if (!vocab_) return {};
  if (!call.cancelled && !call.probe) vocabPages_++;  // a page given up for input, or a probe, isn't the card's share
  return vocab_->apply(call, nowMs, epochS);
}

std::optional<LiveSource::FailedWrite> LiveSource::takeFailedWrite() {
  std::optional<FailedWrite> failed = std::move(failedWrite_);
  failedWrite_.reset();
  return failed;
}

api::ApiResponse LiveSource::send(const LevelChange& change, const lookup::LookupCard& card, std::string& newId,
                                  bool& clearFailed) const {
  const std::string id = card.saved ? card.saved->savedExpressionId : std::string();
  const auto sendItem = [this](const std::optional<net::Request>& request) {
    api::ApiResponse refused;
    refused.error = api::ApiError::Malformed;  // an id that can't go into a path
    return request ? api_.write(*request) : refused;
  };
  if (change.to == Level::None) {  // removed: DELETE only resets a dictionary word to unknown
    if (id.empty()) return {};     // never saved: nothing to remove
    api::ApiResponse removed = sendItem(api::removeRequest(id));
    // What this card saved, it clears (its notes and tags would stay); the user's own item keeps them.
    if (removed.ok() && ours(id)) clearFailed = !sendItem(api::clearRequest(id)).ok();  // removed either way
    return removed;
  }
  if (!id.empty()) return sendItem(api::setProficiencyRequest(id, proficiencyOf(change.to)));
  api::SaveWord save;
  save.language = card.language;
  const std::string headword = card.headword();
  save.text = headword;
  if (!card.senses.empty()) save.translation = card.senses.front().translation;
  save.notes = sentenceFor(change.word).text;
  save.proficiency = proficiencyOf(change.to);
  save.tags = tags_;
  api::ApiResponse saved = api_.write(api::saveRequest(save));
  api::SaveResult result;
  if (saved.ok() && api::parseSave(saved.body, result) != api::ParseStatus::Ok) saved.error = api::ApiError::Malformed;
  if (saved.ok()) newId = std::move(result.savedExpressionId);
  return saved;
}

LiveSource::Fetched LiveSource::fetch(const unsigned long nowMs, const bool closing) const {
  Fetched f;
  // In the order LiveSource.h's fetch() gives (nothing is shown before the tapped sentence's analysis).
  const std::optional<size_t> loading = loadingSentence();
  if (loading == 0u) {
    if (closing) return f;  // closing before the tapped sentence was analyzed: nothing shown, nothing queued
    return analysis(0);
  }
  if (const int due = lookupDue(nowMs, closing); due >= 0) {
    f.kind = Fetched::Kind::Entry;
    f.index = due;
    f.card = cards_[due];
    f.saveRetry = f.card.complete;  // it ran before and failed: this is the save's retry
    // V7c: the card's own answers not written yet first (the same lemma twice on one card), then one bucket read;
    // either needs no call.
    const std::string headword = f.card.headword();
    // The key the answer is for, taken before the call (settings.md: an answer keeps the key it was fetched under).
    const uint32_t account = lookupCache_ ? lookupCache_->account() : 0;
    const auto pending = std::find_if(lookupWrites_.rbegin(), lookupWrites_.rend(), [&](const lookup::CachedLookup& w) {
      return w.language == f.card.language && w.text == headword && w.account == account;
    });
    if (lookupCache_ && pending != lookupWrites_.rend()) {
      f.cacheRead.outcome = lookup::CacheRead::Outcome::Pending;
      lookup::applyLookup(f.card, pending->entry);
      return f;
    }
    if (lookupCache_) {
      lookup::CacheRead read = lookupCache_->read(f.card.language, headword);
      f.cacheRead = lookup::logOf(read);
      if (read.entry) {
        lookup::applyLookup(f.card, std::move(*read.entry));
        return f;
      }
    }
    std::optional<api::LookupResult> answer;
    f.error = lookup::completeCard(api_, f.card, &f.unreadable, lookupCache_ ? &answer : nullptr);  // a failure too
    if (answer && lookup::cacheable(headword, *answer)) {
      f.toCache = lookup::CachedLookup{f.card.language, headword, std::move(*answer), 0, account};
    }
  } else if (loading && !closing) {  // cppcheck-suppress knownConditionTrueFalse ; nullopt when nothing loads
    return analysis(*loading);
  } else if (const int item = itemDue(nowMs, closing); item >= 0) {
    return savedItem(item);
  } else if (writeReady(nowMs, closing)) {
    f.kind = Fetched::Kind::Write;
    f.index = writes_.front().word;
    const api::ApiResponse sent = send(writes_.front(), cards_[f.index], f.savedExpressionId, f.clearFailed);
    f.error = sent.error;
    f.retryAfterS = sent.retryAfterS;
  }
  return f;
}

LiveSource::Fetched LiveSource::analysis(const size_t sentence) const {
  Fetched f;
  f.kind = Fetched::Kind::Analysis;
  f.sentence = sentence;
  lookup::AnalyzedSentence analyzed;  // only for the cards: dropped when this returns, not carried into apply()
  // From the page's analysis when it has this sentence (V7b): no ①, and its states are the mirror's already.
  const std::optional<api::AnalyzeResult> known =
      pageSentences_ ? pageSentences_->analysisOf(sentences_[sentence].tap) : std::nullopt;
  f.report = lookup::analyzeTap(api_, sentences_[sentence].tap, analyzed, f.tapped, known ? &*known : nullptr);
  f.unreadable = f.report.bodyHead;
  if (f.report.outcome == lookup::LookupOutcome::Card) {
    // Each form's name now, outside RenderLock: apply() only takes it (C16's search, run for every word). The
    // card's phase A waits for all of them: naming the tapped word alone first would need another pass to name the
    // rest, and a word stepped onto before it ran would show unnamed; the dev log says how long it takes.
#if LEXIPOINT_DEV_HARNESS
    const unsigned long start = clock_ ? clock_() : 0;
#endif
    f.cards.reserve(analyzed.words.size());
    f.names.reserve(analyzed.words.size());
    const PageSentence page = pageSentence(sentence);
    for (size_t i = 0; i < analyzed.words.size(); i++) {
      f.cards.push_back(lookup::cardFor(analyzed, i));
      f.names.push_back(formNameOf(f.cards.back(), page));
    }
    // A live answer (①'s); the page's was taken when it came.
    if (vocab_ && !f.report.fromPage) f.live = liveStatesOf(analyzed);
#if LEXIPOINT_DEV_HARNESS
    if (clock_) {
      // And the stack this task never used so far (bytes): the search's frames are its deepest here.
      LOG_INF("LXCARD", "names %u words %lu ms, stack %u B free", static_cast<unsigned>(f.cards.size()),
              clock_() - start, static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
#endif
    renameLastWordBefore(sentence, f);
  }
  return f;
}

LiveSource::Advance LiveSource::apply(Fetched fetched, const unsigned long nowMs) {
  switch (fetched.kind) {
    case Fetched::Kind::None:
      return Advance::Idle;
    case Fetched::Kind::Analysis:
      error_ = fetched.report.error;
      if (fetched.report.outcome == lookup::LookupOutcome::Card) return addWords(fetched);
      if (fetched.sentence > 0) return laterSentenceFailed(fetched);
      return fetched.report.outcome == lookup::LookupOutcome::NotFound ? Advance::NotFound : Advance::Unavailable;
    case Fetched::Kind::Entry:
      error_ = fetched.error;
      if (fetched.saveRetry) saveRetried_[fetched.index] = true;
      if (fetched.toCache) {  // for the lemma cache's next write (flushLookups): the newest kLookupPendingMax
        if (lookupWrites_.size() == config::kLookupPendingMax) lookupWrites_.erase(lookupWrites_.begin());
        lookupWrites_.push_back(std::move(*fetched.toCache));
      }
      cards_[fetched.index] = std::move(fetched.card);
      // Its phase is the controller's to compare (Advance::Changed). Phase B keeps the form and the dictionary form
      // (lookup::completeCard doesn't touch them), so the name shown is reused, not worked out again here under
      // RenderLock with this cut alone (which would lose a rename from the next cut).
      rebuild(fetched.index);
      return Advance::Changed;
    case Fetched::Kind::Item: {
      if (fetched.refused) {  // not kept, asked again once the block can be over (from now: the answer's time)
        itemRetryAtMs_ = api::retryAtMs(fetched.retryAfterS, nowMs);
        return Advance::Idle;
      }
      itemRetryAtMs_.reset();
      // Kept whatever came, so it's asked once: a failure leaves "First time you've met this word." (no error shown).
      Item item{std::move(fetched.savedExpressionId), {}, {}};
      if (fetched.error == api::ApiError::None) {
        // Lexipoint's save writes the sentence as notes; a word saved in the Lexirise app carries it in sentence_text.
        const bool noted = !text::trimmedSpaces(fetched.item.notes).empty();
        item.sentence = std::move(noted ? fetched.item.notes : fetched.item.sentenceText);
        item.tags = std::move(fetched.item.userTags);
      }
      if (items_.empty()) items_.reserve(config::kSavedItemsReserved);
      items_.push_back(std::move(item));
      for (int w = 0; w < wordCount(); w++) {  // every copy of the saved word on the card
        if (fillFromItem(w)) rebuild(w);
      }
      return Advance::Idle;  // phase and level unchanged; the word on screen redrawn: takeShownChanged()
    }
    case Fetched::Kind::Write: {
      error_ = fetched.error;
      const LevelChange change = writes_.front();
      writes_.pop_front();
      const lookup::LookupCard& card = cards_[change.word];
      const std::vector<int> same = sameWord(change.word);
      if (fetched.error != api::ApiError::None) {
        // Later changes to this entry (any of its occurrences) were built on this one: drop them, and
        // put the word back where Lexirise has it.
        writes_.erase(std::remove_if(writes_.begin(), writes_.end(),
                                     [&](const LevelChange& c) {
                                       return std::find(same.begin(), same.end(), c.word) != same.end();
                                     }),
                      writes_.end());
        LevelChange back;
        back.word = change.word;
        back.from = change.to;
        back.to = change.from;
        failedWrite_ = FailedWrite{back, fetched.error, fetched.retryAfterS};
        return Advance::Idle;
      }
      std::optional<api::EntryState> saved = card.saved;
      // The entry Lexirise keeps it under: the one whose state the card had (the surface's when only it was saved),
      // or for a new save the lemma's (the save sends the lemma).
      uint32_t savedEntry = card.saved && card.savedEntryId != 0 ? card.savedEntryId : card.lemmaEntryId;
      if (!fetched.savedExpressionId.empty()) {
        savedEntry = card.lemmaEntryId;
        saved.emplace();
        saved->savedExpressionId = fetched.savedExpressionId;
        // What the save sent, as Lexirise now holds it: a later sentence's "Met before" (C14).
        saved->notes = std::string(text::utf8Prefix(sentenceFor(change.word).text, config::kMaxSavedNoteBytes));
        saved->userTags = tags_;
        createdIds_.push_back(fetched.savedExpressionId);
        savedWithBookTag(card.language);  // a new save (POST) carries the tags
      }
      const bool savedHere = saved && ours(saved->savedExpressionId);
      if (change.to == Level::None && savedHere) {
        saved.reset();  // removed and cleared: saving it again is a new save (the full D9 POST)
      } else if (change.to == Level::None && saved) {
        saved->proficiency = 0;  // the user's own item stays, notes and all: a later level is a PATCH
      } else if (saved) {
        saved->proficiency = proficiencyOf(change.to);
      }
      const Language language = card.language;
      // Lexirise keeps a removed dictionary word's item, at level 0 (measured), ours too though the card forgets it.
      std::optional<api::EntryState> kept = saved;
      if (change.to == Level::None && savedHere && card.saved) {
        kept = card.saved;
        kept->proficiency = 0;
      }
      for (const int w : same) {  // one entry in Lexirise
        cards_[w].saved = saved;
        cards_[w].savedEntryId = saved ? savedEntry : 0;
        rebuild(w);  // its "Met before" follows (a copy analysed before the save landed too)
      }
      if (const auto state = vocab::liveStateOf(language, savedEntry, kept)) toMirror(*state, true);
      return Advance::Idle;  // the card already shows the level; a copy on screen that changed: takeShownChanged()
    }
  }
  return Advance::Idle;
}

LiveSource::Advance LiveSource::addWords(Fetched& fetched) {
  const int first = wordCount();
  for (lookup::LookupCard& card : fetched.cards) {
    cards_.push_back(std::move(card));
    words_.emplace_back();  // filled below, once its saved state is settled
    sentenceOf_.push_back(fetched.sentence);
    saveRetried_.push_back(false);
  }
  sentences_[fetched.sentence].analyzed = true;
  resumeAfter_.reset();
  for (const vocab::LiveState& state : fetched.live) toMirror(state, false);
  // An entry already on the card knows best whether it's saved (a save made here may be newer than this
  // analysis): its later occurrences share that, so the next write for any of them is the right one, and its "Met
  // before" (the sentence and tags a save made here sent, or its item once fetched).
  for (int w = first; w < wordCount(); w++) {
    const std::vector<int> same = sameWord(w);
    const auto earlier = std::find_if(same.begin(), same.end(), [first](const int e) { return e < first; });
    if (earlier != same.end()) {
      cards_[w].saved = cards_[*earlier].saved;
      cards_[w].savedEntryId = cards_[*earlier].savedEntryId;
    }
    fillFromItem(w);  // a saved item already fetched on this card (a copy under another entry)
    const auto k = static_cast<size_t>(w - first);
    words_[w] = wordFor(w, k < fetched.names.size() ? &fetched.names[k] : nullptr);  // "Met before" as saved now
  }
  // A cut continuing the one before it (a long sentence the cap cut): the word ending the cut before gets the name
  // worked out with its next character (analysis()), and the earlier cuts' words compare their "Met before" against
  // the sentence joined back now (their names kept).
  if (fetched.renamed >= 0 && fetched.renamed < first) rebuild(fetched.renamed, &fetched.renamedName);
  const size_t chainFirst = cutChain(fetched.sentence).first;
  for (int w = 0; w < first; w++) {
    if (w != fetched.renamed && sentenceOf_[w] >= chainFirst && sentenceOf_[w] < fetched.sentence) rebuild(w);
  }
  // The tapped sentence opens on the tapped word; a later one on its first (where the card steps to).
  if (fetched.sentence == 0) start_ = focused_ = first + static_cast<int>(fetched.tapped);
  return Advance::Changed;
}

LiveSource::Advance LiveSource::laterSentenceFailed(const Fetched& fetched) {
  const bool noWord = fetched.report.outcome == lookup::LookupOutcome::NotFound;
  if (noWord) {  // only punctuation (……, a lone 」): go on to the sentence after
    resumeAfter_ = sentences_[fetched.sentence].tap;
    if (std::optional<text::TapContext> after = nextAskable(*resumeAfter_)) {
      sentences_[fetched.sentence] = {std::move(*after), false};
      return Advance::Idle;
    }
  }
  sentences_.erase(sentences_.begin() + static_cast<long>(fetched.sentence), sentences_.end());
  // No words left on the page: stop there, quietly. No answer: say why; the next step tries again.
  pageEnded_ = noWord;
  extendFailure_ = noWord ? std::nullopt : std::optional<CallFailure>(callFailure(fetched.report.error));
  return Advance::Changed;
}

std::vector<int> LiveSource::sameWord(const int index) const {
  std::vector<int> same;
  const uint32_t entry = cards_[index].lemmaEntryId;
  // One Lexirise entry: the same lemma in the same language (a card can hold sentences of both languages
  // in a book that doesn't say, P9, and entry ids needn't be unique across them).
  const Language language = cards_[index].language;
  for (int i = 0; i < wordCount(); i++) {
    if (i == index || (entry != 0 && cards_[i].lemmaEntryId == entry && cards_[i].language == language)) {
      same.push_back(i);
    }
  }
  return same;
}

Level LiveSource::savedLevel(const int index) const { return levelOf(cards_[index].saved); }

Phase LiveSource::phase(const int index) const { return index < wordCount() ? phaseOf(cards_[index]) : Phase::Pending; }

std::string LiveSource::pendingText() const {
  // Phase 0 is only ever the tapped sentence's (a later one loads while the card stays on its word).
  const text::SentenceChar* c = tappedChar(*sentences_.front().tap.sentence);
  if (!c || c->token.line >= page_.lines.size() || c->token.token >= page_.lines[c->token.line].tokens.size()) {
    return {};
  }
  // The character as it stands on the page (the sentence joined it unchanged).
  const std::string& token = page_.lines[c->token.line].tokens[c->token.token].text;
  return text::utf8Codepoints(token, c->codepoint, c->codepoint + 1);
}

PageScene LiveSource::scene(const int index, const bool highlight, const TextMetrics& metrics,
                            const int highlightCodepoints) const {
  const text::BuiltSentence& sentence = sentenceFor(index);
  if (index >= wordCount() || highlightCodepoints > 0) {
    const text::SentenceChar* c = tappedChar(sentence);
    const uint32_t start = c ? c->start : sentence.tapOffset;
    return readerScene(page_, sentence, start, start + (c ? c->units : 1), highlight, metrics);
  }
  return readerScene(page_, sentence, cards_[index].charStart, cards_[index].charEnd, highlight, metrics);
}

}  // namespace lexipoint::card
