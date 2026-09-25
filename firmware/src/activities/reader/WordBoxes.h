#pragma once

// The screen boxes of a page's selectable words, as word select lays them out and hits them. One rule for
// DictionaryWordSelectActivity (extractWords / wordAt) and, with LEXIRISE, the reader's long-press check
// (pressOnWord: a long-press is taken only on a word), so the two can't disagree. Pure: the caller measures.
// Tests: test/lexirise_pagemodel/WordBoxesTest.cpp.

#include <Epub/Page.h>

#include <cctype>
#include <cstdint>

namespace word_boxes {

constexpr int kSlop = 4;  // matches the highlight box (+2) plus finger error

// A token is selectable when it has an ASCII alphanumeric or a non-ASCII
// codepoint outside U+2000-U+206F (dashes, bullets and other General
// Punctuation that appear as standalone tokens are not words).
inline bool isSelectableToken(const char* text) {
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(text); *p != 0; p++) {
    if (*p < 0x80) {
      if (std::isalnum(*p)) return true;
    } else if (*p == 0xE2 && (p[1] == 0x80 || p[1] == 0x81)) {
      if (p[2] == 0) break;  // truncated sequence: skipping would step past the NUL
      p += 2;                // skip the 3-byte General Punctuation codepoint
    } else {
      return true;
    }
  }
  return false;
}

// The top of a line's word boxes on screen (its ruby, if any, sits above them).
inline int lineTop(const PageLine& line, const TextBlock& block, const int marginTop, const int ascender) {
  return line.yPos + marginTop + block.getRubyShift(ascender);
}

// The left edge of token i's box on screen.
inline int wordLeft(const PageLine& line, const TextBlock& block, const uint16_t i, const int marginLeft) {
  return line.xPos + block.wordXpos(i) + marginLeft;
}

// Whether y is on a line of boxes whose top is boxTop, with finger-sized slop.
inline bool onLineBand(const int boxTop, const int lineHeight, const int y) {
  return y >= boxTop - kSlop && y < boxTop + lineHeight + kSlop;
}

// Whether (x, y) hits a word box, with finger-sized slop. Grown boxes never overlap, at worst they touch,
// so the first hit wins.
inline bool hit(const int boxX, const int boxTop, const int boxWidth, const int lineHeight, const int x, const int y) {
  return onLineBand(boxTop, lineHeight, y) && x >= boxX - kSlop && x < boxX + boxWidth + kSlop;
}

// Whether (x, y) hits any selectable word of `page`, drawn at the margins. `measure(text, style)` gives a
// word's width; only the words of the lines under y are measured.
template <typename Measure>
bool anyWordAt(const Page& page, const int marginLeft, const int marginTop, const int lineHeight, const int ascender,
               const int x, const int y, Measure&& measure) {
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;
    const int top = lineTop(*line, *block, marginTop, ascender);
    if (!onLineBand(top, lineHeight, y)) continue;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* text = block->wordText(i);
      if (!isSelectableToken(text)) continue;
      if (hit(wordLeft(*line, *block, i, marginLeft), top, measure(text, block->wordStyle(i)), lineHeight, x, y)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace word_boxes
