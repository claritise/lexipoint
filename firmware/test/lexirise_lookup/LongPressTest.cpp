// lookup-flow.md §1 (D15): who a long-press on the page belongs to.

#include <gtest/gtest.h>

#include "lexirise/lookup/LongPress.h"

using lexipoint::lookup::LongPressRules;
using lexipoint::lookup::lookupOwnsLongPress;
using lexipoint::lookup::lookupsAvailable;

namespace {

LongPressRules rules(const bool holdActionOn, const bool tapZones) { return {480, 160, holdActionOn, tapZones}; }

}  // namespace

TEST(LongPress, TheWholePageWhenCrossPointHasNoHoldAction) {
  for (const int x : {0, 100, 240, 479}) {
    EXPECT_TRUE(lookupOwnsLongPress(x, rules(false, true))) << x;  // hold action off
    EXPECT_TRUE(lookupOwnsLongPress(x, rules(true, false))) << x;  // swipe mode, or touch controls off
  }
}

TEST(LongPress, OnlyTheCentreWhenTheHoldActionIsOn) {
  const LongPressRules on = rules(true, true);
  EXPECT_FALSE(lookupOwnsLongPress(0, on));
  EXPECT_FALSE(lookupOwnsLongPress(159, on));
  EXPECT_TRUE(lookupOwnsLongPress(160, on));
  EXPECT_TRUE(lookupOwnsLongPress(319, on));
  EXPECT_FALSE(lookupOwnsLongPress(320, on));
  EXPECT_FALSE(lookupOwnsLongPress(479, on));
}

TEST(LongPress, OnlyTakenWhenSomethingCanAnswer) {
  EXPECT_FALSE(lookupsAvailable(false, false));  // the slow tap stays CrossPoint's
  EXPECT_TRUE(lookupsAvailable(true, false));
  EXPECT_TRUE(lookupsAvailable(false, true));
}

TEST(LongPress, UsedOnlyWhenFiredOwnedAndAvailable) {
  using lexipoint::lookup::longPressUse;
  using lexipoint::lookup::LongPressUse;
  int asked = 0;
  int measured = 0;
  const auto available = [&asked](const bool answer) {
    return [&asked, answer] {
      asked++;
      return answer;
    };
  };
  const auto onWord = [&measured](const bool answer) {
    return [&measured, answer] {
      measured++;
      return answer;
    };
  };
  const LongPressRules on = rules(true, true);
  EXPECT_EQ(longPressUse(std::nullopt, on, available(true), onWord(true)), LongPressUse::Leave);  // none this frame
  EXPECT_EQ(longPressUse(150, on, available(true), onWord(true)), LongPressUse::Leave);           // CrossPoint's zone
  EXPECT_EQ(asked, 0);  // the settings aren't read for a frame the lookup can't take
  EXPECT_EQ(longPressUse(165, on, available(true), onWord(true)), LongPressUse::LookUp);
  EXPECT_EQ(longPressUse(165, on, available(false), onWord(true)), LongPressUse::Leave);  // nothing can answer
  EXPECT_EQ(asked, 2);
  EXPECT_EQ(measured, 1);  // the page isn't loaded when nothing could answer
  EXPECT_EQ(longPressUse(10, rules(false, true), available(true), onWord(true)), LongPressUse::LookUp);
}

TEST(LongPress, APressOffTheTextDoesNothing) {
  // A margin, an image, blank space (claritise, 2026-09-25: "keep it doing nothing"): taken and dropped, so
  // its lift isn't a tap either (no menu, no page turn), and no word select.
  using lexipoint::lookup::longPressUse;
  using lexipoint::lookup::LongPressUse;
  const auto yes = [] { return true; };
  const auto no = [] { return false; };
  EXPECT_EQ(longPressUse(240, rules(false, true), yes, no), LongPressUse::Ignore);
  EXPECT_EQ(longPressUse(240, rules(true, true), yes, no), LongPressUse::Ignore);
  EXPECT_EQ(longPressUse(10, rules(true, true), yes, no), LongPressUse::Leave);   // CrossPoint's hold zone: its own
  EXPECT_EQ(longPressUse(240, rules(false, true), no, no), LongPressUse::Leave);  // nothing can answer: a slow tap
}

TEST(LongPress, ALookUpAndAnIgnoredPressAreConsumed) {
  using lexipoint::lookup::consumes;
  using lexipoint::lookup::LongPressUse;
  EXPECT_TRUE(consumes(LongPressUse::LookUp));
  EXPECT_TRUE(consumes(LongPressUse::Ignore));  // its lift must not turn the page or open the menu
  EXPECT_FALSE(consumes(LongPressUse::Leave));  // CrossPoint's: its lift is a slow tap
}

TEST(LongPress, LogNames) {
  using lexipoint::lookup::LongPressUse;
  using lexipoint::lookup::longPressUseName;
  EXPECT_STREQ(longPressUseName(LongPressUse::Leave), "left");
  EXPECT_STREQ(longPressUseName(LongPressUse::LookUp), "taken");
  EXPECT_STREQ(longPressUseName(LongPressUse::Ignore), "ignored");
}
