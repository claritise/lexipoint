// popup-ui.md §2-3: the card's phases, stepping, taps and Home, with time passed in.

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/card/BenchSource.h"
#include "lexirise/card/CardController.h"
#include "lexirise/card/CardInput.h"
#include "lexirise/card/CardOrientation.h"

using namespace lexipoint::card;
namespace config = lexipoint::config;

namespace {

Hit hit(const Target t, const int index = 0) { return {t, index, {}}; }

}  // namespace

TEST(CardController, PhasesPlayOnTheTimer) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(1000);
  EXPECT_EQ(c.state().phase, Phase::Pending);
  EXPECT_EQ(c.state().pendingText, "煩");  // the surface's first character
  EXPECT_EQ(c.highlightCodepoints(), 1);
  EXPECT_FALSE(c.tick(1000 + config::kBenchPhaseAMs - 1));
  EXPECT_TRUE(c.tick(1000 + config::kBenchPhaseAMs));
  EXPECT_EQ(c.state().phase, Phase::Analyzed);
  EXPECT_EQ(c.highlightCodepoints(), 0);  // the highlight grows to the word
  EXPECT_TRUE(c.tick(1000 + config::kBenchPhaseBMs));
  EXPECT_EQ(c.state().phase, Phase::Complete);
  EXPECT_FALSE(c.tick(99999));
}

TEST(CardController, APhaseBCloseBehindAIsMerged) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  // Checked late: B is due within the merge window, so A is skipped.
  EXPECT_TRUE(c.tick(config::kBenchPhaseBMs - config::kPhaseMergeMs));
  EXPECT_EQ(c.state().phase, Phase::Complete);
}

TEST(CardController, SideButtonsStepWordsAndStopAtTheStart) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  c.tick(config::kBenchPhaseBMs);
  ASSERT_EQ(c.word(), 2);
  EXPECT_EQ(c.state().level, Level::Learning);
  const Hit rank = hit(Target::RankRow);
  const Hit tab = hit(Target::Tab, 3);
  c.tap(&rank, 0);  // expanded, then a tab: both survive a step
  c.tap(&tab, 0);
  EXPECT_TRUE(c.step(+1, 5000));
  EXPECT_EQ(c.word(), 3);
  EXPECT_EQ(c.state().view, View::Expanded);
  EXPECT_EQ(c.state().tab, 3);
  EXPECT_EQ(c.state().level, Level::None);
  EXPECT_EQ(c.state().phase, Phase::Analyzed);  // only the lookup re-runs
  EXPECT_TRUE(c.tick(5000 + config::kBenchPhaseBMs - config::kBenchPhaseAMs));
  EXPECT_EQ(c.state().phase, Phase::Complete);
  EXPECT_TRUE(c.step(+1, 6000));
  EXPECT_TRUE(c.step(+1, 6000));
  EXPECT_FALSE(c.step(+1, 6000));  // the last word: the card waits there for the next sentence (P9)
  EXPECT_EQ(c.word(), 5);
  EXPECT_TRUE(c.awaitingNext());
  for (int i = 0; i < 5; i++) c.step(-1, 7000);
  EXPECT_FALSE(c.awaitingNext());  // stepping back cancelled it
  EXPECT_FALSE(c.step(-1, 7000));
  EXPECT_EQ(c.word(), 0);
}

TEST(CardController, SavingShowsTheToastAndItExpires) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  c.step(+1, 0);  // 彼: not saved
  const Hit level1 = hit(Target::Level, 1);
  EXPECT_EQ(c.tap(&level1, 100).effect, Effect::Redraw);
  EXPECT_EQ(c.state().level, Level::Learning);
  EXPECT_EQ(c.state().toast, "Saved as learning  \xC2\xB7  Undo");
  const Hit level3 = hit(Target::Level, 3);
  c.tap(&level3, 200);
  EXPECT_EQ(c.state().toast, "Now known  \xC2\xB7  Undo");
  EXPECT_FALSE(c.tick(200 + config::kToastMs - 1) && c.state().toast.empty());
  c.tick(200 + config::kToastMs);
  EXPECT_TRUE(c.state().toast.empty());
  // Stepping away and back keeps the level (per word).
  c.step(-1, 3000);
  c.step(+1, 3000);
  EXPECT_EQ(c.state().level, Level::Known);
  // Undo (⋯ action 0).
  const Hit undo = hit(Target::Action, 0);
  c.tap(&undo, 4000);
  EXPECT_EQ(c.state().level, Level::None);
  EXPECT_EQ(c.state().toast, "Removed from Lexirise");
}

TEST(CardController, ReadingToggleAsksToPersist) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  const Hit rd = hit(Target::ReadingLine);
  const Outcome o = c.tap(&rd, 0);
  EXPECT_TRUE(o.readingChanged);
  EXPECT_EQ(c.state().reading, ReadingMode::Romaji);
  EXPECT_EQ(c.state().toast, "Readings: romaji");
  c.tap(&rd, 10);
  EXPECT_EQ(c.state().reading, ReadingMode::Kana);
}

TEST(CardController, ClosingAndHome) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  EXPECT_EQ(c.tap(nullptr, 0).effect, Effect::Close);  // the page, in card view
  const Hit close = hit(Target::Close);
  EXPECT_EQ(c.tap(&close, 0).effect, Effect::Close);
  const Hit card = hit(Target::Card);
  EXPECT_EQ(c.tap(&card, 0).effect, Effect::None);  // the card swallows other taps
  const Hit rank = hit(Target::RankRow);
  c.tap(&rank, 0);
  ASSERT_EQ(c.state().view, View::Expanded);
  EXPECT_EQ(c.tap(nullptr, 0).effect, Effect::None);  // the strip: the expanded view covers the page
  EXPECT_EQ(c.home().effect, Effect::Redraw);         // Home: back to the card
  EXPECT_EQ(c.state().view, View::Card);
  EXPECT_EQ(c.home().effect, Effect::Close);  // then close
}

