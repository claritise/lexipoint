#pragma once

// The reader's page under the live card (P5): the word highlighted where it stands, its line for the
// strips and the sentence for the Context tab (popup-ui.md §1-2, D17). The page itself is drawn by the
// reader's own code; this scene only adds the highlight over it. Pure; tests: test/lexirise_card.

#include <cstdint>
#include <string>
#include <vector>

#include "CardSource.h"
#include "lexirise/text/SentenceBuilder.h"

namespace lexipoint::card {

// One laid-out token as drawn: its text and where (screen x, the line's y as drawText takes it).
struct PageToken {
  std::string text;
  int x = 0;
  int width = 0;
  uint8_t style = 0;  // EpdFontFamily::Style: the highlight measures and draws it in its own weight
};

// The page's text lines, indexed like the page model (text::PageModel) the sentence was built from.
struct ReaderLine {
  int y = 0;
  std::vector<PageToken> tokens;
};

struct ReaderPage {
  std::vector<ReaderLine> lines;
  int pageNumber = 0;  // 0: unknown
};

// Where a built text's units [start, end) stand on the page: one box per piece (a word can span tokens and lines), the
// text's own x and advance width (no highlight padding), the line's y (as drawText takes it) and the page's line
// height. The card's highlight (readerScene) and V9's page marks (page/PageMarks.h) both place themselves by it.
struct PieceBox {
  text::TokenRef token;
  uint32_t first = 0;  // the piece's codepoints [first, last] in its token
  uint32_t last = 0;
  Rect box;
};
std::vector<PieceBox> pieceBoxes(const ReaderPage& page, const text::BuiltSentence& sentence, uint32_t start,
                                 uint32_t end, const TextMetrics& metrics);

// The scene with the sentence's units [start, end) active (a word's charStart..charEnd; in phase 0 the
// tapped character's). highlight: invert them on the page (card view).
PageScene readerScene(const ReaderPage& page, const text::BuiltSentence& sentence, uint32_t start, uint32_t end,
                      bool highlight, const TextMetrics& metrics);

}  // namespace lexipoint::card
