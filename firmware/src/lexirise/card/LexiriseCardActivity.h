#pragma once

// The card as an activity: the bench (P4) over the reference's fixtures, opened from the dev harness
// (LX:LEXI CARD). Glue only: CardController decides, CardLayout lays out, CardPainter draws.
// popup-ui.md; tests of the pieces: test/lexirise_card. P5 feeds it from the lookup.

#include <optional>

#include "activities/Activity.h"
#include "lexirise/card/CardController.h"
#include "lexirise/card/CardInput.h"
#include "lexirise/card/ShownTargets.h"

namespace lexipoint::card {

class LexiriseCardActivity final : public Activity {
 public:
  // How the bench opens (the dev harness's LX:LEXI CARD).
  struct Options {
    bool low = false;    // the sentence low on the page (D17: the card-view strip)
    bool smoke = false;  // lxctl card-smoke: starts in kana and never saves a reading switch
  };
  LexiriseCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const BenchBook& book, Options options);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  void onExit() override;

 private:
  void apply(const Outcome& outcome);
  void close();

  CardController controller_;               // loop() and render() share it: touched only under RenderLock
  ShownTargets targets_;                    // the touch targets on screen (under RenderLock)
  PendingInput input_;                      // loop() only
  std::optional<unsigned long> nextDueMs_;  // loop()'s own copy of controller_.nextDueMs()
  bool persistReading_ = true;
  bool smoke_ = false;     // Options::smoke: logs the word after each input (lxctl card-smoke)
  int orientation_ = 0;    // the reader's, restored on exit
  static int cardsShown_;  // for the periodic half refresh (config::kCardHalfRefreshEvery)
};

}  // namespace lexipoint::card
