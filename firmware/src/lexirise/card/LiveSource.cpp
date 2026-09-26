#if LEXIRISE

#include "LiveSource.h"

#include <Utf8.h>

#include <algorithm>
#include <iterator>

#include "LiveWord.h"
#include "lexirise/api/Requests.h"
#include "lexirise/text/Utf8Units.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

namespace {

// The tapped character: phase 0's text and highlight (popup-ui.md §2).
const text::SentenceChar* tappedChar(const text::BuiltSentence& sentence) {
  for (const text::SentenceChar& c : sentence.chars) {
    if (c.start >= sentence.tapOffset) return &c;
  }
  return nullptr;
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
  size_t limit = 1;
  for (const ReaderLine& line : page_.lines) limit += line.tokens.size();
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
  return loadingSentence().has_value() || lookupDue(nowMs, false) >= 0 || writeReady(nowMs, false);
}

void LiveSource::setBookDeck(deck::BookDeck bookDeck, deck::DeckStore& store) {
  savesCarryBookTag_ = std::find(tags_.begin(), tags_.end(), bookDeck.tag()) != tags_.end();
  deckKeys_.clear();
  deckKeys_.reserve(std::size(kLanguages));
  for (const Language language : kLanguages) deckKeys_.push_back(deck::deckKey(language, bookDeck.slug));
  bookDeck_ = std::move(bookDeck);
  decks_ = &store;
  decks_->load();
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
    const bool ours = std::find(createdIds_.begin(), createdIds_.end(), id) != createdIds_.end();
    if (removed.ok() && ours) clearFailed = !sendItem(api::clearRequest(id)).ok();  // removed either way
    return removed;
  }
  if (!id.empty()) return sendItem(api::setProficiencyRequest(id, proficiencyOf(change.to)));
  api::SaveWord word;
  word.language = card.language;
  const std::string headword = card.headword();
  word.text = headword;
  if (!card.senses.empty()) word.translation = card.senses.front().translation;
  word.notes = sentenceFor(change.word).text;
  word.proficiency = proficiencyOf(change.to);
  word.tags = tags_;
  api::ApiResponse saved = api_.write(api::saveRequest(word));
  api::SaveResult result;
  if (saved.ok() && api::parseSave(saved.body, result) != api::ParseStatus::Ok) saved.error = api::ApiError::Malformed;
  if (saved.ok()) newId = std::move(result.savedExpressionId);
  return saved;
}

LiveSource::Fetched LiveSource::fetch(const unsigned long nowMs, const bool closing) const {
  Fetched f;
  // In order: the tapped sentence's analysis (nothing is shown before it); the word on screen's lookup (and a
  // save's); a next sentence the card waits for (a step past the end); the next write that's ready. Closing:
  // only what the queued writes need.
  const std::optional<size_t> loading = loadingSentence();
  if (loading == 0u) {
    if (closing) return f;  // closing before the tapped sentence was analyzed: nothing shown, nothing queued
    return analysis(0);
  }
  if (const int due = lookupDue(nowMs, closing); due >= 0) {
    f.kind = Fetched::Kind::Entry;
    f.index = due;
    f.card = cards_[due];
    f.saveRetry = f.card.complete;                                // it ran before and failed: this is the save's retry
    f.error = lookup::completeCard(api_, f.card, &f.unreadable);  // a failure still completes it
  } else if (loading && !closing) {  // cppcheck-suppress knownConditionTrueFalse ; nullopt when nothing loads
    return analysis(*loading);
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
  f.report = lookup::analyzeTap(api_, sentences_[sentence].tap, f.analysis, f.tapped);
  f.unreadable = f.report.bodyHead;
  return f;
}

LiveSource::Advance LiveSource::apply(Fetched fetched) {
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
      cards_[fetched.index] = std::move(fetched.card);
      words_[fetched.index] = cardWord(cards_[fetched.index]);
      return Advance::Changed;
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
      if (!fetched.savedExpressionId.empty()) {
        saved = api::EntryState{fetched.savedExpressionId, 0, 0};
        createdIds_.push_back(fetched.savedExpressionId);
        savedWithBookTag(card.language);  // a new save (POST) carries the tags
      }
      const bool ours =
          saved && std::find(createdIds_.begin(), createdIds_.end(), saved->savedExpressionId) != createdIds_.end();
      if (change.to == Level::None && ours) {
        saved.reset();  // removed and cleared: saving it again is a new save (the full D9 POST)
      } else if (change.to == Level::None && saved) {
        saved->proficiency = 0;  // the user's own item stays, notes and all: a later level is a PATCH
      } else if (saved) {
        saved->proficiency = proficiencyOf(change.to);
      }
      for (const int w : same) cards_[w].saved = saved;  // one entry in Lexirise
      return Advance::Idle;                              // the card already shows it
    }
  }
  return Advance::Idle;
}

LiveSource::Advance LiveSource::addWords(Fetched& fetched) {
  const int first = wordCount();
  for (size_t i = 0; i < fetched.analysis.words.size(); i++) {
    cards_.push_back(lookup::cardFor(fetched.analysis, i));
    words_.push_back(cardWord(cards_.back()));
    sentenceOf_.push_back(fetched.sentence);
    saveRetried_.push_back(false);
  }
  sentences_[fetched.sentence].analyzed = true;
  resumeAfter_.reset();
  // An entry already on the card knows best whether it's saved (a save made here may be newer than this
  // analysis): its later occurrences share that, so the next write for any of them is the right one.
  for (int w = first; w < wordCount(); w++) {
    for (const int e : sameWord(w)) {
      if (e < first) {
        cards_[w].saved = cards_[e].saved;
        break;
      }
    }
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

#endif  // LEXIRISE
