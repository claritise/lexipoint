#include "CardController.h"

#include <algorithm>
#include <iterator>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/page/MarkRule.h"
#include "lexirise/text/Utf8Prefix.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

CardController::CardController(CardSource& source, const ReadingMode reading, const CardStrings& strings)
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
  closePreview();
  pendingStep_ = -1;
  jumpedAtMs_.reset();
  syncWord(nowMs);
}

bool CardController::syncWord(const unsigned long nowMs) {
  // The sentence just arrived (a lookup answers after phase 0): the card is on the tapped word.
  if (levels_.empty() && source_.wordCount() > 0) word_ = source_.startWord();
  while (static_cast<int>(levels_.size()) < source_.wordCount()) {
    // A word already on the card (the same entry in an earlier sentence) has the level the user set, which
    // may not have reached Lexirise yet (its Undo window): the new one shows that.
    const int i = static_cast<int>(levels_.size());
    const auto same = source_.sameWord(i);
    const auto earlier = std::find_if(same.begin(), same.end(), [i](const int w) { return w < i; });
    levels_.push_back(earlier != same.end() ? levels_[static_cast<size_t>(*earlier)] : source_.savedLevel(i));
  }
  const CardState before = state_;
  const int wordBefore = word_;
  if (pendingStep_ >= 0 && !source_.extending()) {  // the next sentence came, or didn't
    const int first = pendingStep_;
    pendingStep_ = -1;
    const int to = nextMarked(first, 1);  // A3: its first marked word
    if (to < source_.wordCount()) {
      jumpedFrom_ = word_;
      moveTo(to, nowMs);
      jumpedAtMs_ = nowMs;
    } else if (first < source_.wordCount()) {
      // A3: the sentence came with no marked word: on into the next one. At the page's end the press stops there,
      // silently: the card stays on its word (no toast, as a step past the page's last word does today).
      if (source_.extend(nowMs)) pendingStep_ = source_.wordCount();
    } else {
      nextSentenceFailed(nowMs);
    }
  }
  state_.phase = hasWord() ? source_.phase(word_) : Phase::Pending;
  // The tab stays across words, but a language with fewer tabs (⋯ is last) can't be left past its end.
  if (hasWord()) state_.tab = std::min(state_.tab, tabCount(currentWord().language) - 1);
  state_.level = hasWord() ? levels_[word_] : Level::None;
  state_.ignored = hasWord() && source_.ignored(word_);
  state_.pendingText = source_.pendingText();
  state_.pageNumber = source_.pageNumber();
  return state_.phase != before.phase || state_.level != before.level || state_.pendingText != before.pendingText ||
         state_.pageNumber != before.pageNumber || state_.tab != before.tab || state_.toast != before.toast ||
         state_.ignored != before.ignored || word_ != wordBefore;
}

void CardController::nextSentenceFailed(const unsigned long nowMs) {
  const std::optional<CallFailure> why = source_.extendFailure();
  if (!why) return;  // the page simply ended
  // A failed save's Retry, or a save's Undo still in its window, matters more than this: it stays.
  if (failureToast_ || state_.toastUndo) return;
  const char* text = strings_.nextSentenceFailed;
  if (*why == CallFailure::KeyRejected) text = strings_.keyRejected;
  if (*why == CallFailure::RateLimited) text = strings_.rateLimited;
  showToast(text, nowMs, false, config::kFailureToastMs);
}

bool CardController::tick(const unsigned long nowMs) {
  bool changed = source_.tick(nowMs) && syncWord(nowMs);
  if (!state_.toast.empty() && timing::reached(nowMs, toastUntilMs_)) {
    clearToast();
    changed = true;
  }
  return changed;
}

bool CardController::sourceChanged(const unsigned long nowMs) { return syncWord(nowMs); }

std::optional<unsigned long> CardController::nextDueMs() const {
  std::optional<unsigned long> due = source_.nextDueMs();
  if (!state_.toast.empty() && (!due || timing::before(toastUntilMs_, *due))) due = toastUntilMs_;
  return due;
}