TEST(CardController, ToastUndoRevertsTheSave) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  c.step(+1, 0);  // 彼: not saved
  const Hit save = hit(Target::Level, 1);
  c.tap(&save, 100);
  EXPECT_TRUE(c.state().toastUndo);
  const Hit undo = hit(Target::ToastUndo);
  EXPECT_EQ(c.tap(&undo, 200).effect, Effect::Redraw);
  EXPECT_EQ(c.state().level, Level::None);  // a new save is removed
  EXPECT_EQ(c.state().toast, "Removed from Lexirise");
  EXPECT_FALSE(c.state().toastUndo);
  // A level change goes back to the old level.
  c.step(-1, 300);  // 煩わしい: learning
  const Hit known = hit(Target::Level, 3);
  c.tap(&known, 400);
  c.tap(&undo, 500);
  EXPECT_EQ(c.state().level, Level::Learning);
  EXPECT_EQ(c.state().toast, "Now learning");
}

TEST(CardController, StepClearsTheToastAndItsUndo) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  const Hit save = hit(Target::Level, 0);
  c.tap(&save, 0);
  c.step(+1, 10);
  EXPECT_TRUE(c.state().toast.empty());
  EXPECT_FALSE(c.state().toastUndo);
  const Hit undo = hit(Target::ToastUndo);
  EXPECT_EQ(c.tap(&undo, 20).effect, Effect::None);  // nothing to undo for this word
  EXPECT_EQ(c.state().level, Level::None);
}

TEST(CardController, NextDueIsThePhaseOrTheToast) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(1000);
  EXPECT_EQ(c.nextDueMs(), 1000 + config::kBenchPhaseAMs);
  c.tick(1000 + config::kBenchPhaseAMs);
  EXPECT_EQ(c.nextDueMs(), 1000 + config::kBenchPhaseBMs);
  c.tick(1000 + config::kBenchPhaseBMs);
  EXPECT_FALSE(c.nextDueMs());  // idle: loop() needn't take the lock
  const Hit save = hit(Target::Level, 2);
  c.tap(&save, 5000);
  EXPECT_EQ(c.nextDueMs(), 5000 + config::kToastMs);
}

TEST(CardController, DeadlinesSurviveTheMillisWrap) {
  constexpr unsigned long kJustBeforeWrap = 0xFFFFFFFFUL - config::kBenchPhaseAMs / 2;
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(kJustBeforeWrap);                    // phase A falls due just past the wrap
  EXPECT_FALSE(c.tick(kJustBeforeWrap + 1));  // not taken as long past
  EXPECT_EQ(c.state().phase, Phase::Pending);
  EXPECT_TRUE(c.tick(static_cast<uint32_t>(kJustBeforeWrap + config::kBenchPhaseAMs)));  // the wrapped time
  EXPECT_NE(c.state().phase, Phase::Pending);
  ASSERT_TRUE(c.nextDueMs());
}

TEST(CardController, TabsAndActions) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  const Hit tab = hit(Target::Tab, 2);
  EXPECT_EQ(c.tap(&tab, 0).effect, Effect::Redraw);
  EXPECT_EQ(c.tap(&tab, 0).effect, Effect::None);  // already there
  const Hit later = hit(Target::Action, 3);
  c.tap(&later, 0);
  EXPECT_EQ(c.state().toast, "Flagged for later");
  EXPECT_FALSE(c.state().toastUndo);
}

TEST(CardController, TheBenchsIgnorePlaysTheReferencesToast) {
  // The bench keeps no ignore list (C17, V5): its ⋯ Ignore is the reference's toast, nothing more.
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  c.tick(config::kBenchPhaseBMs);
  const Hit ignoreRow = hit(Target::Action, ActionId::Ignore);
  const Outcome o = c.tap(&ignoreRow, 1000);
  EXPECT_TRUE(o.ignores.empty());
  EXPECT_EQ(c.state().toast, "Ignored: won't be marked again");
  EXPECT_FALSE(c.state().toastUndo);
}

namespace {

Hit at(const Target t, const Rect r, const int index = 0) { return {t, index, r}; }

}  // namespace

TEST(CardInput, ATapBeforeTheFirstFrameIsDroppedNotAClose) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  ShownTargets shown;
  PendingInput in;
  in.tap(5, 5, 100);  // on the page, while the first frame is still refreshing
  EXPECT_NE(handleInput(c, shown, in, 100).effect, Effect::Close);
}

TEST(CardInput, ATapDuringARefreshHitsTheFrameTheUserSaw) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  ShownTargets shown;
  shown.drawing({at(Target::Level, {0, 0, 50, 50}, 1), at(Target::Card, {0, 0, 480, 400})}, c.steps(), c.state().view);
  shown.shown(100);
  shown.drawing({at(Target::Card, {0, 0, 480, 400})}, c.steps(), c.state().view);  // phase B moved T L F K away
  shown.shown(600);
  PendingInput in;
  in.tap(10, 10, 400);  // read during B's refresh
  const Outcome o = handleInput(c, shown, in, 700);
  EXPECT_EQ(o.effect, Effect::Redraw);
  EXPECT_EQ(c.state().level, Level::Learning);  // saved at L, as tapped
}

TEST(CardInput, EventsRunInOrderAtTheirOwnTimesAndStopAtAClose) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  const int start = c.word();
  ShownTargets shown;
  shown.drawing({at(Target::Close, {0, 0, 50, 50})}, 1, View::Card);  // the next word's card (one step on), up at 25
  shown.shown(25);
  PendingInput in;
  in.step(+1, 20);
  in.tap(10, 10, 30);  // ✕
  in.step(+1, 40);     // after the close: ignored
  EXPECT_EQ(handleInput(c, shown, in, 50).effect, Effect::Close);
  EXPECT_EQ(c.word(), start + 1);
}

