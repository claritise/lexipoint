#if LEXIRISE

#include "CardSession.h"

namespace lexipoint::card {

Outcome CardSession::handleInput(const unsigned long nowMs) {
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
  answer.clearFailed = fetched.clearFailed;
  // A lookup for the word on screen changes what it shows (a save's retry fills a meaning in while the
  // phase stays Complete), even when the controller sees no change of phase or level.
  const bool onScreen = fetched.kind == LiveSource::Fetched::Kind::Entry && fetched.index == controller_.word();
  switch (live_->apply(std::move(fetched))) {
    case LiveSource::Advance::NotFound:
      answer.ended = LiveOutcome{LiveOutcome::Kind::NotFound, live_->error()};
      return answer;
    case LiveSource::Advance::Unavailable:
      answer.ended = LiveOutcome{LiveOutcome::Kind::Unavailable, live_->error()};
      return answer;
    case LiveSource::Advance::Changed:
      answer.redraw = controller_.sourceChanged() || onScreen;
      break;
    case LiveSource::Advance::Idle:
      break;
  }
  if (const auto failed = live_->takeFailedWrite()) {  // Lexirise didn't take a level change: put it back
    controller_.levelFailed(failed->word, failed->to, nowMs);
    answer.writeFailed = true;
    answer.redraw = true;
  }
  return answer;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
