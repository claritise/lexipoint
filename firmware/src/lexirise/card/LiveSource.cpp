#if LEXIRISE

#include "LiveSource.h"

#include <Utf8.h>

#include <algorithm>

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

LiveSource::LiveSource(api::LexiriseApi& api, text::TapContext tap, ReaderPage page, std::vector<std::string> tags)
    : api_(api), tap_(std::move(tap)), page_(std::move(page)), tags_(std::move(tags)) {}

void LiveSource::queue(const LevelChange& change) {
  const std::vector<int> same = sameWord(change.word);
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
  return !analyzed_ || lookupDue(nowMs, false) >= 0 || writeReady(nowMs, false);
}

std::optional<LevelChange> LiveSource::takeFailedWrite() {
  std::optional<LevelChange> failed = std::move(failedWrite_);
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
  word.notes = tap_.sentence->text;
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
  if (!analyzed_) {
    if (closing) return f;  // nothing shown, nothing queued
    f.kind = Fetched::Kind::Analysis;
    f.report = lookup::analyzeTap(api_, tap_, f.analysis, f.tapped);
  } else if (const int due = lookupDue(nowMs, closing); due >= 0) {
    f.kind = Fetched::Kind::Entry;
    f.index = due;
    f.card = cards_[due];
    f.saveRetry = f.card.complete;                 // it ran before and failed: this is the save's retry
    f.error = lookup::completeCard(api_, f.card);  // a failure still completes it: the word without its meaning
  } else if (writeReady(nowMs, closing)) {
    f.kind = Fetched::Kind::Write;
    f.index = writes_.front().word;
    f.error = send(writes_.front(), cards_[f.index], f.savedExpressionId, f.clearFailed).error;
  }
  return f;
}

LiveSource::Advance LiveSource::apply(Fetched fetched) {
  switch (fetched.kind) {
    case Fetched::Kind::None:
      return Advance::Idle;
    case Fetched::Kind::Analysis:
      error_ = fetched.report.error;
      if (fetched.report.outcome == lookup::LookupOutcome::NotFound) return Advance::NotFound;
      if (fetched.report.outcome != lookup::LookupOutcome::Card) return Advance::Unavailable;
      analysis_ = std::move(fetched.analysis);
      for (size_t i = 0; i < analysis_.words.size(); i++) {
        cards_.push_back(lookup::cardFor(analysis_, i));
        words_.push_back(cardWord(cards_.back()));
      }
      saveRetried_.assign(cards_.size(), false);
      start_ = focused_ = static_cast<int>(fetched.tapped);
      analyzed_ = true;
      return Advance::Changed;
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
      lookup::LookupCard& card = cards_[change.word];
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
        failedWrite_ = back;
        return Advance::Idle;
      }
      std::optional<api::EntryState> saved = card.saved;
      if (!fetched.savedExpressionId.empty()) {
        saved = api::EntryState{fetched.savedExpressionId, 0, 0};
        createdIds_.push_back(fetched.savedExpressionId);
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

std::vector<int> LiveSource::sameWord(const int index) const {
  std::vector<int> same;
  const uint32_t entry = cards_[index].lemmaEntryId;
  for (int i = 0; i < wordCount(); i++) {
    if (i == index || (entry != 0 && cards_[i].lemmaEntryId == entry)) same.push_back(i);
  }
  return same;
}

Level LiveSource::savedLevel(const int index) const { return levelOf(cards_[index].saved); }

Phase LiveSource::phase(const int index) const { return index < wordCount() ? phaseOf(cards_[index]) : Phase::Pending; }

std::string LiveSource::pendingText() const {
  const text::SentenceChar* c = tappedChar(*tap_.sentence);
  if (!c || c->token.line >= page_.lines.size() || c->token.token >= page_.lines[c->token.line].tokens.size()) {
    return {};
  }
  // The character as it stands on the page (the sentence joined it unchanged).
  const std::string& token = page_.lines[c->token.line].tokens[c->token.token].text;
  return text::utf8Codepoints(token, c->codepoint, c->codepoint + 1);
}

PageScene LiveSource::scene(const int index, const bool highlight, const TextMetrics& metrics,
                            const int highlightCodepoints) const {
  const text::BuiltSentence& sentence = *tap_.sentence;
  if (index >= wordCount() || highlightCodepoints > 0) {
    const text::SentenceChar* c = tappedChar(sentence);
    const uint32_t start = c ? c->start : sentence.tapOffset;
    return readerScene(page_, sentence, start, start + (c ? c->units : 1), highlight, metrics);
  }
  return readerScene(page_, sentence, cards_[index].charStart, cards_[index].charEnd, highlight, metrics);
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