TEST(CardInput, TwoReadingSwitchesCancelOut) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  ShownTargets shown;
  shown.drawing({at(Target::ReadingLine, {0, 0, 50, 50})}, c.steps(), c.state().view);
  shown.shown(10);
  PendingInput one;
  one.tap(10, 10, 20);
  EXPECT_TRUE(handleInput(c, shown, one, 20).readingChanged);  // kana → romaji

  BenchSource freshSource(benchJapanese(), false);
  CardController fresh(freshSource, ReadingMode::Kana);
  fresh.open(0);
  PendingInput two;  // two taps in one batch: kana → romaji → kana
  two.tap(10, 10, 20);
  two.tap(10, 10, 30);
  EXPECT_FALSE(handleInput(fresh, shown, two, 30).readingChanged);
  EXPECT_EQ(fresh.state().reading, ReadingMode::Kana);
}

TEST(CardInput, ATapOnTheOldCardDuringAStepIsDropped) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  c.tick(60000);
  const int a = c.word();
  ShownTargets shown;
  shown.drawing({at(Target::Level, {0, 0, 50, 50}, 3)}, c.steps(), c.state().view);  // word A's card, K at the top left
  shown.shown(60000);
  PendingInput in;
  in.step(+1, 60100);     // → word B; its refresh starts
  in.tap(10, 10, 60200);  // K, still on A's card
  EXPECT_NE(handleInput(c, shown, in, 60300).effect, Effect::Close);
  EXPECT_EQ(c.word(), a + 1);
  EXPECT_EQ(c.state().level, benchJapanese().saved[a + 1]);  // B: unchanged
  EXPECT_TRUE(c.state().toast.empty());
}

// The dev harness's target sets (lxctl's CARD_TARGET_LOG and CARD_LEVEL_LOG): the targets a smoke taps, the reading
// line and ✕ among them; not the card's body or the page's word.
TEST(CardInput, TheTargetsASmokeTapsAreListed) {
  char line[kTargetLineSize];
  ASSERT_TRUE(formatTargetLine(at(Target::Level, {1, 2, 3, 4}, 2), true, line, sizeof(line)));
  EXPECT_STREQ(line, "level 2 1 2 3 4 1");
  ASSERT_TRUE(formatTargetLine(at(Target::Level, {1, 2, 3, 4}, 0), false, line, sizeof(line)));
  EXPECT_STREQ(line, "level 0 1 2 3 4 0");
  const std::pair<Target, const char*> listed[] = {{Target::RankRow, "rank"},        {Target::Tab, "tab"},
                                                   {Target::Action, "action"},       {Target::ToastUndo, "undo"},
                                                   {Target::ReadingLine, "reading"}, {Target::Close, "close"}};
  for (const auto& [target, name] : listed) {
    ASSERT_TRUE(formatTargetLine(at(target, {10, 20, 30, 40}, 3), false, line, sizeof(line))) << name;
    EXPECT_EQ(std::string(line), std::string("target ") + name + " 3 10 20 30 40");
  }
  EXPECT_FALSE(formatTargetLine(at(Target::Card, {0, 0, 1, 1}), false, line, sizeof(line)));
  EXPECT_FALSE(formatTargetLine(at(Target::OwnWord, {0, 0, 1, 1}), false, line, sizeof(line)));
}

// kTargetLineSize holds the longest target line: the longest target name with every number at its widest.
TEST(CardInput, TheLongestTargetLineFits) {
  const int m = std::numeric_limits<int>::min();
  char line[kTargetLineSize];
  ASSERT_TRUE(formatTargetLine(Hit{Target::ReadingLine, m, {m, m, m, m}}, true, line, sizeof(line)));
  EXPECT_STREQ(line, "target reading -2147483648 -2147483648 -2147483648 -2147483648 -2147483648");
  EXPECT_EQ(std::strlen(line), kTargetLineSize - 1);
  ASSERT_TRUE(formatTargetLine(Hit{Target::Level, m, {m, m, m, m}}, true, line, sizeof(line)));
  EXPECT_LT(std::strlen(line), kTargetLineSize - 1);  // a level line: "saved" is one digit
}

// kTapLineSize holds the longest tap line: the longest target name with every number at its widest.
TEST(CardInput, TheLongestTapLineFits) {
  const int m = std::numeric_limits<int>::min();
  char line[kTapLineSize];
  formatTapSeen(TapSeen{m, m, false, Hit{Target::ReadingLine, m, {}}}, line, sizeof(line));
  EXPECT_STREQ(line, "tap -2147483648 -2147483648 reading -2147483648");
  EXPECT_EQ(std::strlen(line), kTapLineSize - 1);
}