bool CardController::step(const int direction, const unsigned long nowMs,
                          const std::optional<unsigned long> pressedAtMs) {
  if (pressedBeforeJump(pressedAtMs)) {
    if (direction > 0) return false;  // held through the analysis: its job (off the last word) the jump did
    pendingStep_ = -1;                // a step back cancels any wait, as below
    // Back, as it would have been before the jump: from the word the card waited on.
    const int back = nextMarked(jumpedFrom_ - 1, -1);
    const int to = back >= 0 ? back : jumpedFrom_;
    jumpedFrom_ = to;               // a second such press goes on back from there
    if (to == word_) return false;  // already there (the sentence's first word): nothing to redraw
    moveTo(to, nowMs);
    syncWord(nowMs);
    return true;
  }
  if (direction < 0) {
    pendingStep_ = -1;  // back instead: the next sentence still loads, the card doesn't jump to it
  } else if (pendingStep_ >= 0) {
    return false;  // already on its way there
  }
  const int next = nextMarked(word_ + (direction > 0 ? 1 : -1), direction > 0 ? 1 : -1);
  if (next < 0) return false;  // the tapped sentence's start: the card doesn't go back before it
  if (next >= source_.wordCount()) {
    // The sentence's end: on into the page's next one once it's analyzed (syncWord moves the card); at the
    // page's end, stop. A sentence already loading (the card stepped back meanwhile) is waited for again.
    if (hasWord() && next == source_.wordCount() && (source_.extending() || source_.extend(nowMs))) {
      pendingStep_ = next;
    }
    return false;  // nothing changes on screen until it comes
  }
  moveTo(next, nowMs);
  syncWord(nowMs);
  return true;
}

bool CardController::marked(const int index) const {
  if (source_.neverMarked(index)) return false;
  const Level level = index < static_cast<int>(levels_.size()) ? levels_[index] : source_.savedLevel(index);
  // The page's own rule (page::markForLevel): Level::None is not saved (or level 0); T L F K are levels 1-4.
  return page::markForLevel(level != Level::None, proficiencyOf(level)) != page::Mark::None;
}

int CardController::nextMarked(int from, const int direction) const {
  if (!source_.stepsMarkedWords()) return from;
  while (from >= 0 && from < source_.wordCount() && !marked(from)) from += direction;
  return from;
}

bool CardController::pressedBeforeJump(const std::optional<unsigned long> pressedAtMs) const {
  return pressedAtMs && jumpedAtMs_ && timing::before(*pressedAtMs, *jumpedAtMs_ + config::kStepAfterJumpGraceMs);
}

void CardController::moveTo(const int index, const unsigned long nowMs) {
  word_ = index;
  steps_++;
  closePreview();  // the preview is this word's sentence: a step leaves it unsaved
  // A toast (and its Undo) belongs to the word it was about; a failure is about a save the user believes
  // made, and it came late: it stays for its time, Retry and all.
  if (!failureToast_) clearToast();
  source_.focus(word_, nowMs);
}

void CardController::showToast(std::string text, const unsigned long nowMs, const bool tappable,
                               const unsigned long durationMs) {
  state_.toast = std::move(text);
  state_.toastUndo = tappable;
  toastUntilMs_ = nowMs + durationMs;
  if (!tappable) undo_ = {};
  retries_.clear();  // a new toast replaces a Retry (levelFailed keeps the list it adds to)
  failureToast_ = false;
}

void CardController::clearToast() {
  state_.toast.clear();
  state_.toastUndo = false;
  undo_ = {};
  retries_.clear();
  failureToast_ = false;
}

