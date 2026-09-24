#pragma once

// LEXIPOINT dev harness: synthetic input overlay (pure, no Arduino dependencies, host-testable).
//
// The USB dev harness (src/lexirise/dev/) injects one Frame per main-loop frame. HalGPIO promotes it
// in update() and, while it carries touch, answers *every* touch query from it alone (the query
// methods below), so an injected gesture never mixes with a real finger inside one frame. Two latches
// keep it faithful to the SDK:
//  - suppress(): like InputManager::suppressTouchContact(), the rest of the current gesture
//    (held frames, its tap and its release) is swallowed once a screen consumes it.
//  - held time: lastTouchHeldMs() reports the injected gesture's duration after its release (also a
//    suppressed one, as the SDK does), until a real finger lifts, so a leftover real long-hold can't
//    turn an injected tap into a long-press.
// Only included by HalGPIO under LEXIPOINT_DEV_HARNESS. Tests: test/lexirise_dev/DevOverlayTest.cpp.

namespace lexipoint::dev {

struct TouchPoint {
  bool set = false;
  float nx = 0;  // normalised panel-native coordinates (0..1)
  float ny = 0;
};

struct Frame {
  TouchPoint down;       // contact starts this frame
  TouchPoint held;       // a finger rests here this frame
  TouchPoint tap;        // tap classified this frame (the finger lifted inside the tap slop)
  TouchPoint longPress;  // long-press classified this frame (finger still down)
  TouchPoint swipeStart;
  TouchPoint swipeEnd;
  bool released = false;     // the finger lifts this frame
  bool tapCandidate = true;  // a held finger still inside the tap slop (false once it has moved)
  unsigned long heldMs = 0;  // contact duration reported for this frame
  bool homeTap = false;
  bool homeHold = false;

  bool anyTouch() const { return down.set || held.set || tap.set || longPress.set || swipeStart.set || released; }
  bool any() const { return anyTouch() || homeTap || homeHold; }
  void clearTouch() {
    down = held = tap = longPress = swipeStart = swipeEnd = TouchPoint{};
    released = false;
  }
};

class Overlay {
 public:
  // Queue the frame to become active at the next promote(). A later inject() before then wins.
  void inject(const Frame& frame) {
    queued_ = frame;
    hasQueued_ = true;
  }

  // Once per main-loop frame, after the SDK's own update(). realRelease: the real finger lifted.
  void promote(const bool realRelease) {
    active_ = hasQueued_ ? queued_ : Frame{};
    hasQueued_ = false;
    ownedThisFrame_ = false;

    // The held-time latch sees the release even if suppression hides it below (the SDK records the
    // duration of a suppressed contact too).
    if (active_.released) {
      lastHeldMs_ = active_.heldMs;
      heldLatched_ = true;
    } else if (realRelease) {
      heldLatched_ = false;  // a real finger lifted: the real duration is current again
    }

    if (suppressed_) {
      if (active_.down.set) {
        suppressed_ = false;  // a new gesture starts: the old one is over
      } else if (active_.anyTouch()) {
        const bool endsGesture = active_.released;
        active_.clearTouch();
        if (endsGesture) suppressed_ = false;
      }
    }
  }

  // A screen consumed the current contact (see InputManager::suppressTouchContact()).
  void suppress() {
    if (!active_.anyTouch()) return;
    const bool endsGesture = active_.released;
    active_.clearTouch();
    suppressed_ = !endsGesture;
    ownedThisFrame_ = true;  // keep answering (now empty) for the rest of this frame, not the real SDK
  }

  // Queries, mirroring HalGPIO's. Only meaningful while ownsTouch().
  bool ownsTouch() const { return active_.anyTouch() || ownedThisFrame_; }
  bool tap(float& nx, float& ny) const { return point(active_.tap, nx, ny); }
  bool down(float& nx, float& ny) const { return point(active_.down, nx, ny); }
  bool released() const { return active_.released; }
  bool heldAt(float& nx, float& ny) const { return point(active_.held, nx, ny); }
  bool longPress(float& nx, float& ny) const { return point(active_.longPress, nx, ny); }
  bool tapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
    if (!active_.held.set || !active_.tapCandidate) return false;
    heldMs = active_.heldMs;  // written only on success, like the SDK
    return point(active_.held, nx, ny);
  }
  bool swipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
    if (!active_.swipeStart.set || !active_.swipeEnd.set) return false;
    point(active_.swipeStart, nxStart, nyStart);
    return point(active_.swipeEnd, nxEnd, nyEnd);
  }
  bool homeTap() const { return active_.homeTap; }
  bool homeHold() const { return active_.homeHold; }
  bool anyActivity() const { return active_.any(); }
  bool heldLatched() const { return heldLatched_; }
  unsigned long lastHeldMs() const { return lastHeldMs_; }

  const Frame& active() const { return active_; }

 private:
  static bool point(const TouchPoint& p, float& nx, float& ny) {
    if (!p.set) return false;
    nx = p.nx;
    ny = p.ny;
    return true;
  }

  Frame queued_;
  bool hasQueued_ = false;
  Frame active_;
  bool suppressed_ = false;
  bool ownedThisFrame_ = false;
  bool heldLatched_ = false;
  unsigned long lastHeldMs_ = 0;
};

}  // namespace lexipoint::dev