// The dev harness's tap lines ("[LXCARD] tap …", lxctl's CARD_TAP_LOG): what each tap met, a dropped one too.
TEST(CardInput, EachTapIsReportedWithWhatItMet) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  ShownTargets shown;
  shown.drawing({at(Target::ReadingLine, {0, 0, 50, 50}), at(Target::Tab, {100, 0, 50, 50}, 2)}, c.steps(),
                c.state().view);
  shown.shown(10);
  PendingInput in;
  in.tap(10, 10, 20);        // the reading line
  in.tap(120, 10, 25);       // the tab, index 2
  in.step(+1, 30);           // not a tap: not reported (and the next word's card isn't on screen yet)
  in.tap(120, 10, 40);       // the old card's tab: dropped
  ASSERT_EQ(in.size(), 4u);  // all queued (config::kCardPendingInputMax)
  TapsSeen seen;
  seen.count = 3;  // whatever was there before is replaced
  handleInput(c, shown, in, 60, &seen);
  ASSERT_EQ(seen.count, 3u);
  char line[kTapLineSize];
  formatTapSeen(seen.taps[0], line, sizeof(line));
  EXPECT_STREQ(line, "tap 10 10 reading 0");
  formatTapSeen(seen.taps[1], line, sizeof(line));
  EXPECT_STREQ(line, "tap 120 10 tab 2");
  formatTapSeen(seen.taps[2], line, sizeof(line));
  EXPECT_STREQ(line, "tap 120 10 dropped");

  shown.drawing({at(Target::Close, {0, 0, 50, 50})}, c.steps(), c.state().view);  // the next word's card
  shown.shown(70);
  PendingInput off;
  off.tap(300, 310, 80);  // off the card: it closes
  handleInput(c, shown, off, 90, &seen);
  ASSERT_EQ(seen.count, 1u);
  formatTapSeen(seen.taps[0], line, sizeof(line));
  EXPECT_STREQ(line, "tap 300 310 none");
  PendingInput close;
  close.tap(5, 7, 100);
  handleInput(c, shown, close, 110, &seen);
  formatTapSeen(seen.taps[0], line, sizeof(line));
  EXPECT_STREQ(line, "tap 5 7 close 0");

  shown.drawing({at(Target::Card, {0, 0, 480, 800})}, c.steps(), c.state().view);  // taps the card swallows
  shown.shown(115);
  constexpr int kFull = config::kCardPendingInputMax;
  PendingInput full;  // a full queue of taps: each reported
  for (int i = 0; i < kFull; i++) full.tap(200 + i, 300, 120);
  ASSERT_EQ(full.size(), static_cast<size_t>(kFull));
  handleInput(c, shown, full, 125, &seen);
  ASSERT_EQ(seen.count, static_cast<size_t>(kFull));
  formatTapSeen(seen.taps[kFull - 1], line, sizeof(line));
  EXPECT_EQ(std::string(line), "tap " + std::to_string(200 + kFull - 1) + " 300 card 0");

  PendingInput other;  // not taps: not reported
  other.swipe(Swipe::Up, 10, 10, 120);
  other.longPress(10, 10, 130);
  ASSERT_EQ(other.size(), 2u);
  handleInput(c, shown, other, 140, &seen);
  EXPECT_EQ(seen.count, 0u);
}

TEST(CardInput, EveryTargetHasALogName) {
  EXPECT_STREQ(targetName(Target::Level), "level");
  EXPECT_STREQ(targetName(Target::RankRow), "rank");
  EXPECT_STREQ(targetName(Target::Close), "close");
  EXPECT_STREQ(targetName(Target::ReadingLine), "reading");
  EXPECT_STREQ(targetName(Target::Tab), "tab");
  EXPECT_STREQ(targetName(Target::Action), "action");
  EXPECT_STREQ(targetName(Target::ToastUndo), "undo");
  EXPECT_STREQ(targetName(Target::Card), "card");
  EXPECT_STREQ(targetName(Target::OwnWord), "word");
}

TEST(CardInput, HomeIsNeverDroppedFromAFullQueue) {
  PendingInput in;
  for (int i = 0; i < config::kCardPendingInputMax; i++) in.tap(0, 0, static_cast<unsigned long>(i));
  in.home(99);
  ASSERT_EQ(in.size(), static_cast<size_t>(config::kCardPendingInputMax));
  EXPECT_EQ(in[in.size() - 1].kind, InputEvent::Kind::Home);
  EXPECT_EQ(in[0].kind, InputEvent::Kind::Tap);
}

TEST(CardInput, ABurstPastTheLimitKeepsTheOldest) {
  PendingInput in;
  for (int i = 0; i < config::kCardPendingInputMax + 2; i++) in.step(+1, static_cast<unsigned long>(i));
  ASSERT_EQ(in.size(), static_cast<size_t>(config::kCardPendingInputMax));
  EXPECT_EQ(in[0].ms, 0u);
  in.clear();
  EXPECT_TRUE(in.empty());
}

TEST(CardInput, NothingDueMeansNoRedraw) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  c.tick(60000);
  ShownTargets shown;
  EXPECT_EQ(handleInput(c, shown, PendingInput{}, 60001).effect, Effect::None);
}

TEST(CardOrientation, PortraitEitherWayUpStaysLandscapeTurnsPortrait) {
  enum class O { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  const auto card = [](O o) { return cardOrientation(o, O::Portrait, O::PortraitInverted); };
  EXPECT_EQ(card(O::Portrait), O::Portrait);
  EXPECT_EQ(card(O::PortraitInverted), O::PortraitInverted);
  EXPECT_EQ(card(O::LandscapeClockwise), O::Portrait);
  EXPECT_EQ(card(O::LandscapeCounterClockwise), O::Portrait);
}

TEST(CardInput, HomeQueuedBehindATapRunsAfterIt) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  c.tick(60000);
  const Hit rank = hit(Target::RankRow);
  c.tap(&rank, 60000);  // the expanded view
  ASSERT_EQ(c.state().view, View::Expanded);
  ShownTargets shown;
  shown.drawing({at(Target::RankRow, {0, 0, 50, 50})}, c.steps(), c.state().view);  // its ▲
  shown.shown(60010);
  PendingInput in;
  in.tap(10, 10, 60100);  // ▲ during a refresh
  in.home(60200);         // then Home: back to the card, then … nothing more
  const Outcome o = handleInput(c, shown, in, 60300);
  EXPECT_EQ(c.state().view, View::Card);  // ▲ first (expanded → card), then Home closes
  EXPECT_EQ(o.effect, Effect::Close);
}

TEST(CardInput, HomeOnTheCardClosesAndDropsWhatFollows) {
  BenchSource cSource(benchJapanese(), false);
  CardController c(cSource, ReadingMode::Kana);
  c.open(0);
  const int start = c.word();
  ShownTargets shown;
  PendingInput in;
  in.home(10);
  in.step(+1, 20);
  EXPECT_EQ(handleInput(c, shown, in, 30).effect, Effect::Close);
  EXPECT_EQ(c.word(), start);
}

namespace {

// A source whose words arrive later, as a lookup's do: none in phase 0, then the analyzed sentence.
class LateSource final : public CardSource {
 public:
  std::vector<CardWord> words;
  std::vector<Level> levels;
  std::vector<Phase> phases;
  int start = 0;
  std::vector<int> focused;

