#pragma once

// One frame of the card screen from the controller's state: the page under the card (card view only),
// and the card itself with its touch targets. Pure: the device activity and the host preview tool both
// build their frames here, so the preview draws exactly what the device does. P5 replaces the bench
// page with the reader's page. Tests: test/lexirise_card.

#include "BenchPage.h"
#include "CardController.h"
#include "DisplayList.h"

namespace lexipoint::card {

struct Frame {
  bool pageShown = false;  // the expanded view covers the page
  bench::Scene scene;      // the page (drawn only when pageShown), and the word's box / strip / sentence
  DisplayList card;
};

Frame composeFrame(const CardController& controller, const TextMetrics& metrics);

}  // namespace lexipoint::card
