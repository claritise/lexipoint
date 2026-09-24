// The HalGPIO synthetic-input overlay: one-frame promotion, the suppress latch, the held-time latch.

#include <LexiDevInput.h>
#include <gtest/gtest.h>

using lexipoint::dev::Frame;
using lexipoint::dev::Overlay;
using lexipoint::dev::TouchPoint;

namespace {

TouchPoint at(float nx, float ny) {
  TouchPoint p;
  p.set = true;
  p.nx = nx;
  p.ny = ny;
  return p;
}

Frame downFrame() {
  Frame f;
  f.down = at(0.5f, 0.5f);
  f.held = f.down;
  return f;
}

Frame releaseFrame(unsigned long heldMs) {
  Frame f;
  f.released = true;
  f.heldMs = heldMs;
  return f;
}

TEST(DevOverlay, InjectedFrameLastsExactlyOneFrame) {
  Overlay o;
  EXPECT_FALSE(o.ownsTouch());
  o.inject(downFrame());
  EXPECT_FALSE(o.ownsTouch());  // not until the next promote()
  o.promote(false);
  EXPECT_TRUE(o.ownsTouch());
  EXPECT_TRUE(o.active().down.set);
  o.promote(false);
  EXPECT_FALSE(o.ownsTouch());
}

TEST(DevOverlay, LaterInjectBeforePromoteWins) {
  Overlay o;
  o.inject(downFrame());
  o.inject(releaseFrame(10));
  o.promote(false);
  EXPECT_FALSE(o.active().down.set);
  EXPECT_TRUE(o.active().released);
}

TEST(DevOverlay, HomeEventsAreNotTouch) {
  Overlay o;
  Frame f;
  f.homeTap = true;
  o.inject(f);
  o.promote(false);
  EXPECT_FALSE(o.ownsTouch());
  EXPECT_TRUE(o.active().any());
}

TEST(DevOverlay, SuppressSwallowsRestOfGestureIncludingRelease) {
  Overlay o;
  Frame lp;
  lp.longPress = at(0.2f, 0.3f);
  lp.held = lp.longPress;
  o.inject(lp);
  o.promote(false);
  o.suppress();  // the screen consumed the long-press
  float sx = 0, sy = 0;
  EXPECT_FALSE(o.longPress(sx, sy));  // nothing left to report this frame

  o.inject(releaseFrame(800));
  o.promote(false);
  EXPECT_FALSE(o.active().released);  // the lift is hidden, like the SDK does
  EXPECT_FALSE(o.ownsTouch());

  o.inject(downFrame());  // the next gesture is unaffected
  o.promote(false);
  EXPECT_TRUE(o.active().down.set);
}

TEST(DevOverlay, SuppressedFrameStaysOwnedUntilNextFrame) {
  Overlay o;
  o.inject(downFrame());
  o.promote(false);
  o.suppress();
  EXPECT_TRUE(o.ownsTouch());  // the rest of this frame answers "nothing" rather than the real SDK
  float x = 0, y = 0;
  EXPECT_FALSE(o.down(x, y));
  EXPECT_FALSE(o.heldAt(x, y));
  o.promote(false);
  EXPECT_FALSE(o.ownsTouch());  // released back to the real SDK next frame
}

TEST(DevOverlay, NewGestureEndsSuppression) {
  Overlay o;
  o.inject(downFrame());
  o.promote(false);
  o.suppress();
  o.inject(downFrame());  // a new contact starts before any release arrived
  o.promote(false);
  EXPECT_TRUE(o.active().down.set);
}

TEST(DevOverlay, SuppressWithoutTouchIsNoOp) {
  Overlay o;
  o.suppress();
  o.inject(downFrame());
  o.promote(false);
  EXPECT_TRUE(o.active().down.set);
}

TEST(DevOverlay, HeldTimeLatchesUntilARealRelease) {
  Overlay o;
  EXPECT_FALSE(o.heldLatched());
  o.inject(releaseFrame(120));
  o.promote(false);
  EXPECT_TRUE(o.heldLatched());
  EXPECT_EQ(o.lastHeldMs(), 120u);

  o.promote(false);  // frames pass: still latched (the tap handler reads it after release)
  EXPECT_TRUE(o.heldLatched());

  o.promote(true);  // a real finger lifted
  EXPECT_FALSE(o.heldLatched());
}

TEST(DevOverlay, UnconsumedLongPressTapsOnRelease) {
  Overlay o;
  Frame lp;
  lp.longPress = at(0.2f, 0.3f);
  lp.held = lp.longPress;
  o.inject(lp);
  o.promote(false);  // nobody consumes it
  Frame lift;
  lift.tap = at(0.2f, 0.3f);
  lift.released = true;
  lift.heldMs = 800;
  o.inject(lift);
  o.promote(false);
  float nx = 0, ny = 0;
  EXPECT_TRUE(o.tap(nx, ny));
  EXPECT_TRUE(o.released());
  EXPECT_EQ(o.lastHeldMs(), 800u);
}

TEST(DevOverlay, ConsumedLongPressHidesTapAndReleaseButLatchesDuration) {
  Overlay o;
  Frame lp;
  lp.longPress = at(0.2f, 0.3f);
  lp.held = lp.longPress;
  o.inject(lp);
  o.promote(false);
  o.suppress();
  Frame lift;
  lift.tap = at(0.2f, 0.3f);
  lift.released = true;
  lift.heldMs = 800;
  o.inject(lift);
  o.promote(false);
  float nx = 0, ny = 0;
  EXPECT_FALSE(o.tap(nx, ny));
  EXPECT_FALSE(o.released());
  EXPECT_TRUE(o.heldLatched());  // the SDK records a suppressed contact's duration too
  EXPECT_EQ(o.lastHeldMs(), 800u);
}

TEST(DevOverlay, QueryMapping) {
  Overlay o;
  Frame f;
  f.down = at(0.1f, 0.2f);
  f.held = at(0.3f, 0.4f);
  f.tap = at(0.5f, 0.6f);
  f.longPress = at(0.7f, 0.8f);
  f.swipeStart = at(0.11f, 0.12f);
  f.swipeEnd = at(0.13f, 0.14f);
  f.released = true;
  f.heldMs = 321;
  f.homeTap = true;
  o.inject(f);
  o.promote(false);
  float x = 0, y = 0, x2 = 0, y2 = 0;
  unsigned long ms = 0;
  ASSERT_TRUE(o.down(x, y));
  EXPECT_FLOAT_EQ(x, 0.1f);
  ASSERT_TRUE(o.heldAt(x, y));
  EXPECT_FLOAT_EQ(y, 0.4f);
  ASSERT_TRUE(o.tap(x, y));
  EXPECT_FLOAT_EQ(x, 0.5f);
  ASSERT_TRUE(o.longPress(x, y));
  EXPECT_FLOAT_EQ(y, 0.8f);
  ASSERT_TRUE(o.tapCandidate(x, y, ms));
  EXPECT_EQ(ms, 321u);
  EXPECT_FLOAT_EQ(x, 0.3f);
  ASSERT_TRUE(o.swipe(x, y, x2, y2));
  EXPECT_FLOAT_EQ(x, 0.11f);
  EXPECT_FLOAT_EQ(y2, 0.14f);
  EXPECT_TRUE(o.released());
  EXPECT_TRUE(o.homeTap());
  EXPECT_FALSE(o.homeHold());
  EXPECT_TRUE(o.anyActivity());
}

TEST(DevOverlay, MovedFingerIsNotATapCandidateAndOutParamsAreUntouched) {
  Overlay o;
  Frame f;
  f.held = at(0.5f, 0.5f);
  f.tapCandidate = false;
  f.heldMs = 200;
  o.inject(f);
  o.promote(false);
  float x = -1, y = -1;
  unsigned long ms = 12345;
  EXPECT_FALSE(o.tapCandidate(x, y, ms));
  EXPECT_EQ(ms, 12345u);        // written only on success, like the SDK
  EXPECT_TRUE(o.heldAt(x, y));  // the finger is still held (drags see it)
}

}  // namespace
