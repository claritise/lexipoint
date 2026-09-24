#pragma once

#include <optional>

// What the card does on input and over time (popup-ui.md §2-3): word stepping, taps on its targets,
// Home, toasts, and the phases its CardSource reports (the bench's timer, P4; the lookup, P5). Pure (time
// is passed in); the activity (LexiriseCardActivity) is glue around it. Tests: test/lexirise_card.

#include <vector>

#include "CardModel.h"
#include "CardSource.h"
#include "DisplayList.h"

namespace lexipoint::card {

enum class Effect : uint8_t { None, Redraw, Close };

// A level the user set on a word (T L F K, Undo, ⋯ Undo save): the card shows it at once, and the source
// makes it so in Lexirise (LiveSource: save, level change or removal; the bench has nothing to send).
struct LevelChange {
  int word = 0;
  Level from = Level::None;
  Level to = Level::None;
  // Not sent before this (millis): a change with an Undo toast waits out its window, so an Undo in it
  // costs nothing and a tap on the toast never waits behind a network call.
  unsigned long readyAtMs = 0;
};

struct Outcome {
  Effect effect = Effect::None;
  bool readingChanged = false;       // persist the Japanese reading (settings.md: `reading`)
  std::vector<LevelChange> changes;  // in the order they were made (a batch can hold T, then its Undo)
};

class CardController {
 public:
  CardController(CardSource& source, ReadingMode reading, CardStrings strings = {});

  void open(unsigned long nowMs);  // phase 0 on the source's start word
  bool tick(unsigned long nowMs);  // the source's phases and the toast's end; true: redraw
  // When tick() next has something to do (a phase or the toast's end); none: nothing pending.
  std::optional<unsigned long> nextDueMs() const;
  // The source changed outside tick() (a lookup answered): true when the card shows something new.
  bool sourceChanged();
  bool step(int direction, unsigned long nowMs);     // side buttons: previous / next word, stopping at the ends
  Outcome tap(const Hit* hit, unsigned long nowMs);  // nullptr: outside the card
  Outcome home();                                    // expanded → card; card → close
  // Lexirise refused a level change (or couldn't be reached): the word (and the sentence's other
  // occurrences of it) go back to `level`, with a toast wherever the card is.
  void levelFailed(int word, Level level, unsigned long nowMs);

  const CardSource& source() const { return source_; }
  int word() const { return word_; }
  // How many side-button steps the card has taken: a frame drawn at another count showed another word
  // (ShownTargets), even when the index is the same one (phase 0's word 0 became the tapped word).
  int steps() const { return steps_; }
  const CardWord& currentWord() const;
  const CardState& state() const { return state_; }  // without the page parts (composeFrame adds them)
  const CardStrings& strings() const { return strings_; }
  // Phase 0 highlights the tapped character only; then the whole word.
  int highlightCodepoints() const { return state_.phase == Phase::Pending ? 1 : 0; }

 private:
  void showToast(std::string text, unsigned long nowMs, bool undo = false);
  void clearToast();
  Outcome setLevel(Level level, const char* toastPrefix, bool undo, unsigned long nowMs);
  bool syncWord();  // true: something shown changed
  bool hasWord() const { return word_ < source_.wordCount(); }

  CardSource& source_;
  CardStrings strings_;
  int word_ = 0;
  int steps_ = 0;
  std::vector<Level> levels_;  // each word's level: the source's when it arrived, then the user's changes
  CardState state_;
  unsigned long toastUntilMs_ = 0;
  // What the toast's Undo reverts: that word back to that level (None: the save is removed).
  int undoWord_ = -1;
  Level undoLevel_ = Level::None;
};

}  // namespace lexipoint::card
