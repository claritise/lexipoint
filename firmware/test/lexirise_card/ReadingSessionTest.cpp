// C1, C7 (V6): the reading session's counts and the home screen's summary (docs/v0.2/00-overview.md "V6 design" 1).

#include <gtest/gtest.h>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/session/ReadingSession.h"

using namespace lexipoint::session;
using lexipoint::Language;

namespace {

// A session with its book open.
ReadingSession opened() {
  ReadingSession s;
  s.bookOpened();
  return s;
}

// The book closed to the home screen: its summary.
std::optional<Summary> closedToHome(ReadingSession& s) {
  s.homeNext();
  s.bookClosed();
  return s.takeSummary();
}

}  // namespace

TEST(ReadingSession, CountsSavesKeptAndCardsLookedUp) {
  ReadingSession s = opened();
  s.lookedUp(Language::Japanese);
  s.lookedUp(Language::Japanese);
  s.saved(Language::Japanese, "901", true);
  s.saved(Language::Japanese, "902", false);  // a sentence card is a save too
  s.saved(Language::Japanese, "903", true);
  s.removed("903");  // its Undo takes it back
  s.removed("555");  // not this session's save: nothing
  const auto summary = closedToHome(s);
  ASSERT_TRUE(summary);
  EXPECT_EQ(summary->saved, 2u);
  EXPECT_EQ(summary->lookedUp, 2u);
  EXPECT_EQ(summary->language, Language::Japanese);
  EXPECT_FALSE(summary->words);  // the count never came
  EXPECT_EQ(countsLine(*summary), "2 saved \xC2\xB7 2 looked up");
  EXPECT_EQ(wordsLine(*summary), "");  // the second line left out
}

TEST(ReadingSession, TheSummaryOnlyWhenItLookedSomethingUpAndHomeComesNext) {
  ReadingSession nothing = opened();
  EXPECT_FALSE(closedToHome(nothing));  // no card opened: none
  ReadingSession elsewhere = opened();
  elsewhere.lookedUp(Language::Japanese);
  elsewhere.bookClosed();  // to another screen (the sleep screen, another book, the file browser)
  EXPECT_FALSE(elsewhere.takeSummary());
  ReadingSession once = opened();
  once.lookedUp(Language::Chinese);
  EXPECT_TRUE(closedToHome(once));
  EXPECT_FALSE(once.takeSummary());  // drawn once: taken
  EXPECT_FALSE(once.active());
}

TEST(ReadingSession, ANewBookStartsAgainAndSleepLeavesNone) {
  ReadingSession s = opened();
  s.lookedUp(Language::Japanese);
  s.saved(Language::Japanese, "1", true);
  s.bookOpened();  // another book: counts start again
  s.lookedUp(Language::Japanese);
  const auto summary = closedToHome(s);
  ASSERT_TRUE(summary);
  EXPECT_EQ(summary->saved, 0u);
  EXPECT_EQ(summary->lookedUp, 1u);
  // Going to sleep, the reader closes with no home screen next: the session ends with no summary.
  ReadingSession sleeping = opened();
  sleeping.lookedUp(Language::Japanese);
  sleeping.bookClosed();
  EXPECT_FALSE(sleeping.takeSummary());
  sleeping.homeNext();  // the next boot's home screen finds nothing either
  EXPECT_FALSE(sleeping.takeSummary());
}

TEST(ReadingSession, NothingCountsOutsideABook) {
  ReadingSession s;
  s.lookedUp(Language::Japanese);
  s.saved(Language::Japanese, "1", true);
  EXPECT_FALSE(s.countWanted());
  EXPECT_FALSE(closedToHome(s));
}

