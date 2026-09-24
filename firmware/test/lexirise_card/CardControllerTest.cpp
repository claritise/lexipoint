// popup-ui.md §2-3: the card's phases, stepping, taps and Home, with time passed in.

#include <gtest/gtest.h>

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

TEST(CardController, SideButtonsStepWordsAndStopAtTheEnds) {
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
  EXPECT_FALSE(c.step(+1, 6000));  // the last word
  EXPECT_EQ(c.word(), 5);
  for (int i = 0; i < 5; i++) c.step(-1, 7000);
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
  shown.drawing({at(Target::Level, {0, 0, 50, 50}, 1), at(Target::Card, {0, 0, 480, 400})}, c.steps());
  shown.shown(100);
  shown.drawing({at(Target::Card, {0, 0, 480, 400})}, c.steps());  // phase B moved T L F K away
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
  shown.drawing({at(Target::Close, {0, 0, 50, 50})}, 1);  // the next word's card (one step on), up at 25
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
  shown.drawing({at(Target::ReadingLine, {0, 0, 50, 50})}, c.steps());
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
  shown.drawing({at(Target::Level, {0, 0, 50, 50}, 3)}, c.steps());  // word A's card, K at the top left
  shown.shown(60000);
  PendingInput in;
  in.step(+1, 60100);     // → word B; its refresh starts
  in.tap(10, 10, 60200);  // K, still on A's card
  EXPECT_NE(handleInput(c, shown, in, 60300).effect, Effect::Close);
  EXPECT_EQ(c.word(), a + 1);
  EXPECT_EQ(c.state().level, benchJapanese().saved[a + 1]);  // B: unchanged
  EXPECT_TRUE(c.state().toast.empty());
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
  ShownTargets shown;
  shown.drawing({at(Target::RankRow, {0, 0, 50, 50})}, c.steps());
  shown.shown(10);
  const Hit rank = hit(Target::RankRow);
  c.tap(&rank, 60000);  // the expanded view
  ASSERT_EQ(c.state().view, View::Expanded);
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
  EXPECT_FALSE(c.sourceChanged());

  source.analyzed();  // the lookup answered: the card opens on the tapped word (start 2)
  EXPECT_TRUE(c.sourceChanged());
  EXPECT_EQ(c.state().phase, Phase::Analyzed);
  EXPECT_EQ(c.highlightCodepoints(), 0);
  // The word index the card opened on stays the source's start, set when the sentence arrived.
  source.phases[2] = Phase::Complete;
  EXPECT_TRUE(c.sourceChanged());
  EXPECT_EQ(c.state().phase, Phase::Complete);
  EXPECT_FALSE(c.sourceChanged());  // nothing new
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
