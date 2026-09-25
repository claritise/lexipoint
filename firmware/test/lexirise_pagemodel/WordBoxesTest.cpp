// WordBoxes.h: the word boxes word select hits and the reader's long-press check asks about (lookup-flow.md
// §5e: a long-press is taken only on a word).

#include <Epub/Page.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/reader/WordBoxes.h"

namespace {

constexpr int kCharWidth = 20;
constexpr int kLineHeight = 24;
constexpr int kAscender = 16;
constexpr int kMarginLeft = 10;
constexpr int kMarginTop = 30;
using word_boxes::kSlop;

int measure(const char* text, EpdFontFamily::Style) {
  int cps = 0;
  for (const char* p = text; *p; p++) {
    if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) cps++;
  }
  return cps * kCharWidth;
}

// Tokens laid out from x 0 with a one-character gap after each, on the line at y.
std::unique_ptr<PageLine> line(const std::vector<std::string>& tokens, const int y,
                               std::vector<std::string> ruby = {}) {
  std::vector<int16_t> xs;
  int pos = 0;
  for (const auto& t : tokens) {
    xs.push_back(static_cast<int16_t>(pos));
    pos += measure(t.c_str(), EpdFontFamily::REGULAR) + kCharWidth;
  }
  std::vector<EpdFontFamily::Style> styles(tokens.size(), EpdFontFamily::REGULAR);
  std::vector<uint8_t> focus(tokens.size(), 0);
  std::vector<uint16_t> suffix(tokens.size(), 0);
  auto block = std::make_unique<TextBlock>(tokens, xs, styles, focus, suffix, BlockStyle(), std::move(ruby));
  return std::make_unique<PageLine>(std::move(block), 0, static_cast<int16_t>(y));
}

bool at(const Page& page, const int x, const int y) {
  return word_boxes::anyWordAt(page, kMarginLeft, kMarginTop, kLineHeight, kAscender, x, y, measure);
}

}  // namespace

TEST(WordBoxes, AWordAndItsSlopHitTheGapBetweenThemDoesNot) {
  Page page;
  page.elements.push_back(line({"cat", "dog"}, 0));  // cat: x 10..70, dog: x 90..150; y 30..54
  EXPECT_TRUE(at(page, 40, 40));
  EXPECT_TRUE(at(page, kMarginLeft - kSlop, 40));  // the slop's left edge
  EXPECT_FALSE(at(page, kMarginLeft - kSlop - 1, 40));
  EXPECT_TRUE(at(page, 70 + kSlop - 1, 40));  // cat's right slop
  EXPECT_FALSE(at(page, 80, 40));             // the middle of the gap
  EXPECT_TRUE(at(page, 100, kMarginTop - kSlop));
  EXPECT_FALSE(at(page, 100, kMarginTop - kSlop - 1));
  EXPECT_FALSE(at(page, 100, kMarginTop + kLineHeight + kSlop));  // below the line
  EXPECT_FALSE(at(page, 400, 40));                                // past the line's end
}

TEST(WordBoxes, PunctuationAndBlankSpaceAreNoWord) {
  Page page;
  page.elements.push_back(line({"\xE2\x80\x94", "word"}, 0));  // — (General Punctuation), then a word
  page.elements.push_back(line({"next"}, 100));                // a paragraph gap in between
  EXPECT_FALSE(at(page, 20, 40));                              // on the dash
  EXPECT_TRUE(at(page, 60, 40));
  EXPECT_FALSE(at(page, 60, 90));  // the gap between the paragraphs
  EXPECT_TRUE(at(page, 20, 140));
  EXPECT_FALSE(at(page, 20, 500));  // the bottom margin
}

TEST(WordBoxes, RubyMovesTheBoxesDown) {
  Page page;
  page.elements.push_back(line({"漢字"}, 0, {"かんじ"}));
  const int shift = kAscender / 2;
  EXPECT_FALSE(at(page, 20, kMarginTop - kSlop));  // where the box would be without ruby
  EXPECT_TRUE(at(page, 20, kMarginTop + shift - kSlop));
}

TEST(WordBoxes, TheRuleWordSelectHitsWith) {
  EXPECT_TRUE(word_boxes::hit(10, 30, 60, kLineHeight, 10 - kSlop, 30 - kSlop));
  EXPECT_FALSE(word_boxes::hit(10, 30, 60, kLineHeight, 70 + kSlop, 40));
  EXPECT_TRUE(word_boxes::isSelectableToken("a"));
  EXPECT_TRUE(word_boxes::isSelectableToken("読"));
  EXPECT_FALSE(word_boxes::isSelectableToken("\xE2\x80\xA2"));  // •
  EXPECT_FALSE(word_boxes::isSelectableToken("..."));
}
