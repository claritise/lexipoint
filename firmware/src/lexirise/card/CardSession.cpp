#if LEXIRISE

#include "CardSession.h"

#include "lexirise/util/Timing.h"

namespace lexipoint::card {

Outcome CardSession::handleInput(const unsigned long nowMs) {
  if (!input_.empty()) lastActivityMs_ = nowMs;
  Outcome outcome = card::handleInput(controller_, targets_, input_, nowMs);
  input_.clear();
  if (live_) {
    for (const LevelChange& change : outcome.changes) live_->queue(change);  // the bench has nothing to send
  }
  return outcome;
}

CardSession::Answer CardSession::apply(LiveSource::Fetched fetched, const unsigned long nowMs) {
  Answer answer;
  if (!live_) return answer;
  lastActivityMs_ = nowMs;
  answer.clearFailed = fetched.clearFailed;
  answer.unreadable = fetched.unreadable;
  // A lookup for the word on screen changes what it shows (a save's retry fills the meaning in, or gives
  // another reason for none), even when the controller sees no change of phase or level.
  const bool onScreen = fetched.kind == LiveSource::Fetched::Kind::Entry && fetched.index == controller_.word();
  switch (live_->apply(std::move(fetched))) {
    case LiveSource::Advance::NotFound:
      answer.ended = LiveOutcome{LiveOutcome::Kind::NotFound, live_->error()};
      return answer;
    case LiveSource::Advance::Unavailable:
      answer.ended = LiveOutcome{LiveOutcome::Kind::Unavailable, live_->error()};
      return answer;
    case LiveSource::Advance::Changed:
      answer.redraw = controller_.sourceChanged(nowMs) || onScreen;
      break;
    case LiveSource::Advance::Idle:
      break;
  }
  if (const auto failed = live_->takeFailedWrite()) {  // Lexirise didn't take a level change: put it back
    controller_.levelFailed(failed->back.word, failed->back.to, nowMs, callFailure(failed->error), failed->back.from,
                            failed->retryAfterS);
    answer.writeFailed = true;
    answer.redraw = true;
  }
  return answer;
}

bool CardSession::shouldFetchDeck(const unsigned long nowMs, const bool rendering, const bool touching,
                                  const std::optional<unsigned long> cardDueMs) const {
  return live_ && deckAllowed_ && !rendering && !touching && !cardDueMs && !drawPending_ && input_.empty() &&
         !hasWork(nowMs) && !live_->hasPendingWrites() &&
         timing::reached(nowMs, lastActivityMs_ + config::kDeckIdleMs) && live_->hasDeckWork();
}

void CardSession::applyDeck(const deck::DeckCall& call, const unsigned long nowMs) {
  if (!live_) return;
  live_->applyDeck(call);
  lastActivityMs_ = nowMs;  // the next step waits its own idle time too
}

CardSession::Answer CardSession::applyClosing(LiveSource::Fetched fetched, const unsigned long nowMs) {
  Answer answer = apply(std::move(fetched), nowMs);
  if (answer.writeFailed) {
    unsentSaves_++;
    unsentError_ = live_->error();
  }
  return answer;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