TEST(ReadingSession, TheCountIsWantedOnceThenCountedLocally) {
  ReadingSession s = opened();
  EXPECT_FALSE(s.countWanted());           // no language until a card opens
  s.saved(Language::Japanese, "1", true);  // before any card? (not possible on the device) not counted locally
  s.lookedUp(Language::Japanese);
  ASSERT_EQ(s.countWanted(), Language::Japanese);
  s.saved(Language::Japanese, "2", true);  // before the count came: the count includes it
  s.countFetched(Language::Chinese, 5);    // another language's: not this session's
  EXPECT_TRUE(s.countWanted());
  s.countFetched(Language::Japanese, 1203);
  EXPECT_FALSE(s.countWanted());            // one per session
  s.countFetched(Language::Japanese, 9);    // a second answer changes nothing
  s.saved(Language::Japanese, "3", true);   // +1
  s.saved(Language::Japanese, "4", false);  // a sentence card: not a word (totalCount counts words only)
  s.saved(Language::Japanese, "5", true);   // +1
  s.removed("5");                           // its Undo: -1
  s.removed("2");                           // a word saved before the count came, taken back: -1
  const auto summary = closedToHome(s);
  ASSERT_TRUE(summary);
  EXPECT_EQ(summary->words, 1203u + 1 + 1 - 1 - 1);
  EXPECT_EQ(wordsLine(*summary), "1,203 words in Japanese");
}

TEST(ReadingSession, TheLinesNumbersAndLanguages) {
  Summary s;
  s.saved = 12;
  s.lookedUp = 1140;
  s.language = lexipoint::Language::Chinese;
  s.words = 1;
  EXPECT_EQ(countsLine(s), "12 saved \xC2\xB7 1,140 looked up");
  EXPECT_EQ(wordsLine(s), "1 word in Chinese");
  s.words = 1204;
  EXPECT_EQ(wordsLine(s), "1,204 words in Chinese");
  s.language = Language::Japanese;
  s.words = 0;
  EXPECT_EQ(wordsLine(s), "0 words in Japanese");
  EXPECT_EQ(groupedNumber(1234567), "1,234,567");
  EXPECT_EQ(groupedNumber(999), "999");
}

TEST(ReadingSession, ASentenceSavedThisSessionIsKnownByItsText) {
  ReadingSession s = opened();
  s.lookedUp(Language::Japanese);
  s.saved(Language::Japanese, "88", false);
  s.sentenceSaved(Language::Japanese, "彼は歩いた。", "88");
  EXPECT_EQ(s.sentenceId(Language::Japanese, "彼は歩いた。"), "88");
  EXPECT_FALSE(s.sentenceId(Language::Chinese, "彼は歩いた。"));
  EXPECT_FALSE(s.sentenceId(Language::Japanese, "彼は歩いた"));
  s.removed("88");  // taken back (a sentence card's DELETE removes it): a new save is a POST again
  EXPECT_FALSE(s.sentenceId(Language::Japanese, "彼は歩いた。"));
}

TEST(ReadingSession, PastItsCapASaveIsCountedButItsUndoIsnt) {
  ReadingSession s = opened();
  s.lookedUp(Language::Japanese);
  for (size_t i = 0; i < lexipoint::config::kSessionSavesMax + 1; i++) {
    s.saved(Language::Japanese, std::to_string(i), true);
  }
  s.removed(std::to_string(lexipoint::config::kSessionSavesMax));  // not remembered
  s.removed("0");
  EXPECT_EQ(closedToHome(s)->saved, lexipoint::config::kSessionSavesMax);
}

TEST(ReadingSession, TheFirstCardsLanguageIsTheSessions) {
  ReadingSession s = opened();
  s.lookedUp(Language::Japanese);
  s.lookedUp(Language::Chinese);
  EXPECT_EQ(s.countWanted(), Language::Japanese);
  EXPECT_EQ(closedToHome(s)->language, Language::Japanese);
}

TEST(ReadingSession, OnlyTheSessionLanguagesWordsMoveTheCount) {
  ReadingSession s = opened();
  s.lookedUp(Language::Japanese);
  s.countFetched(Language::Japanese, 100);
  s.saved(Language::Japanese, "501", false);  // a sentence card isn't a word
  s.removed("501");
  s.saved(Language::Chinese, "9", true);  // another language's word
  const auto summary = closedToHome(s);
  EXPECT_EQ(summary->words, 100u);
  EXPECT_EQ(summary->saved, 1u);
}

TEST(ReadingSession, AnotherLanguagesWordTakenBackDoesntMoveTheCount) {
  ReadingSession s = opened();
  s.lookedUp(Language::Japanese);
  s.countFetched(Language::Japanese, 100);
  s.saved(Language::Chinese, "9", true);
  s.removed("9");
  EXPECT_EQ(closedToHome(s)->words, 100u);
}
