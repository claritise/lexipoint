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

TEST(LongPress, TakenOnlyWhenFiredOwnedAndAvailable) {
  using lexipoint::lookup::takeLongPress;
  int asked = 0;
  const auto available = [&asked](const bool answer) {
    return [&asked, answer] {
      asked++;
      return answer;
    };
  };
  const LongPressRules on = rules(true, true);
  EXPECT_FALSE(takeLongPress(std::nullopt, on, available(true)));  // no long-press this frame
  EXPECT_FALSE(takeLongPress(150, on, available(true)));           // CrossPoint's zone (touch-down point)
  EXPECT_EQ(asked, 0);  // the settings aren't read for a frame the lookup can't take
  EXPECT_TRUE(takeLongPress(165, on, available(true)));
  EXPECT_FALSE(takeLongPress(165, on, available(false)));  // nothing can answer: stays CrossPoint's
  EXPECT_EQ(asked, 2);
  EXPECT_TRUE(takeLongPress(10, rules(false, true), available(true)));
}
