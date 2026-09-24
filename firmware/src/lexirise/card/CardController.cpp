#if LEXIRISE

#include "CardController.h"

#include <iterator>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/text/Utf8Prefix.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

namespace {

std::string firstCodepoint(const std::string& text) { return std::string(text::utf8FirstChars(text, 1)); }

}  // namespace

CardController::CardController(const BenchBook& book, const ReadingMode reading, const bool low, CardStrings strings)
    : book_(book), strings_(strings), low_(low), levels_(book.saved) {
  state_.reading = reading;
}

void CardController::open(const unsigned long nowMs) {
  word_ = book_.start;
  state_.view = View::Card;
  state_.tab = 0;
  clearToast();
  syncWord();
  startPhases(Phase::Pending, nowMs, config::kBenchPhaseBMs);
}

void CardController::syncWord() {
  state_.level = levels_[word_];
  const CardWord& w = currentWord();
  state_.pendingText = firstCodepoint(w.surface.empty() ? w.word : w.surface);
  state_.pageNumber = book_.pageNumber;
}

void CardController::startPhases(const Phase first, const unsigned long nowMs, const unsigned long toB) {
  state_.phase = first;
  phaseADueMs_ = first == Phase::Pending ? nowMs + config::kBenchPhaseAMs : nowMs;
  phaseBDueMs_ = nowMs + toB;
}

bool CardController::tick(const unsigned long nowMs) {
  bool changed = false;
  if (state_.phase == Phase::Pending && timing::reached(nowMs, phaseADueMs_)) {
    // B close behind A: skip A's refresh (popup-ui.md §2, ~0.5 s per partial refresh).
    state_.phase = timing::reached(nowMs + config::kPhaseMergeMs, phaseBDueMs_) ? Phase::Complete : Phase::Analyzed;
    changed = true;
  } else if (state_.phase == Phase::Analyzed && timing::reached(nowMs, phaseBDueMs_)) {
    state_.phase = Phase::Complete;
    changed = true;
  }
  if (!state_.toast.empty() && timing::reached(nowMs, toastUntilMs_)) {
    clearToast();
    changed = true;
  }
  return changed;
}

std::optional<unsigned long> CardController::nextDueMs() const {
  std::optional<unsigned long> due;
  if (state_.phase == Phase::Pending) due = phaseADueMs_;
  if (state_.phase == Phase::Analyzed) due = phaseBDueMs_;
  if (!state_.toast.empty() && (!due || timing::before(toastUntilMs_, *due))) due = toastUntilMs_;
  return due;
}

bool CardController::step(const int direction, const unsigned long nowMs) {
  const int next = word_ + (direction > 0 ? 1 : -1);
  if (next < 0 || next >= static_cast<int>(book_.words.size())) return false;  // v0.1: stop at the ends
  word_ = next;
  clearToast();  // a toast (and its Undo) belongs to the word it was about
  syncWord();
  // Stepping re-runs only dictionary/lookup: the word is known at once, the translation follows.
  startPhases(Phase::Analyzed, nowMs, config::kBenchPhaseBMs - config::kBenchPhaseAMs);
  return true;
}

void CardController::showToast(std::string text, const unsigned long nowMs, const bool undo) {
  state_.toast = std::move(text);
  state_.toastUndo = undo;
  toastUntilMs_ = nowMs + config::kToastMs;
  if (!undo) undoWord_ = -1;
}

void CardController::clearToast() {
  state_.toast.clear();
  state_.toastUndo = false;
  undoWord_ = -1;
}

Outcome CardController::tap(const Hit* hit, const unsigned long nowMs) {
  if (!hit) {  // the page outside the card: close (the expanded view covers the page)
    return {state_.view == View::Card ? Effect::Close : Effect::None, false};
  }
  switch (hit->target) {
    case Target::Level: {
      const Level before = levels_[word_];
      const auto level = static_cast<Level>(hit->index);
      levels_[word_] = level;
      state_.level = level;
      showToast(std::string(before == Level::None ? strings_.savedAs : strings_.now) + strings_.levelNames[hit->index] +
                    strings_.undoSuffix,
                nowMs, true);
      undoWord_ = word_;
      undoLevel_ = before;
      return {Effect::Redraw, false};
    }
    case Target::ToastUndo: {  // a new save is removed; a level change goes back (popup-ui.md §3.2)
      if (undoWord_ != word_) return {};
      const Level restored = undoLevel_;
      levels_[word_] = restored;
      state_.level = restored;
      showToast(restored == Level::None ? std::string(strings_.removed)
                                        : std::string(strings_.now) + strings_.levelNames[static_cast<int>(restored)],
                nowMs);
      return {Effect::Redraw, false};
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
      if (hit->index == 0) {  // Undo save
        levels_[word_] = Level::None;
        state_.level = Level::None;
        showToast(strings_.removed, nowMs);
      } else if (hit->index >= 1 && hit->index <= static_cast<int>(std::size(strings_.actionDone))) {
        showToast(strings_.actionDone[hit->index - 1], nowMs);
      }
      return {Effect::Redraw, false};
    case Target::Card:
      return {};
  }
  return {};
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
