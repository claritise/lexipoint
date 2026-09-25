#pragma once

// Where the card's words come from, and the page under it: the bench's fixtures on a timer (BenchSource,
// P4) or a lookup over the network (LiveSource, P5). CardController asks; the source answers. Pure.

#include <optional>
#include <string>
#include <vector>

#include "CardModel.h"
#include "DisplayList.h"

namespace lexipoint::card {

// Why a call to Lexirise failed, as the card tells it (offline-and-errors.md §3): a level change (a Retry
// toast) or the next sentence (a plain toast). LiveSource.h callFailure() maps an api::ApiError to it.
enum class CallFailure : uint8_t {
  Network,      // no WiFi, a timeout, 5xx, a bad answer
  KeyRejected,  // 401/403: Lexirise is off until reboot or a new key
  RateLimited,  // 429
};

// The page under the card for the active word (D17): drawn over the page backdrop in card view (the
// bench's is the whole page; the reader's is only the highlight), plus the word's box on the page, its
// line for the strips and the sentence for the Context tab.
struct PageScene {
  DisplayList page;
  Rect wordOnPage;               // every piece of the word, in one box (is any of it covered: D17)
  std::vector<Rect> wordPieces;  // each piece's own box (a word can break over lines)
  StripLine strip;
  MarkedText sentence;  // the word marked
};

class CardSource {
 public:
  virtual ~CardSource() = default;

  // The words known so far (0 in phase 0, before the sentence is analyzed), the one the card opens on,
  // and each word with its level when the card opened.
  virtual int wordCount() const = 0;
  virtual int startWord() const = 0;
  virtual const CardWord& word(int index) const = 0;
  virtual Level savedLevel(int index) const = 0;
  // The word's phase: Pending until the sentence is analyzed, then A, B or B′ as its lookup goes.
  virtual Phase phase(int index) const = 0;
  virtual std::string pendingText() const = 0;  // phase 0: the tapped character
  virtual int pageNumber() const = 0;           // "This book · p. 84"; 0: none
  // The ⋯ tab's actions after Undo save (save the sentence, ignore, look up later) are v0.2 (C17): the
  // bench plays the reference's toasts; a live card says they aren't there yet.
  virtual bool demoActions() const { return false; }
  // The words that are one Lexirise entry with `index` (the same lemma twice in a sentence), itself
  // included: a level set on one is the level of all of them.
  virtual std::vector<int> sameWord(const int index) const { return {index}; }

  // The card opened (on startWord()) or stepped to `index` at `nowMs`: that word's lookup starts.
  virtual void open(unsigned long nowMs) = 0;
  virtual void focus(int index, unsigned long nowMs) = 0;
  // Stepped past the last word: start on the page's next sentence (lookup-flow.md §6, P9). Its words come at
  // wordCount() onwards once it's analyzed. False when there's none (the page ends) or the source can't.
  virtual bool extend(unsigned long /*nowMs*/) { return false; }
  // A sentence extend() started is still being analyzed (its words aren't there yet).
  virtual bool extending() const { return false; }
  // Why the last sentence extend() started brought no words; nullopt when the page simply ended.
  virtual std::optional<CallFailure> extendFailure() const { return std::nullopt; }
  // Time passed: true when something the card shows changed. nextDueMs: when tick() next has work.
  virtual bool tick(unsigned long nowMs) = 0;
  virtual std::optional<unsigned long> nextDueMs() const = 0;

  // The page with word `index` active. highlight: invert it (card view); highlightCodepoints > 0: only
  // its first characters (phase 0: the tapped one; popup-ui.md §2, it then grows to the word).
  virtual PageScene scene(int index, bool highlight, const TextMetrics& metrics, int highlightCodepoints) const = 0;
};

}  // namespace lexipoint::card
