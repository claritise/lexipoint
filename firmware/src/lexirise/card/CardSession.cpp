#include "CardSession.h"

#if LEXIPOINT_DEV_HARNESS
#include <Logging.h>
#endif

#include <algorithm>
#include <iterator>

#include "lexirise/util/Timing.h"

namespace lexipoint::card {

Outcome CardSession::handleInput(const unsigned long nowMs, TapsSeen* seen) {
  if (!input_.empty()) lastActivityMs_ = nowMs;
  Outcome outcome = card::handleInput(controller_, targets_, input_, nowMs, seen);
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
  switch (live_->apply(std::move(fetched), nowMs)) {
    case LiveSource::Advance::NotFound:
      answer.ended = LiveOutcome{LiveOutcome::Kind::NotFound, live_->error()};
      return answer;
    case LiveSource::Advance::Unavailable:
      answer.ended = LiveOutcome{LiveOutcome::Kind::Unavailable, live_->error()};
      return answer;
    case LiveSource::Advance::Changed:
      answer.redraw = controller_.sourceChanged(nowMs);
      break;
    case LiveSource::Advance::Idle:
      break;
  }
  // The word on screen shows something new even when the controller sees no change of phase or level (a lookup's
  // meaning, a copy's "Met before" after a write, an earlier cut's word once the next cut joins): LiveSource::apply.
  if (live_->takeShownChanged()) answer.redraw = true;
  if (const auto failed = live_->takeFailedWrite()) {  // Lexirise didn't take a level change: put it back
    controller_.levelFailed(failed->back.word, failed->back.to, nowMs, callFailure(failed->error), failed->back.from,
                            failed->retryAfterS);
    answer.writeFailed = true;
    answer.redraw = true;
  }
  return answer;
}

CardSession::Persisted CardSession::persistIgnores(const Outcome& outcome) const {
  Persisted done;
  IgnoredWordStore* store = live_ ? live_->ignoredStore() : nullptr;
  if (!store) return done;  // the bench keeps no list (nor does a card given none: it can't ignore)
  const size_t n = outcome.ignores.size();
  std::vector<std::optional<IgnoredKey>> keys;
  keys.reserve(n);
  std::transform(outcome.ignores.begin(), outcome.ignores.end(), std::back_inserter(keys),
                 [this](const IgnoreChange& change) { return live_->ignoreKey(change.word); });
  done.failed.reserve(n);
  done.evicted.reserve(n);
  // In the order they were made (the file stays newest last); a change a later one for the same key overrides is
  // skipped (an Ignore and its Undo together: nothing written), unless it carries back a pushed-out key (an Undo's
  // restore): that key must return even when the word is ignored again after it (which then pushes out the oldest,
  // that same key, again, for its own Undo).
  for (size_t i = 0; i < n; i++) {
    const IgnoreChange& change = outcome.ignores[i];
    const std::optional<IgnoredKey>& key = keys[i];
    if (key && !change.restore &&
        std::find(keys.begin() + static_cast<std::ptrdiff_t>(i) + 1, keys.end(), key) != keys.end()) {
      continue;
    }
    using Write = IgnoredWordStore::Write;
    std::optional<IgnoredKey> evicted;
    const Write wrote = key ? store->write(*key, change.ignored, &evicted, change.restore) : Write::Failed;
    const bool ok = wrote != Write::Failed;
#if LEXIPOINT_DEV_HARNESS
    // For the device check (device-checks.md V5): the key (`ja:<id>`, or a dictionary form) and what the SD card got.
    LOG_INF("LXCARD", "ignore %s %s %s", key ? ignoredKeyText(*key).c_str() : "-", change.ignored ? "on" : "off",
            wrote == Write::Written ? "written" : (ok ? "unchanged" : "failed"));
#endif
    if (!ok) done.failed.push_back(change);
    if (evicted) done.evicted.emplace_back(change.word, std::move(*evicted));
  }
  return done;
}

bool CardSession::idleFor(const unsigned long nowMs, const bool rendering, const bool touching,
                          const std::optional<unsigned long> cardDueMs, const unsigned long idleMs) const {
  return live_ && !rendering && !touching && !cardDueMs && !drawPending_ && input_.empty() && !hasWork(nowMs) &&
         !live_->hasPendingWrites() && timing::reached(nowMs, lastActivityMs_ + idleMs);
}

bool CardSession::shouldFetchDeck(const unsigned long nowMs, const bool rendering, const bool touching,
                                  const std::optional<unsigned long> cardDueMs) const {
  return deckAllowed_ && idleFor(nowMs, rendering, touching, cardDueMs, config::kDeckIdleMs) && live_->hasDeckWork();
}

bool CardSession::shouldFlushFiles(const unsigned long nowMs, const bool rendering, const bool touching,
                                   const std::optional<unsigned long> cardDueMs) const {
  return idleFor(nowMs, rendering, touching, cardDueMs, config::kDeckIdleMs) &&
         (live_->mirrorFlushDue() || live_->lookupFlushDue());
}

CardSession::IdleStep CardSession::nextIdleStep(const unsigned long nowMs, const bool rendering, const bool touching,
                                                const std::optional<unsigned long> cardDueMs,
                                                const uint32_t epochS) const {
  if (shouldFetchDeck(nowMs, rendering, touching, cardDueMs)) return IdleStep::Deck;
  if (shouldFlushFiles(nowMs, rendering, touching, cardDueMs)) return IdleStep::Flush;
  if (shouldProbeVocab(nowMs, rendering, touching, cardDueMs)) return IdleStep::Probe;
  if (shouldFetchVocab(nowMs, rendering, touching, cardDueMs, epochS)) return IdleStep::Vocab;
  return IdleStep::None;
}

bool CardSession::shouldProbeVocab(const unsigned long nowMs, const bool rendering, const bool touching,
                                   const std::optional<unsigned long> cardDueMs) const {
  return idleFor(nowMs, rendering, touching, cardDueMs, config::kVocabCardProbeIdleMs) && live_->hasProbeWork(nowMs);
}

bool CardSession::mirrorChanged(const std::vector<uint32_t>& changedEntries) {
  if (!live_) return false;
  bool redraw = false;
  for (const int w : live_->takeMirrorChanges(changedEntries)) redraw = controller_.savedLevelChanged(w) || redraw;
  return live_->takeShownChanged() || redraw;
}

bool CardSession::shouldFetchVocab(const unsigned long nowMs, const bool rendering, const bool touching,
                                   const std::optional<unsigned long> cardDueMs, const uint32_t epochS) const {
  return idleFor(nowMs, rendering, touching, cardDueMs, config::kVocabIdleMs) &&
         !(deckAllowed_ && live_->hasDeckWork()) && live_->hasVocabWork(nowMs, epochS);
}

vocab::PageApplied CardSession::applyVocab(const std::optional<vocab::PageCall>& call, const unsigned long nowMs,
                                           const uint32_t epochS) {
  if (!live_ || !call) return {};
  vocab::PageApplied applied = live_->applyVocab(*call, nowMs, epochS);
  lastActivityMs_ = nowMs;  // the next page waits its own idle time too
  return applied;
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