  int wordCount() const override { return static_cast<int>(words.size()); }
  int startWord() const override { return start; }
  const CardWord& word(const int i) const override { return words[i]; }
  Level savedLevel(const int i) const override { return levels[i]; }
  Phase phase(const int i) const override { return phases[i]; }
  std::string pendingText() const override { return "読"; }
  int pageNumber() const override { return 0; }
  void open(unsigned long) override {}
  void focus(const int i, unsigned long) override { focused.push_back(i); }
  bool tick(unsigned long) override { return false; }
  std::optional<unsigned long> nextDueMs() const override { return std::nullopt; }
  PageScene scene(int, bool, const TextMetrics&, int) const override { return {}; }

  void analyzed() {
    for (const char* w : {"彼", "本", "読む"}) {
      CardWord cw;
      cw.word = w;
      words.push_back(cw);
    }
    levels = {Level::None, Level::Fresh, Level::None};
    phases = {Phase::Analyzed, Phase::Analyzed, Phase::Analyzed};
    start = 2;
  }
};

}  // namespace

TEST(CardController, PhaseZeroHasNoWordUntilTheSourceAnswers) {
  LateSource source;
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  EXPECT_EQ(c.state().phase, Phase::Pending);
  EXPECT_EQ(c.state().pendingText, "読");
  EXPECT_EQ(c.highlightCodepoints(), 1);
  EXPECT_TRUE(c.currentWord().word.empty());
  EXPECT_FALSE(c.step(+1, 10));  // nothing to step through yet
  const Hit level = hit(Target::Level, 1);
  EXPECT_EQ(c.tap(&level, 10).effect, Effect::None);  // nothing to save yet
  EXPECT_FALSE(c.nextDueMs());
  EXPECT_FALSE(c.sourceChanged(0));

  source.analyzed();  // the lookup answered: the card opens on the tapped word (start 2)
  EXPECT_TRUE(c.sourceChanged(0));
  EXPECT_EQ(c.state().phase, Phase::Analyzed);
  EXPECT_EQ(c.highlightCodepoints(), 0);
  // The word index the card opened on stays the source's start, set when the sentence arrived.
  source.phases[2] = Phase::Complete;
  EXPECT_TRUE(c.sourceChanged(0));
  EXPECT_EQ(c.state().phase, Phase::Complete);
  EXPECT_FALSE(c.sourceChanged(0));  // nothing new
}

TEST(CardController, StepsFocusTheSourceAndLevelsComeFromIt) {
  LateSource source;
  source.analyzed();
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  EXPECT_EQ(c.word(), 2);
  EXPECT_EQ(c.currentWord().word, "読む");
  EXPECT_FALSE(c.step(+1, 10));  // the end
  EXPECT_TRUE(c.step(-1, 10));
  EXPECT_EQ(source.focused, std::vector<int>{1});
  EXPECT_EQ(c.state().level, Level::Fresh);  // 本 was saved as fresh
}

// popup-ui.md §3.2: swipes on the card and a long-press on another word (P7).
namespace {
// The card view's card is the bottom half, the detail view's everything below y 80. Each input is followed
// by the frame it asked for (`redraw`), as render() would put it on screen, unless a test wants the old one.
struct Swiping {
  BenchSource source{benchJapanese(), false};
  CardController c{source, ReadingMode::Kana};
  ShownTargets shown;
  unsigned long now = config::kBenchPhaseBMs;
  Swiping() {
    c.open(0);
    c.tick(now);
    draw();
  }
  void draw() {
    const Rect card = c.state().view == View::Card ? Rect{0, 400, 480, 400} : Rect{0, 80, 480, 720};
    shown.drawing({at(Target::Card, card)}, c.steps(), c.state().view);
    shown.shown(now);
  }
  Outcome run(PendingInput& in, const bool redraw) {
    const Outcome o = handleInput(c, shown, in, now + 20);
    now += 100;
    if (redraw) draw();
    return o;
  }
  Outcome swipe(const Swipe direction, const int x = 240, const int y = 600, const bool redraw = true) {
    PendingInput in;
    in.swipe(direction, x, y, now + 10);
    return run(in, redraw);
  }
  Outcome longPress(const int x, const int y, const bool redraw = true) {
    PendingInput in;
    in.longPress(x, y, now + 10);
    return run(in, redraw);
  }
};
}  // namespace

TEST(CardSwipe, UpOpensTheDetailViewAndDownGoesBackThenCloses) {
  Swiping s;
  EXPECT_EQ(s.swipe(Swipe::Up).effect, Effect::Redraw);
  EXPECT_EQ(s.c.state().view, View::Expanded);
  EXPECT_EQ(s.swipe(Swipe::Up).effect, Effect::None);  // already there
  EXPECT_EQ(s.swipe(Swipe::Down).effect, Effect::Redraw);
  EXPECT_EQ(s.c.state().view, View::Card);
  EXPECT_EQ(s.swipe(Swipe::Down).effect, Effect::Close);
}

TEST(CardSwipe, LeftAndRightChangeTabsInTheDetailViewOnly) {
  Swiping s;
  EXPECT_EQ(s.swipe(Swipe::Left).effect, Effect::None);  // the card view has no tabs
  EXPECT_EQ(s.c.state().tab, 0);
  s.swipe(Swipe::Up);
  EXPECT_EQ(s.swipe(Swipe::Right).effect, Effect::None);  // the first tab: stops
  const int last = tabCount(s.c.currentWord().language) - 1;
  for (int tab = 1; tab <= last; tab++) {
    EXPECT_EQ(s.swipe(Swipe::Left).effect, Effect::Redraw);
    EXPECT_EQ(s.c.state().tab, tab);
  }
  EXPECT_EQ(s.swipe(Swipe::Left).effect, Effect::None);  // the ⋯ tab: stops
  EXPECT_EQ(s.c.state().tab, last);
  EXPECT_EQ(s.swipe(Swipe::Right).effect, Effect::Redraw);
  EXPECT_EQ(s.c.state().tab, last - 1);
}

TEST(CardSwipe, ASwipeThatStartsOffTheCardIsDropped) {
  Swiping s;
  EXPECT_EQ(s.swipe(Swipe::Down, 240, 200).effect, Effect::None);  // started on the page
  EXPECT_EQ(s.swipe(Swipe::Up, 240, 200).effect, Effect::None);
  EXPECT_EQ(s.c.state().view, View::Card);
}

