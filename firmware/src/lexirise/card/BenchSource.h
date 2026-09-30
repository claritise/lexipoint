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
  BenchSource(const BenchBook& book, bool low)
      : book_(book), low_(low), words_(book.words), ignored_(book.words.size(), false) {}

  const BenchBook& book() const { return book_; }

  int wordCount() const override { return static_cast<int>(book_.words.size()) * (repeated_ ? 2 : 1); }
  int startWord() const override { return book_.start; }
  const CardWord& word(const int index) const override { return words_[fixture(index)]; }
  Level savedLevel(const int index) const override { return book_.saved[fixture(index)]; }
  Phase phase(int) const override { return phase_; }  // the focused word's
  std::string pendingText() const override;
  int pageNumber() const override { return book_.pageNumber; }
  bool demoActions() const override { return true; }
  // The reader's ignore list, in memory only (C17, V6: the ⋯ row's Ignore and Undo ignore play as on a live card).
  bool ignored(int index) const override { return ignored_[fixture(index)]; }
  bool setIgnored(int index, bool ignored) override;
  bool keepsIgnoreList() const override { return true; }
  // The word's sentence: the page's context lines to the full stop after the word, the rest of the page after it, a
  // sentence at a time.
  std::optional<SentenceForSave> sentenceForSave(int index) const override;

  // Phase B with no meaning (the offline row): for the P6 goldens and previews, not the bench's timer.
  void unanswered() { phase_ = Phase::Unanswered; }
  // Word `index` with other readings (C15, V6), and every word without a key to ignore it by: for the V6 goldens.
  void setAlso(const int index, std::vector<AlsoReading> also) { words_[fixture(index)].also = std::move(also); }
  void dropIgnoreKeys() { keyless_ = true; }

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
  std::vector<CardWord> words_;  // the book's, with the goldens' changes (setAlso)
  std::vector<bool> ignored_;    // per fixture word
  bool keyless_ = false;
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
