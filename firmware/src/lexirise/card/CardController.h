#pragma once

#include <optional>

// What the card does on input and over time (popup-ui.md §2-3): the phases, word stepping, taps on its
// targets, Home, toasts. Pure (time is passed in); the activity (LexiriseCardActivity) is glue around it.
// The bench (P4) plays the phases on a timer over fixtures; P5 drives them from the lookup.
// Tests: test/lexirise_card.

#include <vector>

#include "BenchFixtures.h"
#include "CardModel.h"
#include "DisplayList.h"

namespace lexipoint::card {

enum class Effect : uint8_t { None, Redraw, Close };

struct Outcome {
  Effect effect = Effect::None;
  bool readingChanged = false;  // persist the Japanese reading (settings.md: `reading`)
};

class CardController {
 public:
  CardController(const BenchBook& book, ReadingMode reading, bool low, CardStrings strings = {});

  void open(unsigned long nowMs);  // phase 0 on the book's start word
  bool tick(unsigned long nowMs);  // phases and the toast's end; true: redraw
  // When tick() next has something to do (a phase or the toast's end); none: nothing pending.
  std::optional<unsigned long> nextDueMs() const;
  bool step(int direction, unsigned long nowMs);     // side buttons: previous / next word, stopping at the ends
  Outcome tap(const Hit* hit, unsigned long nowMs);  // nullptr: outside the card
  Outcome home();                                    // expanded → card; card → close

  const BenchBook& book() const { return book_; }
  int word() const { return word_; }
  bool low() const { return low_; }
  const CardWord& currentWord() const { return book_.words[word_]; }
  const CardState& state() const { return state_; }  // without the page parts (the activity adds them)
  const CardStrings& strings() const { return strings_; }
  // Phase 0 highlights the tapped character only; then the whole word.
  int highlightCodepoints() const { return state_.phase == Phase::Pending ? 1 : 0; }

 private:
  void startPhases(Phase first, unsigned long nowMs, unsigned long toB);
  void showToast(std::string text, unsigned long nowMs, bool undo = false);
  void clearToast();
  void syncWord();

  const BenchBook& book_;
  CardStrings strings_;
  bool low_;
  int word_ = 0;
  std::vector<Level> levels_;
  CardState state_;
  unsigned long phaseADueMs_ = 0;
  unsigned long phaseBDueMs_ = 0;
  unsigned long toastUntilMs_ = 0;
  // What the toast's Undo reverts: that word back to that level (None: the save is removed).
  int undoWord_ = -1;
  Level undoLevel_ = Level::None;
};

}  // namespace lexipoint::card
