// Compiled into the dev harness build, and into host tests (which define the flag themselves).
#if LEXIPOINT_DEV_HARNESS

#include "DevGesture.h"

namespace lexipoint::dev {
namespace {

using F = LogicalFrame;

LogicalFrame frame(const unsigned flags, const int x, const int y, const unsigned long heldMs) {
  LogicalFrame f;
  f.flags = flags;
  f.x = x;
  f.y = y;
  f.heldMs = heldMs;
  return f;
}

void add(Gesture& g, const LogicalFrame& f) {
  if (g.count < config::kMaxGestureFrames) g.frames[g.count++] = f;
}

TouchPoint point(const Orientation o, const int x, const int y, const int w, const int h) {
  TouchPoint p;
  p.set = true;
  logicalToNormalised(o, x, y, w, h, p.nx, p.ny);
  return p;
}

}  // namespace

Gesture makeTap(const int x, const int y) {
  Gesture g;
  add(g, frame(F::Down | F::Held, x, y, 0));
  add(g, frame(F::Held, x, y, config::kTapHeldMs));
  add(g, frame(F::Tap | F::Released, x, y, config::kTapHeldMs));
  return g;
}

Gesture makeLongPress(const int x, const int y) {
  Gesture g;
  add(g, frame(F::Down | F::Held, x, y, 0));
  add(g, frame(F::Held, x, y, config::kTapHeldMs));
  add(g, frame(F::LongPress | F::Held, x, y, config::kLongHeldMs));
  add(g, frame(F::Tap | F::Released, x, y, config::kLongHeldMs));
  return g;
}

Gesture makeSwipe(const int x1, const int y1, const int x2, const int y2) {
  Gesture g;
  add(g, frame(F::Down | F::Held, x1, y1, 0));
  add(g, frame(F::Held | F::Moved, x2, y2, config::kSwipeHeldMs));
  LogicalFrame end = frame(F::Swipe | F::Released, x1, y1, config::kSwipeHeldMs);
  end.x2 = x2;
  end.y2 = y2;
  add(g, end);
  return g;
}

bool swipeIsLongEnough(const int x1, const int y1, const int x2, const int y2) {
  const int dx = x2 > x1 ? x2 - x1 : x1 - x2;
  const int dy = y2 > y1 ? y2 - y1 : y1 - y2;
  return dx >= config::kSwipeMinPx || dy >= config::kSwipeMinPx;
}

Gesture makeHome(const bool hold) {
  Gesture g;
  add(g, frame(hold ? F::HomeHold : F::HomeTap, 0, 0, 0));
  return g;
}

bool GestureQueue::enqueue(const Gesture& g) {
  if (g.count == 0 || count_ + g.count > config::kQueueCapacity) return false;
  for (size_t i = 0; i < g.count; i++) {
    frames_[(head_ + count_) % config::kQueueCapacity] = g.frames[i];
    count_++;
  }
  return true;
}

bool GestureQueue::pop(LogicalFrame& out) {
  if (count_ == 0) return false;
  out = frames_[head_];
  head_ = (head_ + 1) % config::kQueueCapacity;
  count_--;
  return true;
}

Frame toPanelFrame(const LogicalFrame& f, const Orientation o, const int w, const int h) {
  Frame out;
  const auto at = point(o, f.x, f.y, w, h);
  if (f.has(F::Down)) out.down = at;
  if (f.has(F::Held)) out.held = at;
  if (f.has(F::Tap)) out.tap = at;
  if (f.has(F::LongPress)) out.longPress = at;
  if (f.has(F::Swipe)) {
    out.swipeStart = at;
    out.swipeEnd = point(o, f.x2, f.y2, w, h);
  }
  out.released = f.has(F::Released);
  out.tapCandidate = !f.has(F::Moved);
  out.heldMs = f.heldMs;
  out.homeTap = f.has(F::HomeTap);
  out.homeHold = f.has(F::HomeHold);
  return out;
}

}  // namespace lexipoint::dev

#endif  // LEXIPOINT_DEV_HARNESS
