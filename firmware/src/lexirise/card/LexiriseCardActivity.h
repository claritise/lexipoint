#pragma once

// The card as an activity (popup-ui.md): the bench (P4) over the reference's fixtures, opened from the
// dev harness (LX:LEXI CARD), or the live card (P5) over the reader's page, opened by word select with
// a lookup to run. Glue only: CardController decides, CardLayout lays out, CardPainter draws, the
// CardSource answers. Tests of the pieces: test/lexirise_card.

#include <functional>
#include <memory>
#include <optional>

#include "activities/Activity.h"
#include "lexirise/card/BenchFixtures.h"
#include "lexirise/card/CardController.h"
#include "lexirise/card/CardInput.h"
#include "lexirise/card/CardSession.h"
#include "lexirise/card/LiveSource.h"
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

  // The live card. `drawPage` draws the reader's page (the backdrop in card view, in the orientation it
  // was laid out in); `outcome` is filled when the card ends.
  LexiriseCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<LiveSource> source,
                       std::function<void(GfxRenderer&)> drawPage, std::shared_ptr<LiveOutcome> outcome);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  void onExit() override;

 private:
  void handleQueuedInput(unsigned long nowMs);
  // Smoke mode's word log (lxctl): what the card shows, read under RenderLock, logged after it.
  struct SmokeState {
    int word = 0;
    bool expanded = false;
    int tab = 0;
  };
  SmokeState smokeState() const;
  void logWord(const SmokeState& shown, bool hadInput);
  void readGestures(unsigned long now);  // a long-press and a card swipe, queued with the rest
  void fetchAnswer();
  // The writes still queued, sent before the card goes (the toast said "Saved"). `lockHeld`: from
  // onExit, under the lock exitActivity holds.
  void flushWrites(bool lockHeld);
  void apply(const Outcome& outcome);
  void end(LiveOutcome ending);
  void logAnswer(const CardSession::Answer& answer) const;
  void redraw();  // requestUpdate(), telling the session a frame is on its way

  std::unique_ptr<CardSource> source_;  // the bench's, or the lookup's
  LiveSource* live_ = nullptr;          // source_ when live: loop() fetches, apply() under RenderLock
  std::function<void(GfxRenderer&)> drawPage_;
  std::shared_ptr<LiveOutcome> outcome_;
  CardController controller_;               // loop() and render() share it: touched only under RenderLock
  ShownTargets targets_;                    // the touch targets on screen (under RenderLock)
  PendingInput input_;                      // loop() only
  CardSession session_;                     // the three above and the live source, glued (pure)
  std::optional<unsigned long> nextDueMs_;  // loop()'s own copy of controller_.nextDueMs()
  bool persistReading_ = true;
  bool smoke_ = false;         // Options::smoke: logs the word after each input (lxctl card-smoke)
  int loggedWord_ = -1;        // smoke: the word last logged
  int orientation_ = 0;        // the reader's, restored on exit
  bool pageUnderCard_ = true;  // false when the page was laid out for another orientation (landscape)
  bool finishing_ = false;     // end() ran: no more lookups or input
  static int cardsShown_;      // for the periodic half refresh (config::kCardHalfRefreshEvery)
};

}  // namespace lexipoint::card
