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

  // The book's deck (LiveSource::setBookDeck): its next call runs only on an idle card, and never as the card
  // closes: nothing else to fetch or send (a write in its Undo window included), nothing to draw or handle, and no
  // input or answer for config::kDeckIdleMs. The call blocks the loop like any other: a tap made during it waits
  // for it (one request), and a side-button press made and released during it is lost (rare: one to three calls
  // per book per boot). A new book takes two idle windows after its save's Undo window: the list, then the
  // creation.
  // A finger is on the screen (a swipe or a long-press on its way): the idle time starts again.
  void touched(const unsigned long nowMs) { lastActivityMs_ = nowMs; }
  // `touching`: a finger is on the screen (see touched()): not idle. `cardDueMs`: when the
  // card next has something to do (CardController::nextDueMs, the loop's copy): a toast still up ("Save failed ·
  // Retry" on the same network a deck call would wait on) or a phase to come, so not idle either.
  bool shouldFetchDeck(unsigned long nowMs, bool rendering, bool touching,
                       std::optional<unsigned long> cardDueMs) const;
  // Book decks may be made (deck::deckAllowed): set when the card opens and whenever the settings change (the web
  // page can turn Deck per book off while a card is open).
  void setDeckAllowed(const bool allowed) { deckAllowed_ = allowed; }
  void opened(const unsigned long nowMs) { lastActivityMs_ = nowMs; }  // the idle time starts when the card opens
  deck::DeckCall fetchDeck() { return live_ ? live_->fetchDeck() : deck::DeckCall{}; }
  // Outside RenderLock: the card's state doesn't change, only the deck store.
  void applyDeck(const deck::DeckCall& call, unsigned long nowMs);

 private:
  CardController& controller_;
  ShownTargets& targets_;
  PendingInput& input_;
  LiveSource* live_;
  std::atomic<bool> drawPending_{false};  // redrawAsked(), not yet frameShown()
  unsigned long lastActivityMs_ = 0;      // the card's opening, its last input, answer or deck call (loop task)
  bool deckAllowed_ = true;
  int unsentSaves_ = 0;
  api::ApiError unsentError_ = api::ApiError::None;
};

}  // namespace lexipoint::card
