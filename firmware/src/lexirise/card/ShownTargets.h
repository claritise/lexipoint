#pragma once

// The touch targets of the frame that was on screen when a tap was made, and the word that frame showed
// (a tap on the old card while a side-button step redraws must not act on the new word). render() lays out a new frame,
// then holds the lock through the panel refresh, so loop() only gets to a tap sampled during the refresh
// once the new targets are stored: matching it against them would hit what the user hadn't seen yet (the
// bottom-anchored card grows upward as phase B fills the meaning line). Pure, host-tested
// (test/lexirise_card); the activity keeps it under RenderLock.

#include <utility>
#include <vector>

#include "DisplayList.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

struct ShownFrame {
  std::vector<Hit> hits;
  int word = -1;  // the card's word in this frame (CardController::word())
};

class ShownTargets {
 public:
  // render(): this frame's targets and word, before its refresh starts.
  void drawing(std::vector<Hit> hits, const int word) { pending_ = {std::move(hits), word}; }
  // render(): the refresh ended at `nowMs`; the pending frame is what the user sees from here on.
  void shown(const unsigned long nowMs) {
    previous_ = std::move(current_);
    hasPrevious_ = hasCurrent_;
    current_ = std::move(pending_);
    pending_ = {};
    hasCurrent_ = true;
    shownAtMs_ = nowMs;
  }
  // The frame on screen at `tapMs`; nullptr when nothing was shown yet (the tap is ignored, not a miss
  // that closes the card).
  const ShownFrame* at(const unsigned long tapMs) const {
    if (!hasCurrent_) return nullptr;
    if (timing::reached(tapMs, shownAtMs_)) return &current_;
    return hasPrevious_ ? &previous_ : nullptr;
  }

 private:
  ShownFrame pending_;
  ShownFrame current_;
  ShownFrame previous_;
  unsigned long shownAtMs_ = 0;
  bool hasCurrent_ = false;
  bool hasPrevious_ = false;
};

}  // namespace lexipoint::card