Outcome CardController::tap(const Hit* hit, const unsigned long nowMs, const std::optional<PagePoint> at) {
  if (hit && hit->target == Target::OwnWord) return {};  // P10: the word on the page: it's already on the card
  // Anything done on the card while it waits for the next sentence is about this word: the card stays (the
  // jump would clear a save's Undo toast just made). The words still arrive; the next step goes to them.
  if (hit) pendingStep_ = -1;
  if (!hit) {  // the page outside the card: close, to look up the word there (the expanded view covers the page)
    if (state_.view != View::Card) return {};
    Outcome o{Effect::Close, false};
    o.lookUpAt = at;
    return o;
  }
  switch (hit->target) {
    case Target::Level: {
      if (!hasWord()) return {};
      const auto level = static_cast<Level>(hit->index);
      const Level before = levels_[word_];
      if (level == before) return {};  // already there: a double tap sends nothing twice
      Outcome o = setLevel(level, before == Level::None ? strings_.savedAs : strings_.now, true, nowMs);
      undo_ = {Undo::Kind::Level, word_, before, std::nullopt};
      return o;
    }
    case Target::ToastUndo: {  // a new save is removed; a level change goes back (popup-ui.md §3.2)
      if (!retries_.empty()) return retry(nowMs);
      if (undo_.kind == Undo::Kind::None || undo_.word != word_) return {};
      if (undo_.kind == Undo::Kind::Ignore || undo_.kind == Undo::Kind::Unignore) {
        // Back as it was; the toast goes (the ⋯ row's words follow).
        const bool on = undo_.kind == Undo::Kind::Unignore;
        source_.setIgnored(word_, on);
        state_.ignored = on;
        Outcome o{Effect::Redraw, false};
        o.ignores.push_back({word_, on, on ? std::nullopt : undo_.evicted});
        clearToast();
        return o;
      }
      if (undo_.kind == Undo::Kind::Sentence) {  // the sentence card taken back (unsent while in its window)
        Outcome o{Effect::Redraw, false};
        o.sentences.push_back({word_, {}, true, nowMs});
        showToast(strings_.removed, nowMs);
        return o;
      }
      const Level restored = undo_.level;
      if (restored == Level::None) {
        Outcome o = setLevel(restored, "", false, nowMs);
        showToast(strings_.removed, nowMs);
        return o;
      }
      return setLevel(restored, strings_.now, false, nowMs);
    }
    case Target::RankRow:
      state_.view = state_.view == View::Card ? View::Expanded : View::Card;
      closePreview();
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
      if (state_.tab == hit->index && !preview_) return {};
      state_.tab = hit->index;  // the ⋯ tab again, or another: the preview closes unsaved
      closePreview();
      return {Effect::Redraw, false};
    case Target::Action:
      if (!hasWord()) return {};
      if (hit->index == ActionId::UndoSave) {
        if (levels_[word_] == Level::None) return {};
        Outcome o = setLevel(Level::None, "", false, nowMs);
        showToast(strings_.removed, nowMs);
        return o;
      }
      if (hit->index == ActionId::SaveSentence) return openPreview(nowMs);
      if (hit->index == ActionId::Shorter || hit->index == ActionId::Longer) {
        return changePreview(hit->index == ActionId::Longer);
      }
      if (hit->index == ActionId::SaveSentenceNow) return saveSentence(nowMs);
      if (hit->index == ActionId::Ignore) return ignore(nowMs);
      if (hit->index == ActionId::LookUpLater) {
        showToast(source_.demoActions() ? strings_.actionDone[actionIndex(hit->index)] : strings_.notYet, nowMs);
      }
      return {Effect::Redraw, false};
    case Target::Card:
    case Target::OwnWord:  // handled above; here for the switch's completeness
      return {};
  }
  return {};
}

Outcome CardController::ignore(const unsigned long nowMs) {
  // A second tap while this word's ignore (or un-ignore) Undo is up: nothing (as a level's double tap), so the Undo
  // stays, and a tap matched against the frame before the first one's never undoes it.
  const bool undoUp = undo_.kind == Undo::Kind::Ignore || undo_.kind == Undo::Kind::Unignore;
  if (state_.toastUndo && undoUp && undo_.word == word_) return {};
  if (!source_.keepsIgnoreList()) {  // nowhere to keep it
    showToast(strings_.sdCardFailed, nowMs, false, config::kFailureToastMs);
    return {Effect::Redraw, false};
  }
  const bool on = !source_.ignored(word_);  // the row reads Undo ignore for an ignored word
  if (!source_.setIgnored(word_, on)) {     // a word with no key (no entry id, no usable form): can't be listed
    showToast(strings_.cantIgnore, nowMs);
    return {Effect::Redraw, false};
  }
  state_.ignored = on;
  Outcome o{Effect::Redraw, false};
  o.ignores.push_back({word_, on, std::nullopt});
  const char* text = on ? strings_.actionDone[actionIndex(ActionId::Ignore)] : strings_.noLongerIgnored;
  showToast(std::string(text) + strings_.undoSuffix, nowMs, true, config::kIgnoreToastMs);
  undo_ = {on ? Undo::Kind::Ignore : Undo::Kind::Unignore, word_, Level::None, std::nullopt};
  return o;
}

Outcome CardController::openPreview(const unsigned long nowMs) {
  if (preview_) return {};  // open already (a tap matched against the frame before it)
  std::optional<SentenceForSave> sentence = source_.sentenceForSave(word_);
  if (!sentence || text::trimmedSpaces(sentence->text).empty()) {  // no sentence to save (a source without one)
    showToast(source_.demoActions() ? strings_.actionDone[actionIndex(ActionId::SaveSentence)] : strings_.notYet,
              nowMs);
    return {Effect::Redraw, false};
  }
  preview_.emplace(*sentence);
  syncPreview();
  return {Effect::Redraw, false};
}

Outcome CardController::changePreview(const bool longer) {
  if (!preview_ || !(longer ? preview_->longer() : preview_->shorter())) return {};  // nothing to do: nothing changes
  syncPreview();
  return {Effect::Redraw, false};
}

