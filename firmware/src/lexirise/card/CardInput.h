#pragma once

// The card's input, read on every loop() pass and handled once no render is running. loop() must not wait
// on RenderLock through a panel refresh (~0.5 s): touch is only read by the main loop, so a tap made during
// that wait would be read afterwards and stamped too late, and ShownTargets would match it against a frame
// the user hadn't seen. Each event keeps the time it was read. Pure, host-tested (test/lexirise_card).

#include <FreeInkUICore.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>

#include "CardController.h"
#include "ShownTargets.h"
#include "lexirise/LexiriseConfig.h"

namespace lexipoint::card {

struct InputEvent {
  enum class Kind : uint8_t { Tap, Step, Home, Swipe, LongPress };
  Kind kind = Kind::Tap;
  int x = 0;  // Tap, LongPress; Swipe: where it started
  int y = 0;
  int direction = 0;  // Step: +1 next, -1 previous
  unsigned long ms = 0;
  Swipe swipe = Swipe::Up;  // Swipe
  // Step: when the button was first seen down (a press held over a network call is old); none: not known.
  std::optional<unsigned long> pressedMs = std::nullopt;
};

// Card swipes stay clear of CrossPoint's edge gestures (popup-ui.md §3.2: Back from the left edge, the
// reader menu or Home from the bottom, the frontlight panel from the top). A swipe the SDK classifies as one
// of those (fui::edgeSwipe, its own bands) is never the card's, and a card swipe also starts at least
// config::kCardSwipeEdgeMarginPx (~10 mm) inside the left, top and bottom edges whichever way it goes. It
// must also start on the card (handleInput). Logical (portrait) coordinates.
inline bool swipeClearOfEdges(const int startX, const int startY, const int endX, const int endY, const int screenWidth,
                              const int screenHeight) {
  using freeink::ui::ScreenEdge;
  constexpr ScreenEdge kReserved[] = {ScreenEdge::Left, ScreenEdge::Top, ScreenEdge::Bottom};
  const bool edgeGesture = std::any_of(std::begin(kReserved), std::end(kReserved), [&](const ScreenEdge edge) {
    return freeink::ui::edgeSwipe(edge, startX, startY, endX, endY, screenWidth, screenHeight);
  });
  return !edgeGesture && startX >= config::kCardSwipeEdgeMarginPx && startY >= config::kCardSwipeEdgeMarginPx &&
         startY < screenHeight - config::kCardSwipeEdgeMarginPx;
}

// A tap or a long-press on the page outside the card looks the word there up in its place (popup-ui.md §3.2;
// the tap since P10) only on a live card with word select's page under it, in the same coordinates (not the
// bench, not a landscape page); elsewhere a tap just closes the card and a long-press does nothing. A
// long-press is always consumed either way.
inline bool pagePressLooksUp(const bool live, const bool pageUnderCard) { return live && pageUnderCard; }
// The point a closing card hands word select: a tap's or long-press's, kept only where pagePressLooksUp.
inline std::optional<PagePoint> lookUpOnClose(const std::optional<PagePoint>& at, const bool live,
                                              const bool pageUnderCard) {
  return pagePressLooksUp(live, pageUnderCard) ? at : std::nullopt;
}

// The card's swipe for a swipe's endpoints (the SDK's dominant-axis rule, as MappedInputManager::wasSwipe);
// nullopt when the SDK names no direction.
inline std::optional<Swipe> swipeBetween(const int startX, const int startY, const int endX, const int endY) {
  switch (freeink::ui::swipeDirection(startX, startY, endX, endY)) {
    case freeink::ui::SwipeDir::Left:
      return Swipe::Left;
    case freeink::ui::SwipeDir::Right:
      return Swipe::Right;
    case freeink::ui::SwipeDir::Up:
      return Swipe::Up;
    case freeink::ui::SwipeDir::Down:
      return Swipe::Down;
    case freeink::ui::SwipeDir::None:
      break;
  }
  return std::nullopt;
}

class PendingInput {
 public:
  // Oldest first; when full, the newest is dropped (a burst during one refresh), except that a Home or a
  // long-press always takes the last place: the way out (or to the next word) never goes missing.
  void tap(int x, int y, unsigned long ms) { push({InputEvent::Kind::Tap, x, y, 0, ms}); }
  // `pressedMs`: when the button went down (the release is read at `ms`).
  void step(int direction, unsigned long ms, std::optional<unsigned long> pressedMs) {
    push({InputEvent::Kind::Step, 0, 0, direction, ms, Swipe::Up, pressedMs});
  }
  void step(int direction, unsigned long ms) { step(direction, ms, std::nullopt); }  // press time not known
  void home(unsigned long ms) { push({InputEvent::Kind::Home, 0, 0, 0, ms}); }
  void swipe(Swipe direction, int x, int y, unsigned long ms) {
    push({InputEvent::Kind::Swipe, x, y, 0, ms, direction});
  }
  void longPress(int x, int y, unsigned long ms) { push({InputEvent::Kind::LongPress, x, y, 0, ms}); }
  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }
  const InputEvent& operator[](const size_t i) const { return events_[i]; }
  void clear() { count_ = 0; }

 private:
  void push(const InputEvent& e) {
    if (count_ < events_.size()) {
      events_[count_++] = e;
    } else if (e.kind == InputEvent::Kind::Home ||
               (e.kind == InputEvent::Kind::LongPress && events_[count_ - 1].kind != InputEvent::Kind::Home)) {
      events_[count_ - 1] = e;  // a long-press never takes a Home's place: the way out comes first
    }
  }
  std::array<InputEvent, config::kCardPendingInputMax> events_{};
  size_t count_ = 0;
};

// Feeds the events to the controller in order (Home too: it queues like the rest, so it never overtakes a
// tap made before it), each at its own time and each tap, swipe and long-press against the frame on screen
// then (dropped when nothing was shown yet, or it showed another word or the other view; a swipe that didn't
// start on the card is dropped too), stopping at a close; then the phases due by `nowMs`.
// Redraw when anything changed; readingChanged when the reading ends up different.
Outcome handleInput(CardController& controller, const ShownTargets& targets, const PendingInput& input,
                    unsigned long nowMs);

}  // namespace lexipoint::card
