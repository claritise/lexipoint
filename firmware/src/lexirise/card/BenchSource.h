#pragma once

// The bench's card source (P4): the reference's fixtures (BenchFixtures), their page (BenchPage), and
// the loading phases played on a timer (config::kBenchPhaseAMs / kBenchPhaseBMs), with phase B close
// behind A merged into one refresh (popup-ui.md §2). Pure; tests: test/lexirise_card.

#include "BenchFixtures.h"
#include "CardSource.h"

namespace lexipoint::card {

class BenchSource final : public CardSource {
 public:
  BenchSource(const BenchBook& book, bool low) : book_(book), low_(low) {}

  const BenchBook& book() const { return book_; }

  int wordCount() const override { return static_cast<int>(book_.words.size()); }
  int startWord() const override { return book_.start; }
  const CardWord& word(const int index) const override { return book_.words[index]; }
  Level savedLevel(const int index) const override { return book_.saved[index]; }
  Phase phase(int) const override { return phase_; }  // the focused word's
  std::string pendingText() const override;
  int pageNumber() const override { return book_.pageNumber; }
  bool demoActions() const override { return true; }

  void open(unsigned long nowMs) override;
  void focus(int index, unsigned long nowMs) override;
  bool tick(unsigned long nowMs) override;
  std::optional<unsigned long> nextDueMs() const override;

  PageScene scene(int index, bool highlight, const TextMetrics& metrics, int highlightCodepoints) const override;

 private:
  const BenchBook& book_;
  bool low_;
  Phase phase_ = Phase::Pending;
  unsigned long phaseADueMs_ = 0;
  unsigned long phaseBDueMs_ = 0;
};

}  // namespace lexipoint::card