TEST(CardSwipe, OnlySwipesClearOfTheEdgeGesturesCount) {
  const int w = 480;
  const int h = 800;
  const int m = config::kCardSwipeEdgeMarginPx;
  const int side = static_cast<int>(w * freeink::ui::EDGE_SWIPE_SIDE_FRAC);        // 120
  const int band = static_cast<int>(h * freeink::ui::EDGE_SWIPE_TOP_BOTTOM_FRAC);  // 112
  EXPECT_TRUE(swipeClearOfEdges(240, 400, 240, 200, w, h));
  EXPECT_TRUE(swipeClearOfEdges(479, 400, 200, 400, w, h));       // the right edge is free
  EXPECT_FALSE(swipeClearOfEdges(m - 1, 400, m - 1, 200, w, h));  // ~10 mm from the left, whichever way
  EXPECT_FALSE(swipeClearOfEdges(240, m - 1, 100, m - 1, w, h));  // ... and the top
  EXPECT_FALSE(swipeClearOfEdges(240, h - m, 100, h - m, w, h));  // ... and the bottom
  // The SDK's own bands, wider than the margin: those swipes are CrossPoint's (Back, the frontlight panel,
  // the reader menu or Home), never the card's.
  EXPECT_FALSE(swipeClearOfEdges(side, 400, side + 200, 400, w, h));
  EXPECT_TRUE(swipeClearOfEdges(side + 1, 400, side + 200, 400, w, h));
  EXPECT_TRUE(swipeClearOfEdges(side, 400, side, 200, w, h));  // up, not away from the left edge
  EXPECT_FALSE(swipeClearOfEdges(240, band, 240, band + 200, w, h));
  EXPECT_TRUE(swipeClearOfEdges(240, band, 440, band, w, h));  // across, not down from the top
  EXPECT_FALSE(swipeClearOfEdges(240, h - band, 240, h - band - 200, w, h));
  EXPECT_TRUE(swipeClearOfEdges(240, h - band - 1, 240, h - band - 201, w, h));
}

TEST(CardSwipe, DirectionFollowsTheDominantAxis) {
  EXPECT_EQ(swipeBetween(240, 600, 250, 300), Swipe::Up);
  EXPECT_EQ(swipeBetween(240, 300, 230, 600), Swipe::Down);
  EXPECT_EQ(swipeBetween(400, 400, 100, 390), Swipe::Left);
  EXPECT_EQ(swipeBetween(100, 400, 400, 410), Swipe::Right);
  EXPECT_EQ(swipeBetween(240, 400, 240, 400), Swipe::Right);  // the SDK's tie goes to the horizontal axis
}

TEST(CardLongPress, OnThePageClosesAndLooksUpThere) {
  Swiping s;
  const Outcome o = s.longPress(100, 200);
  EXPECT_EQ(o.effect, Effect::Close);
  ASSERT_TRUE(o.lookUpAt.has_value());
  EXPECT_EQ(o.lookUpAt->x, 100);
  EXPECT_EQ(o.lookUpAt->y, 200);
}

TEST(CardLongPress, OnTheCardOrOverTheDetailViewDoesNothing) {
  Swiping s;
  EXPECT_EQ(s.longPress(100, 600).effect, Effect::None);  // on the card
  s.swipe(Swipe::Up);
  const Outcome o = s.longPress(100, 200);  // the detail view covers the page
  EXPECT_EQ(o.effect, Effect::None);
  EXPECT_FALSE(o.lookUpAt.has_value());
}

TEST(CardLongPress, ATapOnThePageDoesWhatALongPressDoes) {
  // P10: a tap outside the card closes it to look up the word there (word select goes back to the reader
  // when there's none), as a long-press always did; a tap on the card doesn't.
  Swiping s;
  PendingInput in;
  in.tap(100, 200, s.now + 10);
  const Outcome o = s.run(in, true);
  EXPECT_EQ(o.effect, Effect::Close);
  ASSERT_TRUE(o.lookUpAt.has_value());
  EXPECT_EQ(o.lookUpAt->x, 100);
  EXPECT_EQ(o.lookUpAt->y, 200);
  PendingInput onCard;
  onCard.tap(100, 600, s.now + 10);
  EXPECT_NE(s.run(onCard, true).effect, Effect::Close);
}

TEST(CardSwipe, ASwipeOrLongPressOnTheOldCardDuringAStepIsDropped) {
  Swiping s;
  s.c.step(+1, s.now + 1);  // the frame on screen still shows the previous word
  s.swipe(Swipe::Up, 240, 600, /*redraw=*/false);
  EXPECT_EQ(s.c.state().view, View::Card);
  const Outcome o = s.longPress(100, 200, /*redraw=*/false);
  EXPECT_NE(o.effect, Effect::Close);
  EXPECT_FALSE(o.lookUpAt.has_value());
}

TEST(CardSwipe, ATouchOnTheOtherViewsFrameIsDropped) {
  Swiping s;
  s.swipe(Swipe::Up, 240, 600, /*redraw=*/false);  // the detail view, its frame not drawn yet
  ASSERT_EQ(s.c.state().view, View::Expanded);
  const Outcome o = s.longPress(100, 200, /*redraw=*/false);  // off the card on the frame seen; the detail view now
  EXPECT_NE(o.effect, Effect::Close);
  EXPECT_FALSE(o.lookUpAt.has_value());
  s.swipe(Swipe::Left, 240, 600, /*redraw=*/false);  // no tab change from a frame that showed no tabs
  EXPECT_EQ(s.c.state().tab, 0);
}

