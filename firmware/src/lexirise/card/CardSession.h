#pragma once

// What the card activity does between its input, the card and the network, without the device: the queued
// input handled (the level changes it makes go to the source), a lookup's answer applied, a failed write
// put back, the writes still queued sent when the card closes. LexiriseCardActivity only adds RenderLock,
// redraw requests and the log around it. Pure; tests: test/lexirise_card (with a scripted Lexirise).

#include <atomic>
#include <optional>

#include "CardController.h"
#include "CardInput.h"
#include "LiveSource.h"
#include "ShownTargets.h"

namespace lexipoint::card {

// How a live card ended, for word select (the card is gone by then): closed by the user, or never shown
// because Lexirise had no word (NotFound) or no answer (Unavailable: StarDict gets its turn).
struct LiveOutcome {
  enum class Kind : uint8_t { Closed, NotFound, Unavailable } kind = Kind::Closed;
  api::ApiError error = api::ApiError::None;
};

// What word select does once the card has ended (lookup-flow.md §4, §5b): redraw its page; say "Not
// found" (a Lexirise miss is final); let StarDict answer; or say "No dictionary set" when there's none.
enum class AfterCard : uint8_t { Redraw, NotFound, RunStarDict, NoDictionary };
inline AfterCard afterCard(const LiveOutcome::Kind ended, const bool starDictSet) {
  switch (ended) {
    case LiveOutcome::Kind::Closed:
      return AfterCard::Redraw;
    case LiveOutcome::Kind::NotFound:
      return AfterCard::NotFound;
    case LiveOutcome::Kind::Unavailable:
      break;
  }
  return starDictSet ? AfterCard::RunStarDict : AfterCard::NoDictionary;
}

class CardSession {
 public:
  // `live`: the lookup's source, or nullptr for the bench (nothing to fetch or send).
  CardSession(CardController& controller, ShownTargets& targets, PendingInput& input, LiveSource* live)
      : controller_(controller), targets_(targets), input_(input), live_(live) {}

  // The queued input, handled and cleared (under RenderLock on the device). Its level changes are queued
  // for Lexirise, all of them, in order: a close in the same batch doesn't lose the save before it.
  Outcome handleInput(unsigned long nowMs);

  // Whether loop() makes its network call now: there's work, and nothing is waiting to be drawn or being
  // drawn. A call blocks the loop (no input is read), so the screen must not change under it: a redraw
  // asked for first gets on screen (a step's phase A, a save's toast), and a tap read after the call is
  // matched against the frame that was up all along.
  // `rendering`: a render holds the lock now (RenderLock::peek()).
  // Input still queued (read while a render held the lock) goes first: it may change what to fetch.
  bool shouldFetch(const unsigned long nowMs, const bool rendering) const {
    return !rendering && !drawPending_ && input_.empty() && hasWork(nowMs);
  }
  // A redraw was asked for (loop task) / its frame is on screen (render task, after the refresh).
  void redrawAsked() { drawPending_ = true; }
  void frameShown() { drawPending_ = false; }
  bool hasWork(const unsigned long nowMs) const { return live_ && live_->hasWork(nowMs); }
  // One network call (outside RenderLock); `closing`: only what the queued writes need, all of them now.
  LiveSource::Fetched fetch(const unsigned long nowMs, const bool closing = false) const {
    return live_ ? live_->fetch(nowMs, closing) : LiveSource::Fetched{};
  }

  struct Answer {
    bool redraw = false;
    std::optional<LiveOutcome> ended;  // the card ends: nothing to show
    bool writeFailed = false;          // a level change was put back (the error is live's error())
    bool clearFailed = false;          // a removal went through, its notes and tags weren't cleared
  };
  Answer apply(LiveSource::Fetched fetched, unsigned long nowMs);  // under RenderLock on the device

  bool hasPendingWrites() const { return live_ && live_->hasPendingWrites(); }

 private:
  CardController& controller_;
  ShownTargets& targets_;
  PendingInput& input_;
  LiveSource* live_;
  std::atomic<bool> drawPending_{false};  // redrawAsked(), not yet frameShown()
};

}  // namespace lexipoint::card
