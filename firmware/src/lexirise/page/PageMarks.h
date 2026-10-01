#pragma once

// V9a's page marks (docs/v0.2/page-annotations.md §2 A1, "V9a decisions", signed off by claritise 2026-09-29;
// reference/v9a-annotations.html): every word on an analyzed page not saved (or at level 0) gets a solid underline,
// particles included; a tracked or learning word (1-2) a dotted one; a fresh or known word (3-4), an ignored one (the
// reader's list, C17) or one suspended in Lexirise, none; punctuation never. Drawn over the page after it renders
// (§3: no reflow), from the page's cached analysis (V7b: which word is where) and the vocab mirror (V7a: what the
// reader knows, by the saved-state rule), placed by the same span → glyph walk the card's highlight uses
// (card::pieceBoxes over text::buildPageText's characters). The same marks decide A3's stepping on a card
// (CardController). Pure; tests: test/lexirise_page/PageMarksTest.cpp.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "MarkRule.h"
#include "PageAnalysis.h"
#include "PageSentences.h"
#include "lexirise/card/ReaderScene.h"
#include "lexirise/settings/IgnoredWords.h"

namespace lexipoint::page {

// A word's state as the marks read it.
struct WordState {
  bool saved = false;
  int proficiency = 0;  // 0-4 (Lexirise's; the card's T L F K are 1-4)
  bool suspended = false;
  bool ignored = false;
};
Mark markFor(const WordState& word);

// Where the marks' states come from: the mirror (MirrorView on the device), and the reader's ignore list. The base
// answers "the page's snapshot stands" and "nothing ignored" (the bench's).
class MarkSources {
 public:
  virtual ~MarkSources() = default;
  virtual MirrorSays mirror(uint32_t /*entryId*/) const { return {}; }
  virtual bool ignored(const IgnoredKey& /*key*/) const { return false; }
};

// The device's sources (page/ReaderMarks.cpp draws with them): the vocab mirror by the saved-state rule (MirrorView),
// under the open card's level changes the mirror doesn't hold yet (VocabStore::unsentState: the reader's own newest
// choice, so the page under the card shows a level on its next frame, its Undo too), and the reader's ignore list; all
// asked from memory.
class StoreSources final : public MarkSources {
 public:
  StoreSources(Language language, uint64_t analyzedMs, vocab::VocabStore& mirror, IgnoredWordStore& ignoredWords)
      : language_(language), store_(mirror), view_(language, analyzedMs, mirror), ignoredWords_(ignoredWords) {}
  MirrorSays mirror(uint32_t entryId) const override;
  bool ignored(const IgnoredKey& key) const override { return ignoredWords_.contains(key); }

 private:
  Language language_;
  vocab::VocabStore& store_;
  MirrorView view_;
  IgnoredWordStore& ignoredWords_;
};

// A word-like occurrence's span in the page's text (UTF-16 units) and its mark (Mark::None ones are left out).
struct SpanMark {
  uint32_t start = 0;
  uint32_t end = 0;
  Mark mark = Mark::None;
  bool operator==(const SpanMark&) const = default;
};

// The state of occurrence `occ`: its lemma's entry first (a saved 食べる marks 食べた), then its own, as the card reads
// a word's saved state (lookup::cardFor); each entry as the mirror says, else the page's snapshot.
WordState wordStateOf(const PageAnalysis& page, const Occ& occ, const MarkSources& sources);
// Every word-like occurrence's mark, in the page's order (Mark::None ones left out).
std::vector<SpanMark> pageMarks(const PageAnalysis& page, const MarkSources& sources);

// A mark on one line of a word: from its first character's x to its last character's advance end on that line
// (the justification's gaps between them included: each CJK character is a token of its own), the line's top and
// height (card::pieceBoxes, merged per line). A word across a line end has one run on each line.
struct MarkRun {
  card::Rect box;
  Mark mark = Mark::None;
  bool operator==(const MarkRun&) const = default;
};
std::vector<MarkRun> markRuns(const card::ReaderPage& page, const text::BuiltSentence& pageText,
                              const std::vector<SpanMark>& marks, const card::TextMetrics& metrics);

// The black rectangles a run draws, its line's baseline at box.y + `ascender`, the mark's top `belowBaseline` under it
// (markBelowBaseline of the font's em): a solid underline kMarkThickness tall, or kMarkDot squares every
// kMarkDotPitch; both pulled in kMarkInset at each end, so two marked words side by side read as two. None for a run
// too narrow for one.
void markFills(const MarkRun& run, int ascender, int belowBaseline, std::vector<card::Rect>& out);

}  // namespace lexipoint::page
