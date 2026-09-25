#pragma once

// The bench's card source (P4): the reference's fixtures (BenchFixtures), their page (BenchPage), and
// the loading phases played on a timer (config::kBenchPhaseAMs / kBenchPhaseBMs), with phase B close
// behind A merged into one refresh (popup-ui.md §2). Stepped past its last word it goes on into a "next
// sentence" after config::kBenchNextSentenceMs (P9: the fixtures have one sentence, so it is that sentence again:
// words from wordCount() on are the fixtures' again), so lxctl card-smoke can drive the wait and the jump.
// Pure; tests: test/lexirise_card.

#include "BenchFixtures.h"
#include "CardSource.h"

namespace lexipoint::card {

class BenchSource final : public CardSource {
 public:
  BenchSource(const BenchBook& book, bool low) : book_(book), low_(low) {}

  const BenchBook& book() const { return book_; }

  int wordCount() const override { return static_cast<int>(book_.words.size()) * (repeated_ ? 2 : 1); }
  int startWord() const override { return book_.start; }
  const CardWord& word(const int index) const override { return book_.words[fixture(index)]; }
  Level savedLevel(const int index) const override { return book_.saved[fixture(index)]; }
  Phase phase(int) const override { return phase_; }  // the focused word's
  std::string pendingText() const override;
  int pageNumber() const override { return book_.pageNumber; }
  bool demoActions() const override { return true; }

  // Phase B with no meaning (the offline row): for the P6 goldens and previews, not the bench's timer.
  void unanswered() { phase_ = Phase::Unanswered; }

  void open(unsigned long nowMs) override;
  void focus(int index, unsigned long nowMs) override;
  bool tick(unsigned long nowMs) override;
  std::optional<unsigned long> nextDueMs() const override;
  bool extend(unsigned long nowMs) override;
  bool extending() const override { return extending_; }

  PageScene scene(int index, bool highlight, const TextMetrics& metrics, int highlightCodepoints) const override;

 private:
  const BenchBook& book_;
  bool low_;
  Phase phase_ = Phase::Pending;
  unsigned long phaseADueMs_ = 0;
  unsigned long phaseBDueMs_ = 0;
  bool repeated_ = false;   // the "next sentence" came: the fixtures' words twice over
  bool extending_ = false;  // it's on its way, due at extendDueMs_
  unsigned long extendDueMs_ = 0;

  bool tickPhases(unsigned long nowMs);  // the focused word's phases A and B
  size_t fixture(const int index) const { return static_cast<size_t>(index) % book_.words.size(); }
};

}  // namespace lexipoint::card
