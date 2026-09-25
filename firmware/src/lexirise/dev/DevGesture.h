#pragma once

// LEXIPOINT dev harness gestures (pure, host-testable). A gesture is a short sequence of frames in
// *logical* screen coordinates; each frame is converted to panel-native coordinates only when it's
// fed to HalGPIO, using the orientation and panel size at that moment.
// Tests: test/lexirise_dev/DevGestureTest.cpp.

#include <LexiDevInput.h>

#include <cstddef>

#include "DevConfig.h"
#include "DevCoords.h"

namespace lexipoint::dev {

struct LogicalFrame {
  enum Flag : unsigned {
    Down = 1u << 0,
    Held = 1u << 1,
    Tap = 1u << 2,
    LongPress = 1u << 3,
    Swipe = 1u << 4,
    Released = 1u << 5,
    HomeTap = 1u << 6,
    HomeHold = 1u << 7,
    Moved = 1u << 8,  // the finger has left the tap slop (no longer a tap candidate)
  };
  unsigned flags = 0;
  int x = 0, y = 0;    // the contact point (swipe start)
  int x2 = 0, y2 = 0;  // swipe end
  unsigned long heldMs = 0;
  bool has(const Flag f) const { return (flags & f) != 0; }
};

struct Gesture {
  LogicalFrame frames[config::kMaxGestureFrames];
  size_t count = 0;
};

// Frame sequences, matching what the SDK classifier reports for a real finger:
//  tap:   down(0) → held(kTapHeldMs) → tap+released(kTapHeldMs)
//  long:  down(0) → held(kTapHeldMs) → longPress+held(kLongHeldMs) → tap+released(kLongHeldMs)
//         (like the SDK, an unconsumed long-press still taps on lift; a screen that consumes the
//          long-press calls suppressTouchContact(), which swallows that tap and release)
//  swipe: down(0) → held at the end point, moved(kSwipeHeldMs) → swipe+released(kSwipeHeldMs)
//         (the moved finger is not a tap candidate, so no touch-down fires at the start point)
//  home:  homeTap, or homeHold
Gesture makeTap(int x, int y);
Gesture makeLongPress(int x, int y);
Gesture makeSwipe(int x1, int y1, int x2, int y2);
Gesture makeHome(bool hold);

// True when a swipe travels far enough for the SDK classifier to report it (config::kSwipeMinPx on at
// least one axis). Logical and panel pixels are the same size.
bool swipeIsLongEnough(int x1, int y1, int x2, int y2);

// Fixed-capacity FIFO of frames. Gestures go in whole or not at all.
class GestureQueue {
 public:
  bool enqueue(const Gesture& g);
  bool pop(LogicalFrame& out);
  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }
  void clear() { head_ = count_ = 0; }

 private:
  LogicalFrame frames_[config::kQueueCapacity];
  size_t head_ = 0;
  size_t count_ = 0;
};

// Converts one logical frame to the panel-native frame HalGPIO consumes.
Frame toPanelFrame(const LogicalFrame& f, Orientation orientation, int panelWidth, int panelHeight);

}  // namespace lexipoint::dev
