#if LEXIRISE

#include "CardController.h"

#include <iterator>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

CardController::CardController(CardSource& source, const ReadingMode reading, CardStrings strings)
    : source_(source), strings_(strings) {
  state_.reading = reading;
}

const CardWord& CardController::currentWord() const {
  static const CardWord kNone;  // phase 0: nothing analyzed yet
  return hasWord() ? source_.word(word_) : kNone;
}

void CardController::open(const unsigned long nowMs) {
  source_.open(nowMs);
  word_ = 0;
  steps_ = 0;
  levels_.clear();
  state_.view = View::Card;
  state_.tab = 0;
  clearToast();
  syncWord();
}

bool CardController::syncWord() {
  // The sentence just arrived (a lookup answers after phase 0): the card is on the tapped word.
  if (levels_.empty() && source_.wordCount() > 0) word_ = source_.startWord();
  while (static_cast<int>(levels_.size()) < source_.wordCount()) {
    levels_.push_back(source_.savedLevel(static_cast<int>(levels_.size())));
  }
  const CardState before = state_;
  state_.phase = hasWord() ? source_.phase(word_) : Phase::Pending;
  state_.level = hasWord() ? levels_[word_] : Level::None;
  state_.pendingText = source_.pendingText();
  state_.pageNumber = source_.pageNumber();
  return state_.phase != before.phase || state_.level != before.level || state_.pendingText != before.pendingText ||
         state_.pageNumber != before.pageNumber;
}

bool CardController::tick(const unsigned long nowMs) {
  bool changed = source_.tick(nowMs) && syncWord();
  if (!state_.toast.empty() && timing::reached(nowMs, toastUntilMs_)) {
    clearToast();
    changed = true;
  }
  return changed;
}

bool CardController::sourceChanged() { return syncWord(); }

std::optional<unsigned long> CardController::nextDueMs() const {
  std::optional<unsigned long> due = source_.nextDueMs();
  if (!state_.toast.empty() && (!due || timing::before(toastUntilMs_, *due))) due = toastUntilMs_;
  return due;
}

bool CardController::step(const int direction, const unsigned long nowMs) {
  const int next = word_ + (direction > 0 ? 1 : -1);
  if (!hasWord() || next < 0 || next >= source_.wordCount()) return false;  // v0.1: stop at the ends
  word_ = next;
  steps_++;
  // A toast (and its Undo) belongs to the word it was about; a failure is about a save the user believes
  // made, and it came late: it stays for its time, Retry and all.
  if (!failureToast_) clearToast();
  source_.focus(word_, nowMs);
  syncWord();
  return true;
}

void CardController::showToast(std::string text, const unsigned long nowMs, const bool tappable,
                               const unsigned long durationMs) {
  state_.toast = std::move(text);
  state_.toastUndo = tappable;
  toastUntilMs_ = nowMs + durationMs;
  if (!tappable) undoWord_ = -1;
  retries_.clear();  // a new toast replaces a Retry (levelFailed keeps the list it adds to)
  failureToast_ = false;
}

void CardController::clearToast() {
  state_.toast.clear();
  state_.toastUndo = false;
  undoWord_ = -1;
  retries_.clear();
  failureToast_ = false;
}