TEST(CardLongPress, ReplacesOnlyALiveCardOverWordSelectsPage) {
  EXPECT_TRUE(pagePressLooksUp(true, true));
  EXPECT_FALSE(pagePressLooksUp(false, true));  // the bench: no word select under it
  EXPECT_FALSE(pagePressLooksUp(true, false));  // a landscape book: its page isn't drawn under the card
  const std::optional<PagePoint> at = PagePoint{120, 300};
  EXPECT_TRUE(lookUpOnClose(at, true, true).has_value());
  EXPECT_FALSE(lookUpOnClose(at, false, true).has_value());  // the bench: a tap just closes
  EXPECT_FALSE(lookUpOnClose(at, true, false).has_value());  // landscape: a tap just closes
  EXPECT_FALSE(lookUpOnClose(std::nullopt, true, true).has_value());
}

TEST(CardController, ATapOnThePageClosesToLookUpTheWordThere) {
  // P10 (claritise, 2026-09-25): "changing words when the dictionary is open should be tap instead of hold".
  BenchSource source(benchJapanese(), false);
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  const Outcome o = c.tap(nullptr, 0, PagePoint{120, 300});
  EXPECT_EQ(o.effect, Effect::Close);
  ASSERT_TRUE(o.lookUpAt.has_value());  // word select looks up the word there, or goes back if there's none
  EXPECT_EQ(o.lookUpAt->x, 120);
  EXPECT_EQ(o.lookUpAt->y, 300);
  EXPECT_FALSE(c.tap(nullptr, 0).lookUpAt.has_value());  // no point given: a plain close
  const Hit rank = hit(Target::RankRow);
  c.tap(&rank, 0);
  ASSERT_EQ(c.state().view, View::Expanded);
  EXPECT_EQ(c.tap(nullptr, 0, PagePoint{120, 300}).effect, Effect::None);  // the detail view covers the page
}

TEST(CardSwipe, NothingOpensOrChangesTabBeforeTheWordArrives) {
  BenchSource source(benchJapanese(), false);
  CardController c(source, ReadingMode::Kana);
  c.open(0);  // phase 0: the tapped character only
  ASSERT_EQ(c.state().phase, Phase::Pending);
  EXPECT_EQ(c.swipe(Swipe::Up).effect, Effect::None);
  EXPECT_EQ(c.state().view, View::Card);
  EXPECT_EQ(c.swipe(Swipe::Left).effect, Effect::None);
  EXPECT_EQ(c.swipe(Swipe::Down).effect, Effect::Close);  // closing still works
}

TEST(CardInput, ALongPressIsNeverDroppedFromAFullQueue) {
  PendingInput in;
  for (int i = 0; i < config::kCardPendingInputMax; i++) in.tap(0, 0, static_cast<unsigned long>(i));
  in.longPress(5, 6, 100);
  ASSERT_EQ(in.size(), static_cast<size_t>(config::kCardPendingInputMax));
  EXPECT_EQ(in[in.size() - 1].kind, InputEvent::Kind::LongPress);
  EXPECT_EQ(in[in.size() - 1].x, 5);
}

TEST(CardController, ATabPastTheLanguagesLastIsBroughtBack) {
  BenchSource source(benchChinese(), false);
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  c.tick(config::kBenchPhaseBMs);
  const Hit rank = hit(Target::RankRow);
  const Hit far = hit(Target::Tab, 9);
  c.tap(&rank, 0);
  c.tap(&far, 0);
  c.sourceChanged(0);
  EXPECT_EQ(c.state().tab, tabCount(c.currentWord().language) - 1);
}

TEST(CardInput, ALongPressNeverTakesAHomesPlace) {
  PendingInput in;
  for (int i = 0; i < config::kCardPendingInputMax - 1; i++) in.tap(0, 0, static_cast<unsigned long>(i));
  in.home(50);
  in.longPress(5, 6, 60);  // full: the way out stays
  ASSERT_EQ(in.size(), static_cast<size_t>(config::kCardPendingInputMax));
  EXPECT_EQ(in[in.size() - 1].kind, InputEvent::Kind::Home);
}

// P9: the bench goes on into a canned "next sentence" (its own again), so card-sentence can drive it.
TEST(CardController, TheBenchGoesOnIntoItsNextSentence) {
  BenchSource source(benchJapanese(), false);
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  c.tick(config::kBenchPhaseBMs);
  const int n = source.wordCount();
  while (c.step(+1, 5000)) {
  }
  ASSERT_EQ(c.word(), n - 1);
  EXPECT_TRUE(c.awaitingNext());
  // The loop wakes for whichever comes first: the last word's own phases, then the "next sentence".
  ASSERT_TRUE(c.nextDueMs().has_value());
  EXPECT_LT(*c.nextDueMs(), 5000 + config::kBenchNextSentenceMs);
  c.tick(5000 + config::kBenchPhaseBMs);  // its phases done
  EXPECT_EQ(c.nextDueMs(), 5000 + config::kBenchNextSentenceMs);
  c.tick(5000 + config::kBenchNextSentenceMs - 1);  // the last word's own phases still play meanwhile
  EXPECT_EQ(c.word(), n - 1);
  EXPECT_EQ(c.state().phase, Phase::Complete);
  EXPECT_TRUE(c.tick(5000 + config::kBenchNextSentenceMs));
  EXPECT_EQ(c.word(), n);  // its first word
  EXPECT_EQ(c.currentWord().word, benchJapanese().words[0].word);
  while (c.step(+1, 9000)) {
  }
  EXPECT_EQ(c.word(), 2 * n - 1);  // then the page ends
  EXPECT_FALSE(c.awaitingNext());
}

// V9a A3 (claritise 2026-09-29, "Marked words by default"): with the card open on an analyzed page, the side buttons
// step only between the marked words (not saved or level 0, tracked, learning; never an ignored or suspended word),
// across the page's sentences; past the last marked word a press stops.
namespace {

class MarkedSource final : public CardSource {
 public:
  // The page's sentences, each a list of (word, level, never marked).
  struct W {
    const char* word;
    Level level;
    bool never = false;
  };
  std::vector<std::vector<W>> sentences;
  size_t loaded = 1;  // sentences whose words are there
  bool marks = true;
  bool loading = false;
  std::vector<CardWord> cards;
  std::vector<W> flat;
  int start = 0;

