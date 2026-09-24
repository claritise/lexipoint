#pragma once

// Which lines of a laid-out page start a paragraph. CrossPoint's Page doesn't keep that (every line is
// its own TextBlock), so it's read off the geometry: the line before ended short, the vertical gap
// grew, the line is indented, it starts with an ideographic space, or its block style changed. Pure;
// tests: test/lexirise_sentence/ParagraphBreaksTest.cpp.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::text {

struct LineShape {
  int left = 0;                             // x of the first glyph
  int right = 0;                            // x just past the last glyph
  int top = 0;                              // the line's y
  bool startsWithIdeographicSpace = false;  // "　" (U+3000): the Japanese paragraph indent
  int blockInset = 0;                       // the block's left margin + padding
  int alignment = 0;                        // the block's text-align
};

// One flag per line. `em` is the width of one full-width character in the page's font.
inline std::vector<bool> paragraphStarts(const std::vector<LineShape>& lines, const int em) {
  std::vector<bool> starts(lines.size(), false);
  if (lines.empty()) return starts;

  int contentLeft = lines[0].left;
  int contentRight = lines[0].right;
  for (const LineShape& line : lines) {
    contentLeft = std::min(contentLeft, line.left);
    contentRight = std::max(contentRight, line.right);
  }
  // The usual line advance: the median of the gaps between consecutive lines.
  std::vector<int> advances;
  for (size_t i = 1; i < lines.size(); i++) {
    if (lines[i].top > lines[i - 1].top) advances.push_back(lines[i].top - lines[i - 1].top);
  }
  int advance = 0;
  if (!advances.empty()) {
    std::nth_element(advances.begin(), advances.begin() + advances.size() / 2, advances.end());
    advance = advances[advances.size() / 2];
  }

  const auto indented = [&](const LineShape& line) {
    return line.left - contentLeft >= static_cast<int>(config::kParagraphIndentEm * em);
  };
  for (size_t i = 0; i < lines.size(); i++) {
    const LineShape& line = lines[i];
    bool start = line.startsWithIdeographicSpace;
    if (i == 0) {
      start = start || indented(line);  // the page top: only an indent can tell
    } else {
      const LineShape& prev = lines[i - 1];
      start = start || contentRight - prev.right >= static_cast<int>(config::kParagraphShortLineEm * em) ||
              (advance > 0 && line.top - prev.top > static_cast<int>(config::kParagraphGapFactor * advance)) ||
              (indented(line) && !indented(prev)) || line.blockInset != prev.blockInset ||
              line.alignment != prev.alignment;
    }
    starts[i] = start;
  }
  return starts;
}

}  // namespace lexipoint::text
