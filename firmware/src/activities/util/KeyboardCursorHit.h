#pragma once

// Where a tap on one line of the keyboard's text field puts the cursor
// (KeyboardEntryActivity::cursorPositionFromPoint). Pure; tests: test/keyboard_layers (KeyboardCursorHit).

#include <cstddef>
#include <functional>

// `x` on the line drawn from `lineStartX`, `textWidth` wide, holding the text's bytes [lineStart, lineEnd): left of the
// line is its start, right of it its end, and inside it the boundary before the character whose midpoint is right of
// `x`. `next(i)`: the byte after the character at i; `widthTo(i)`: the width of [lineStart, i).
inline size_t cursorInLine(const int x, const int lineStartX, const int textWidth, const size_t lineStart,
                           const size_t lineEnd, const std::function<size_t(size_t)>& next,
                           const std::function<int(size_t)>& widthTo) {
  if (x <= lineStartX) return lineStart;
  if (x >= lineStartX + textWidth) return lineEnd;
  int previousWidth = 0;
  for (size_t i = lineStart; i < lineEnd;) {
    const size_t after = next(i);
    const int nextWidth = widthTo(after);
    const int midpoint = lineStartX + previousWidth + (nextWidth - previousWidth) / 2;
    if (x < midpoint) return i;
    previousWidth = nextWidth;
    i = after;
  }
  return lineEnd;
}
