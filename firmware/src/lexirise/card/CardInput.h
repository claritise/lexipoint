#pragma once

// The card's input, read on every loop() pass and handled once no render is running. loop() must not wait
// on RenderLock through a panel refresh (~0.5 s): touch is only read by the main loop, so a tap made during
// that wait would be read afterwards and stamped too late, and ShownTargets would match it against a frame
// the user hadn't seen. Each event keeps the time it was read. Pure, host-tested (test/lexirise_card).

#include <array>
#include <cstddef>

#include "CardController.h"
#include "ShownTargets.h"
#include "lexirise/LexiriseConfig.h"

namespace lexipoint::card {

struct InputEvent {
  enum class Kind : uint8_t { Tap, Step, Home };
  Kind kind = Kind::Tap;
  int x = 0;  // Tap
  int y = 0;
  int direction = 0;  // Step: +1 next, -1 previous
  unsigned long ms = 0;
};

class PendingInput {
 public:
  // Oldest first; when full, the newest is dropped (a burst during one refresh), except that a Home always
  // takes the last place: the way out never goes missing.
  void tap(int x, int y, unsigned long ms) { push({InputEvent::Kind::Tap, x, y, 0, ms}); }
  void step(int direction, unsigned long ms) { push({InputEvent::Kind::Step, 0, 0, direction, ms}); }
  void home(unsigned long ms) { push({InputEvent::Kind::Home, 0, 0, 0, ms}); }
  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }
  const InputEvent& operator[](const size_t i) const { return events_[i]; }
  void clear() { count_ = 0; }

 private:
  void push(const InputEvent& e) {
    if (count_ < events_.size()) {
      events_[count_++] = e;
    } else if (e.kind == InputEvent::Kind::Home) {
      events_[count_ - 1] = e;
    }
  }
  std::array<InputEvent, config::kCardPendingInputMax> events_{};
  size_t count_ = 0;
};

// Feeds the events to the controller in order (Home too: it queues like the rest, so it never overtakes a
// tap made before it), each at its own time and each tap against the frame on screen then (dropped when
// nothing was shown yet, or it showed another word), stopping at a close; then the phases due by `nowMs`.
// Redraw when anything changed; readingChanged when the reading ends up different.
Outcome handleInput(CardController& controller, const ShownTargets& targets, const PendingInput& input,
                    unsigned long nowMs);

}  // namespace lexipoint::card
