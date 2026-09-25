#pragma once

// The page under the bench card: the reference's own page (.pg: 22/20 px padding, line-height 1.95), in
// the reader's font (sanctioned deviation 3: page text follows the reader settings). It gives each word
// its box on the page (D17), its strip line and the context sentence. Bench only: on a real page these
// come from the laid-out Page (P5). Pure; tests: test/lexirise_card.

#include "BenchFixtures.h"
#include "CardModel.h"
#include "CardSource.h"
#include "DisplayList.h"

namespace lexipoint::card::bench {

using Scene = PageScene;

// A token (or the part of one that fits) on a wrapped row.
struct PlacedToken {
  std::string text;
  int word = -1;         // as BenchToken::word
  uint32_t firstCp = 0;  // where this part starts in its token, in codepoints
  int x = 0;             // from the row's left
  int width = 0;
};
using PlacedRow = std::vector<PlacedToken>;

// The book's lines wrapped at `width` (P11): the reference's lines, set in the device's wider page font, can
// run past the panel. Each line keeps its tokens while they fit, then goes on to a new row; a token wider
// than a whole row is broken between codepoints. A line that fits comes out as it went in.
std::vector<PlacedRow> wrapLines(const std::vector<BenchLine>& lines, int width, const TextMetrics& metrics);

// The page for `book` with `word` active; `low`: the sentence low on the page ("Low on page", D17).
// `highlightCodepoints` > 0 inverts only the word's first that many characters (phase 0: the tapped one;
// popup-ui.md §2, the highlight then grows to the word).
Scene layoutPage(const BenchBook& book, int word, bool low, bool highlight, const TextMetrics& metrics,
                 int highlightCodepoints = 0);

// The card state for a bench scene: the view, the word's saved level, and the page's strip / sentence.
CardState benchState(const BenchBook& book, int word, const Scene& scene);

}  // namespace lexipoint::card::bench
