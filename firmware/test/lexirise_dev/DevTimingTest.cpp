// The keep-awake lease and the synthetic button press (src/lexirise/dev/DevTiming.h).

#include <gtest/gtest.h>

#include <climits>

#include "DevTiming.h"

using namespace lexipoint::dev;

namespace {

constexpr unsigned long kLease = config::kKeepAwakeLeaseMs;

TEST(KeepAwake, LeaseNeedsAHostAndARecentCommand) {
  KeepAwake k;
  k.renew(1000);
  EXPECT_TRUE(k.active(1000, true));
  EXPECT_TRUE(k.active(1000 + kLease - 1, true));
  EXPECT_FALSE(k.active(1000 + kLease, true));  // expired
  EXPECT_FALSE(k.active(1001, false));          // battery or charger: never kept awake by the lease
  k.renew(1000 + kLease);
  EXPECT_TRUE(k.active(1000 + kLease + 5, true));
}

TEST(KeepAwake, ForcedModes) {
  KeepAwake k;
  k.setMode(AwakeMode::On);
  EXPECT_TRUE(k.active(123456789, false));
  k.setMode(AwakeMode::Off);
  k.renew(0);
  EXPECT_FALSE(k.active(0, true));
  k.setMode(AwakeMode::Lease);
  EXPECT_TRUE(k.active(1, true));
}

TEST(KeepAwake, SurvivesMillisWrap) {
  KeepAwake k;
  const unsigned long nearWrap = ULONG_MAX - 10;
  k.renew(nearWrap);
  EXPECT_TRUE(k.active(nearWrap + 20, true));  // wrapped past zero
  EXPECT_FALSE(k.active(nearWrap + kLease + 1, true));
}

TEST(ButtonPress, HeldForDeadlineAndMinimumUpdates) {
  ButtonPress b;
  ASSERT_TRUE(b.start(5, 1000, 150));
  const uint8_t mask = 1u << 5;
  EXPECT_EQ(b.tick(1000), mask);
  EXPECT_EQ(b.tick(1100), mask);  // before the deadline
  // Deadline passed but only 3 samples needed in total: this is the 3rd, so the press ends now.
  EXPECT_EQ(b.tick(1200), 0);
  EXPECT_FALSE(b.busy());
}

TEST(ButtonPress, SlowLoopStillGetsMinimumSamples) {
  ButtonPress b;
  ASSERT_TRUE(b.start(4, 0, 120));
  const uint8_t mask = 1u << 4;
  EXPECT_EQ(b.tick(5000), mask);  // deadline long gone, 1st sample
  EXPECT_EQ(b.tick(5050), mask);  // 2nd sample
  EXPECT_EQ(b.tick(5100), 0);     // 3rd sample: released
}

TEST(ButtonPress, BusyWhileHeld) {
  ButtonPress b;
  ASSERT_TRUE(b.start(4, 0, 1000));
  EXPECT_TRUE(b.busy());
  EXPECT_FALSE(b.start(5, 10, 100));  // no button-to-button switch without a release
  EXPECT_EQ(b.tick(20), 1u << 4);
}

TEST(ButtonPress, IdleReturnsZero) {
  ButtonPress b;
  EXPECT_EQ(b.tick(0), 0);
  EXPECT_FALSE(b.busy());
}

}  // namespace
