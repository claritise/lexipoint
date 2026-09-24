// Gesture frame sequences, the all-or-nothing queue, and logical → panel conversion.

#include <gtest/gtest.h>

#include "DevGesture.h"

using namespace lexipoint::dev;
using F = LogicalFrame;

namespace {

TEST(DevGesture, TapIsDownHeldThenTapRelease) {
  const Gesture g = makeTap(10, 20);
  ASSERT_EQ(g.count, 3u);
  EXPECT_TRUE(g.frames[0].has(F::Down));
  EXPECT_TRUE(g.frames[0].has(F::Held));
  EXPECT_EQ(g.frames[0].heldMs, 0u);
  EXPECT_TRUE(g.frames[1].has(F::Held));
  EXPECT_EQ(g.frames[1].heldMs, config::kTapHeldMs);
  EXPECT_TRUE(g.frames[2].has(F::Tap));
  EXPECT_TRUE(g.frames[2].has(F::Released));
  EXPECT_FALSE(g.frames[2].has(F::Held));
  for (size_t i = 0; i < g.count; i++) {
    EXPECT_EQ(g.frames[i].x, 10);
    EXPECT_EQ(g.frames[i].y, 20);
  }
}

TEST(DevGesture, TapHeldTimeClearsTouchDownSelectDelay) {
  // MappedInputManager only reports a touch-down once contact has lasted 90 ms.
  EXPECT_GT(config::kTapHeldMs, 90u);
  EXPECT_GT(config::kLongHeldMs, 700u);  // reader long-press threshold
}

TEST(DevGesture, LongPressFiresWhileHeldThenTapsOnLift) {
  // Like the SDK: an unconsumed long-press still taps on release (heldMs tells callers it was long).
  const Gesture g = makeLongPress(5, 6);
  ASSERT_EQ(g.count, 4u);
  EXPECT_TRUE(g.frames[2].has(F::LongPress));
  EXPECT_TRUE(g.frames[2].has(F::Held));
  EXPECT_FALSE(g.frames[2].has(F::Released));
  EXPECT_TRUE(g.frames[3].has(F::Released));
  EXPECT_TRUE(g.frames[3].has(F::Tap));
  EXPECT_EQ(g.frames[3].heldMs, config::kLongHeldMs);
}

TEST(DevGesture, SwipeMiddleFrameHasMovedToTheEnd) {
  // A real flick leaves the tap slop long before the 90 ms touch-down delay: no touch-down at the start.
  const Gesture g = makeSwipe(1, 2, 300, 400);
  ASSERT_EQ(g.count, 3u);
  const auto& mid = g.frames[1];
  EXPECT_TRUE(mid.has(F::Held));
  EXPECT_TRUE(mid.has(F::Moved));
  EXPECT_EQ(mid.x, 300);
  EXPECT_EQ(mid.y, 400);
  const Frame panel = toPanelFrame(mid, Orientation::Portrait, 800, 480);
  EXPECT_FALSE(panel.tapCandidate);
  EXPECT_TRUE(toPanelFrame(g.frames[0], Orientation::Portrait, 800, 480).tapCandidate);
}

TEST(DevGesture, SwipeCarriesBothEnds) {
  const Gesture g = makeSwipe(1, 2, 300, 400);
  ASSERT_EQ(g.count, 3u);
  const auto& end = g.frames[2];
  EXPECT_TRUE(end.has(F::Swipe));
  EXPECT_TRUE(end.has(F::Released));
  EXPECT_EQ(end.x, 1);
  EXPECT_EQ(end.x2, 300);
  EXPECT_EQ(end.y2, 400);
}

TEST(DevGesture, SwipeMinimumDistanceMatchesSdk) {
  EXPECT_TRUE(swipeIsLongEnough(0, 0, config::kSwipeMinPx, 0));
  EXPECT_TRUE(swipeIsLongEnough(100, 100, 100, 100 - config::kSwipeMinPx));
  EXPECT_FALSE(swipeIsLongEnough(0, 0, config::kSwipeMinPx - 1, config::kSwipeMinPx - 1));  // neither axis
  EXPECT_FALSE(swipeIsLongEnough(5, 5, 5, 5));
}

TEST(DevGesture, HomeIsOneFrame) {
  EXPECT_EQ(makeHome(false).count, 1u);
  EXPECT_TRUE(makeHome(false).frames[0].has(F::HomeTap));
  EXPECT_TRUE(makeHome(true).frames[0].has(F::HomeHold));
}

TEST(DevGesture, QueueIsFifoAndAllOrNothing) {
  GestureQueue q;
  const Gesture tap = makeTap(1, 1);
  size_t accepted = 0;
  while (q.enqueue(tap)) accepted++;
  EXPECT_EQ(accepted, config::kQueueCapacity / tap.count);
  EXPECT_EQ(q.size(), accepted * tap.count);  // nothing half-queued
  EXPECT_FALSE(q.enqueue(Gesture{}));         // empty gestures are rejected

  LogicalFrame f;
  ASSERT_TRUE(q.pop(f));
  EXPECT_TRUE(f.has(F::Down));
  ASSERT_TRUE(q.pop(f));
  EXPECT_TRUE(f.has(F::Held));
  ASSERT_TRUE(q.pop(f));
  EXPECT_TRUE(f.has(F::Tap));
  EXPECT_TRUE(q.enqueue(tap));  // room again after popping one gesture
}

TEST(DevGesture, PanelFrameConversion) {
  constexpr int W = 800, H = 480;
  const Gesture g = makeSwipe(0, 0, 479, 799);
  const Frame end = toPanelFrame(g.frames[2], Orientation::Portrait, W, H);
  EXPECT_TRUE(end.swipeStart.set);
  EXPECT_TRUE(end.swipeEnd.set);
  EXPECT_TRUE(end.released);
  EXPECT_FALSE(end.tap.set);
  float nx = 0, ny = 0;
  logicalToNormalised(Orientation::Portrait, 479, 799, W, H, nx, ny);
  EXPECT_FLOAT_EQ(end.swipeEnd.nx, nx);
  EXPECT_FLOAT_EQ(end.swipeEnd.ny, ny);

  const Frame home = toPanelFrame(makeHome(true).frames[0], Orientation::Portrait, W, H);
  EXPECT_TRUE(home.homeHold);
  EXPECT_FALSE(home.anyTouch());
}

}  // namespace