Outcome CardController::saveSentence(const unsigned long nowMs) {
  if (!preview_) return {};
  std::string text = preview_->shown().text;
  closePreview();  // back to the rows, with the save's toast
  Outcome o{Effect::Redraw, false};
  if (text.empty()) return o;
  o.sentences.push_back({word_, std::move(text), false, nowMs + config::kToastMs});
  showToast(std::string(strings_.actionDone[actionIndex(ActionId::SaveSentence)]) + strings_.undoSuffix, nowMs, true);
  undo_ = {Undo::Kind::Sentence, word_, Level::None, std::nullopt};
  return o;
}

void CardController::closePreview() {
  preview_.reset();
  state_.preview.reset();
  state_.previewLonger.reset();
}

void CardController::syncPreview() {
  state_.preview = preview_->shown();
  state_.previewLonger = preview_->longerShown();
}

void CardController::sentenceFailed(const unsigned long nowMs) {
  showToast(strings_.saveFailed, nowMs, false, config::kFailureToastMs);
  failureToast_ = true;  // it came late: a step keeps it
}

void CardController::ignoreEvicted(const int word, IgnoredKey evicted) {
  if (undo_.kind == Undo::Kind::Ignore && undo_.word == word) undo_.evicted = std::move(evicted);
}

void CardController::ignoreFailed(const int word, const bool wanted, const unsigned long nowMs) {
  if (word < 0 || word >= source_.wordCount()) return;
  source_.setIgnored(word, !wanted);
  state_.ignored = hasWord() && source_.ignored(word_);  // the word shown may be another copy of that entry
  showToast(strings_.sdCardFailed, nowMs, false, config::kFailureToastMs);
  failureToast_ = true;  // it came after the input: a step in the same batch, or a sentence failing, keeps it
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
  Outcome o{Effect::Redraw, false};
  o.changes.push_back(change);
  return o;
}

bool CardController::savedLevelChanged(const int word) {
  if (word < 0 || word >= static_cast<int>(levels_.size())) return false;
  const Level level = source_.savedLevel(word);
  for (const int same : source_.sameWord(word)) levels_[same] = level;
  const Level before = state_.level;
  if (hasWord()) state_.level = levels_[word_];
  return state_.level != before;
}

void CardController::levelFailed(const int word, const Level level, const unsigned long nowMs, const CallFailure why,
                                 const Level wanted, const uint32_t retryInS) {
  if (word < 0 || word >= static_cast<int>(levels_.size())) return;
  for (const int same : source_.sameWord(word)) levels_[same] = level;
  if (hasWord()) state_.level = levels_[word_];
  // Said even when the card moved on: the user thinks it's saved (offline-and-errors.md §3).
  if (why == CallFailure::KeyRejected) {
    showToast(strings_.keyRejected, nowMs, false, config::kFailureToastMs);
    failureToast_ = true;
    return;
  }
  std::string text = why == CallFailure::RateLimited
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
  Outcome o{Effect::Redraw, false};
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
  pendingStep_ = -1;  // as for a tap: the card doesn't jump after it
  if (state_.view == View::Expanded) {
    state_.view = View::Card;
    closePreview();
    return {Effect::Redraw, false};
  }
  return {Effect::Close, false};
}

Outcome CardController::swipe(const Swipe direction) {
  pendingStep_ = -1;  // as for a tap: the card stays on this word
  // Before the word arrives there's no detail view to open or tab to change (the rank row isn't a target
  // either): only a swipe down (close) means something.
  if (direction != Swipe::Down && (!hasWord() || state_.phase == Phase::Pending)) return {};
  const bool expanded = state_.view == View::Expanded;
  switch (direction) {
    case Swipe::Up:
      if (expanded) return {};
      state_.view = View::Expanded;
      closePreview();
      return {Effect::Redraw, false};
    case Swipe::Down:
      return home();
    case Swipe::Left:
    case Swipe::Right: {
      if (!expanded) return {};
      const int tab = state_.tab + (direction == Swipe::Left ? 1 : -1);
      if (tab < 0 || tab >= tabCount(currentWord().language)) return {};
      state_.tab = tab;
      closePreview();
      return {Effect::Redraw, false};
    }
  }
  return {};
}

Outcome CardController::longPress(const Hit* hit, const int x, const int y) {
  if (hit || state_.view != View::Card) return {};
  Outcome o{Effect::Close, false};
  o.lookUpAt = PagePoint{x, y};
  return o;
}

}  // namespace lexipoint::card
