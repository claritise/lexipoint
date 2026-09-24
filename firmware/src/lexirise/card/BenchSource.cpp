#if LEXIRISE

#include "BenchSource.h"

#include "BenchPage.h"
#include "lexirise/LexiriseConfig.h"
#include "lexirise/text/Utf8Prefix.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

std::string BenchSource::pendingText() const {
  const CardWord& w = book_.words[book_.start];
  return std::string(text::utf8FirstChars(w.surface.empty() ? w.word : w.surface, 1));
}

void BenchSource::open(const unsigned long nowMs) {
  phase_ = Phase::Pending;
  phaseADueMs_ = nowMs + config::kBenchPhaseAMs;
  phaseBDueMs_ = nowMs + config::kBenchPhaseBMs;
}

void BenchSource::focus(int, const unsigned long nowMs) {
  // Stepping re-runs only dictionary/lookup: the word is known at once, the translation follows.
  phase_ = Phase::Analyzed;
  phaseADueMs_ = nowMs;
  phaseBDueMs_ = nowMs + config::kBenchPhaseBMs - config::kBenchPhaseAMs;
}

bool BenchSource::tick(const unsigned long nowMs) {
  if (phase_ == Phase::Pending && timing::reached(nowMs, phaseADueMs_)) {
    // B close behind A: skip A's refresh (popup-ui.md §2, ~0.5 s per partial refresh).
    phase_ = timing::reached(nowMs + config::kPhaseMergeMs, phaseBDueMs_) ? Phase::Complete : Phase::Analyzed;
    return true;
  }
  if (phase_ == Phase::Analyzed && timing::reached(nowMs, phaseBDueMs_)) {
    phase_ = Phase::Complete;
    return true;
  }
  return false;
}

std::optional<unsigned long> BenchSource::nextDueMs() const {
  if (phase_ == Phase::Pending) return phaseADueMs_;
  if (phase_ == Phase::Analyzed) return phaseBDueMs_;
  return std::nullopt;
}

PageScene BenchSource::scene(const int index, const bool highlight, const TextMetrics& metrics,
                             const int highlightCodepoints) const {
  return bench::layoutPage(book_, index, low_, highlight, metrics, highlightCodepoints);
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