  void load() {
    flat.clear();
    for (size_t s = 0; s < loaded; s++) flat.insert(flat.end(), sentences[s].begin(), sentences[s].end());
    cards.clear();
    for (const W& w : flat) {
      CardWord cw;
      cw.word = w.word;
      cards.push_back(cw);
    }
  }
  int wordCount() const override { return static_cast<int>(flat.size()); }
  int startWord() const override { return start; }
  const CardWord& word(const int i) const override { return cards[i]; }
  Level savedLevel(const int i) const override { return flat[i].level; }
  Phase phase(int) const override { return Phase::Complete; }
  std::string pendingText() const override { return ""; }
  int pageNumber() const override { return 0; }
  void open(unsigned long) override {}
  void focus(int, unsigned long) override {}
  bool extend(unsigned long) override {
    if (loaded >= sentences.size()) return false;
    loading = true;
    return true;
  }
  bool extending() const override { return loading; }
  bool tick(unsigned long) override {
    if (!loading) return false;
    loading = false;
    loaded++;
    load();
    return true;
  }
  std::optional<unsigned long> nextDueMs() const override { return std::nullopt; }
  PageScene scene(int, bool, const TextMetrics&, int) const override { return {}; }
  bool stepsMarkedWords() const override { return marks; }
  bool neverMarked(const int i) const override { return flat[i].never; }
};

using W = MarkedSource::W;

MarkedSource page() {
  MarkedSource s;
  s.sentences = {{{"祖父", Level::Learning},
                  {"は", Level::Known},
                  {"毎朝", Level::Fresh},
                  {"海", Level::None, true},
                  {"窓辺", Level::None}},
                 {{"雲", Level::Known}, {"の", Level::Known}},  // nothing marked
                 {{"動き", Level::Tracked}, {"を", Level::Known}}};
  s.load();
  return s;
}

}  // namespace

TEST(CardControllerA3, SideButtonsStepBetweenMarkedWords) {
  MarkedSource s = page();
  CardController c(s, ReadingMode::Kana);
  c.open(0);
  EXPECT_EQ(c.word(), 0);  // 祖父
  EXPECT_TRUE(c.step(+1, 10));
  EXPECT_EQ(c.word(), 4);  // 窓辺: は, 毎朝 (known) and 海 (ignored) skipped
  EXPECT_TRUE(c.step(-1, 20));
  EXPECT_EQ(c.word(), 0);
  EXPECT_FALSE(c.step(-1, 30));  // the tapped sentence's start
}

TEST(CardControllerA3, OnIntoTheNextSentencesFirstMarkedWordOverOneWithNone) {
  MarkedSource s = page();
  CardController c(s, ReadingMode::Kana);
  c.open(0);
  ASSERT_TRUE(c.step(+1, 10));
  ASSERT_EQ(c.word(), 4);
  EXPECT_FALSE(c.step(+1, 20));  // the sentence's end: the next one loads
  EXPECT_TRUE(c.awaitingNext());
  c.tick(30);  // it came with nothing marked: on into the one after it
  EXPECT_TRUE(c.awaitingNext());
  EXPECT_EQ(c.word(), 4);
  c.tick(40);
  EXPECT_EQ(c.word(), 7);        // 動き
  EXPECT_FALSE(c.step(+1, 50));  // the page's last marked word: a press stops
  EXPECT_FALSE(c.awaitingNext());
  EXPECT_EQ(c.word(), 7);
}

TEST(CardControllerA3, ALevelSetOnTheCardDecidesToo) {
  MarkedSource s = page();
  CardController c(s, ReadingMode::Kana);
  c.open(0);
  ASSERT_TRUE(c.step(+1, 10));  // 窓辺
  const Hit known = hit(Target::Level, 3);
  c.tap(&known, 20);            // saved as known on this card: no mark any more
  ASSERT_TRUE(c.step(-1, 30));  // back to 祖父
  EXPECT_EQ(c.word(), 0);
  EXPECT_FALSE(c.step(+1, 40));  // 窓辺 is skipped now: on into the next sentences
}

TEST(CardControllerA3, EveryWordWithoutMarks) {
  MarkedSource s = page();
  s.marks = false;  // "Every word", or a page not analyzed
  CardController c(s, ReadingMode::Kana);
  c.open(0);
  ASSERT_TRUE(c.step(+1, 10));
  EXPECT_EQ(c.word(), 1);  // は
}

// A press made while the card jumped on into the next sentence (seen within kStepAfterJumpGraceMs of the jump)
// goes back as it would have from the word the card waited on: to the previous marked word before it, or to that word
// itself when there's none.
TEST(CardControllerA3, ABackPressDuringTheJumpGoesToThePreviousMarkedWord) {
  MarkedSource s = page();
  CardController c(s, ReadingMode::Kana);
  c.open(0);
  ASSERT_TRUE(c.step(+1, 10));  // 窓辺
  ASSERT_EQ(c.word(), 4);
  c.step(+1, 20);  // on into the next sentences
  c.tick(30);
  c.tick(40);
  ASSERT_EQ(c.word(), 7);           // 動き, jumped to at 40
  EXPECT_TRUE(c.step(-1, 45, 41));  // pressed at 41, during the jump
  EXPECT_EQ(c.word(), 0);           // 祖父: 海 (ignored), 毎朝 and は (known) skipped, as from 窓辺
}

TEST(CardControllerA3, ABackPressDuringTheJumpWithNoMarkBeforeStaysOnTheWordItLeft) {
  MarkedSource s = page();
  s.sentences[0][0].level = Level::Known;  // nothing marked before 窓辺
  s.load();
  s.start = 4;
  CardController c(s, ReadingMode::Kana);
  c.open(0);
  ASSERT_EQ(c.word(), 4);
  c.step(+1, 20);
  c.tick(30);
  c.tick(40);
  ASSERT_EQ(c.word(), 7);
  EXPECT_TRUE(c.step(-1, 45, 41));
  EXPECT_EQ(c.word(), 4);  // 窓辺, the word it waited on
}
