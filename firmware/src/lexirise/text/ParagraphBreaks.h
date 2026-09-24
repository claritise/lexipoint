#pragma once

// Which lines of a laid-out page start a paragraph. CrossPoint's Page doesn't keep that (every line is
// its own TextBlock), so it's read off the geometry: the line before ended short, the vertical gap
// grew, the line is indented (a first-line indent, not a hanging one), it starts with an ideographic
// space, or its block style changed. A furigana line is taller by its ruby shift, which isn't a gap.
// Pure; tests: test/lexirise_sentence/ParagraphAndTapTest.cpp.

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
  int rubyShift = 0;                        // extra height a furigana line takes above its text
};

// One flag per line. `em` is the width of one full-width character in the page's font; without one
// (em <= 0) only the signals that don't need it count.
inline std::vector<bool> paragraphStarts(const std::vector<LineShape>& lines, const int em) {
  std::vector<bool> starts(lines.size(), false);
  if (lines.empty()) return starts;

  int contentLeft = lines[0].left;
  int contentRight = lines[0].right;
  for (const LineShape& line : lines) {
    contentLeft = std::min(contentLeft, line.left);
    contentRight = std::max(contentRight, line.right);
  }
  // The gap from line i-1 to line i, less the furigana height line i-1 carries.
  const auto gap = [&lines](const size_t i) { return lines[i].top - lines[i - 1].top - lines[i - 1].rubyShift; };
  // The usual line advance: the median of those gaps.
  std::vector<int> advances;
  for (size_t i = 1; i < lines.size(); i++) {
    if (gap(i) > 0) advances.push_back(gap(i));
  }
  int advance = 0;
  if (!advances.empty()) {
    std::nth_element(advances.begin(), advances.begin() + advances.size() / 2, advances.end());
    advance = advances[advances.size() / 2];
  }

  const bool useEm = em > 0;
  // Indented from the column. Wide furigana can push a line right, so ruby lines don't count.
  const auto indented = [&](const size_t i) {
    return useEm && lines[i].rubyShift == 0 &&
           lines[i].left - contentLeft >= static_cast<int>(config::kParagraphIndentEm * em);
  };
  // A first-line indent: only this line is indented (a hanging indent indents the lines after the first).
  const auto firstLineIndent = [&](const size_t i) {
    return indented(i) && (i + 1 >= lines.size() || !indented(i + 1)) && (i == 0 || !indented(i - 1));
  };
  for (size_t i = 0; i < lines.size(); i++) {
    const LineShape& line = lines[i];
    bool start = line.startsWithIdeographicSpace || firstLineIndent(i);
    if (i > 0) {
      const LineShape& prev = lines[i - 1];
      start = start || (useEm && contentRight - prev.right >= static_cast<int>(config::kParagraphShortLineEm * em)) ||
              (advance > 0 && gap(i) > static_cast<int>(config::kParagraphGapFactor * advance)) ||
              line.blockInset != prev.blockInset || line.alignment != prev.alignment;
    }
    starts[i] = start;
  }
  return starts;
}

}  // namespace lexipoint::text
