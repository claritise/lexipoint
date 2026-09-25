#pragma once

// One frame of the card screen from the controller's state: the page under the card (card view only),
// and the card itself with its touch targets. Pure: the device activity and the host preview tool both
// build their frames here, so the preview draws exactly what the device does. The page comes from the
// controller's CardSource. Tests: test/lexirise_card.

#include "CardController.h"
#include "CardSource.h"
#include "DisplayList.h"

namespace lexipoint::card {

struct Frame {
  bool pageShown = false;  // the expanded view covers the page
  PageScene scene;         // the page (drawn only when pageShown), and the word's box / strip / sentence
  DisplayList card;
};

// `pageVisible`: the page is drawn under the card (false when the card turned a landscape reader to
// portrait): no highlight then, and the word counts as covered, so the card view shows its strip (D17).
Frame composeFrame(const CardController& controller, const TextMetrics& metrics, bool pageVisible = true);

}  // namespace lexipoint::card
