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
  api::ApiError error = api::ApiError::None;  // NotFound / Unavailable: why
  // Closed: saves that couldn't be sent as the card closed (a Retry still waiting out a 429, WiFi gone),
  // and why the last one failed: word select says so, since the card can't any more.
  int unsentSaves = 0;
  api::ApiError unsentError = api::ApiError::None;
  // Closed by a tap or a long-press on the page outside the card: the word there is looked up next.
  std::optional<PagePoint> lookUpAt{};
};

// What word select does once the card has ended (lookup-flow.md §4, §5b): the user closed it (Closed:
// WordSelectFlow.h closeStep says where to); saves went unsent (UnsentSave: that notice first, then the rest
// of the close); say "Not found" (a Lexirise miss is final); let StarDict answer; or say "No dictionary set"
// when there's none.
enum class AfterCard : uint8_t { Closed, UnsentSave, NotFound, RunStarDict, NoDictionary };
inline AfterCard afterCard(const LiveOutcome& ended, const bool starDictSet) {
  switch (ended.kind) {
    case LiveOutcome::Kind::Closed:
      return ended.unsentSaves > 0 ? AfterCard::UnsentSave : AfterCard::Closed;  // never fails silently (§0)
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
    std::string unreadable;            // a response we couldn't read: its start, for the log
  };
  Answer apply(LiveSource::Fetched fetched, unsigned long nowMs);  // under RenderLock on the device
  // The same, as the card closes: a write that fails now is counted for word select to tell
  // (LiveOutcome::unsentSaves), since the card can't show its toast any more.
  Answer applyClosing(LiveSource::Fetched fetched, unsigned long nowMs);
  int unsentSaves() const { return unsentSaves_; }
  api::ApiError unsentError() const { return unsentError_; }

  bool hasPendingWrites() const { return live_ && live_->hasPendingWrites(); }

 private:
  CardController& controller_;
  ShownTargets& targets_;
  PendingInput& input_;
  LiveSource* live_;
  std::atomic<bool> drawPending_{false};  // redrawAsked(), not yet frameShown()
  int unsentSaves_ = 0;
  api::ApiError unsentError_ = api::ApiError::None;
};

}  // namespace lexipoint::card