Outcome CardController::tap(const Hit* hit, const unsigned long nowMs) {
  if (!hit) {  // the page outside the card: close (the expanded view covers the page)
    return {state_.view == View::Card ? Effect::Close : Effect::None, false};
  }
  switch (hit->target) {
    case Target::Level: {
      if (!hasWord()) return {};
      const auto level = static_cast<Level>(hit->index);
      const Level before = levels_[word_];
      if (level == before) return {};  // already there: a double tap sends nothing twice
      Outcome o = setLevel(level, before == Level::None ? strings_.savedAs : strings_.now, true, nowMs);
      undoWord_ = word_;
      undoLevel_ = before;
      return o;
    }
    case Target::ToastUndo: {  // a new save is removed; a level change goes back (popup-ui.md §3.2)
      if (!retries_.empty()) return retry(nowMs);
      if (undoWord_ != word_) return {};
      const Level restored = undoLevel_;
      if (restored == Level::None) {
        Outcome o = setLevel(restored, "", false, nowMs);
        showToast(strings_.removed, nowMs);
        return o;
      }
      return setLevel(restored, strings_.now, false, nowMs);
    }
    case Target::RankRow:
      state_.view = state_.view == View::Card ? View::Expanded : View::Card;
      return {Effect::Redraw, false};
    case Target::Close:
      return {Effect::Close, false};
    case Target::ReadingLine:
      state_.reading = state_.reading == ReadingMode::Kana ? ReadingMode::Romaji : ReadingMode::Kana;
      showToast(
          std::string(strings_.readings) + (state_.reading == ReadingMode::Kana ? strings_.kana : strings_.romaji),
          nowMs);
      return {Effect::Redraw, true};
    case Target::Tab:
      if (state_.tab == hit->index) return {};
      state_.tab = hit->index;
      return {Effect::Redraw, false};
    case Target::Action:
      if (!hasWord()) return {};
      if (hit->index == 0) {  // Undo save
        if (levels_[word_] == Level::None) return {};
        Outcome o = setLevel(Level::None, "", false, nowMs);
        showToast(strings_.removed, nowMs);
        return o;
      }
      if (hit->index >= 1 && hit->index <= static_cast<int>(std::size(strings_.actionDone))) {
        showToast(source_.demoActions() ? strings_.actionDone[hit->index - 1] : strings_.notYet, nowMs);
      }
      return {Effect::Redraw, false};
    case Target::Card:
      return {};
  }
  return {};
}

// The word at `level` now, with "<prefix><level name>[ · Undo]" (the caller replaces it for a removal).
Outcome CardController::setLevel(const Level level, const char* toastPrefix, const bool undo,
                                 const unsigned long nowMs) {
  const LevelChange change{word_, levels_[word_], level, undo ? nowMs + config::kToastMs : nowMs};
  for (const int same : source_.sameWord(word_)) levels_[same] = level;  // one entry in Lexirise
  state_.level = level;
  if (level != Level::None) {
    showToast(
        std::string(toastPrefix) + strings_.levelNames[static_cast<int>(level)] + (undo ? strings_.undoSuffix : ""),
        nowMs, undo);
  }
  Outcome o{Effect::Redraw, false, {}};
  o.changes.push_back(change);
  return o;
}

void CardController::levelFailed(const int word, const Level level, const unsigned long nowMs, const WriteFailure why,
                                 const Level wanted, const uint32_t retryInS) {
  if (word < 0 || word >= static_cast<int>(levels_.size())) return;
  for (const int same : source_.sameWord(word)) levels_[same] = level;
  if (hasWord()) state_.level = levels_[word_];
  // Said even when the card moved on: the user thinks it's saved (offline-and-errors.md §3).
  if (why == WriteFailure::KeyRejected) {
    showToast(strings_.keyRejected, nowMs, false, config::kFailureToastMs);
    failureToast_ = true;
    return;
  }
  std::string text = why == WriteFailure::RateLimited
                         ? std::string(strings_.rateLimitedTryIn) + std::to_string(retryInS) + strings_.seconds
                         : std::string(strings_.saveFailed);
  std::vector<Failed> retries = std::move(retries_);  // the toast's Retry keeps every failure it covers
  retries.push_back({word, wanted, nowMs + retryInS * 1000UL});
  showToast(text + strings_.retrySuffix, nowMs, true, config::kFailureToastMs);  // it came late: longer
  retries_ = std::move(retries);
  failureToast_ = true;
}

Outcome CardController::retry(const unsigned long nowMs) {
  // Sent now (the user asked twice), or when a rate limit's back-off has passed: every call is refused
  // before then, so the latest back-off among the failures covers all of them.
  unsigned long readyAt = nowMs;
  for (const Failed& f : retries_) {
    if (timing::before(readyAt, f.notBeforeMs)) readyAt = f.notBeforeMs;
  }
  Outcome o{Effect::Redraw, false, {}};
  for (const Failed& f : retries_) {
    const LevelChange change{f.word, levels_[f.word], f.wanted, readyAt};
    for (const int same : source_.sameWord(f.word)) levels_[same] = f.wanted;
    if (change.from != change.to) o.changes.push_back(change);
  }
  if (hasWord()) state_.level = levels_[word_];
  showToast(strings_.retrying, nowMs);  // clears retries_
  return o;
}

Outcome CardController::home() {
  if (state_.view == View::Expanded) {
    state_.view = View::Card;
    return {Effect::Redraw, false};
  }
  return {Effect::Close, false};
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
