// The adapter against real Page / TextBlock objects: every valid text line becomes a model line (the
// index extractWords() gives WordBox), tokens keep their order, ruby never enters, images and invalid
// blocks are skipped, and paragraph starts come from the geometry.

#include <Epub/Page.h>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "lexirise/text/PageModelAdapter.h"
#include "lexirise/text/SentenceBuilder.h"

using lexipoint::text::buildPageModel;
using lexipoint::text::buildSentence;
using lexipoint::text::PageModel;
using lexipoint::text::Script;

namespace {

constexpr int kEm = 20;
constexpr int kAdvance = 30;
constexpr int kAscender = 16;

// Every token is kEm wide per codepoint-ish (bytes / 3 for CJK is close enough here).
int measure(const char* text, EpdFontFamily::Style) {
  int cps = 0;
  for (const char* p = text; *p; p++) {
    if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) cps++;
  }
  return cps * kEm;
}

// A line of CJK tokens laid out left to right from x.
std::unique_ptr<PageLine> line(const std::vector<std::string>& tokens, const int row, const int x = 0,
                               std::vector<std::string> ruby = {}) {
  std::vector<int16_t> xs;
  int pos = 0;
  for (const auto& t : tokens) {
    xs.push_back(static_cast<int16_t>(pos));
    pos += measure(t.c_str(), EpdFontFamily::REGULAR);
  }
  std::vector<EpdFontFamily::Style> styles(tokens.size(), EpdFontFamily::REGULAR);
  std::vector<uint8_t> focus(tokens.size(), 0);
  std::vector<uint16_t> suffix(tokens.size(), 0);
  auto block = std::make_unique<TextBlock>(tokens, xs, styles, focus, suffix, BlockStyle(), std::move(ruby));
  return std::make_unique<PageLine>(std::move(block), static_cast<int16_t>(x), static_cast<int16_t>(row * kAdvance));
}

// 20 characters fill the 400px column.
std::vector<std::string> fullLine(const char* ch = "あ") { return std::vector<std::string>(20, ch); }

}  // namespace

TEST(PageModelAdapter, LinesTokensAndOrder) {
  Page page;
  page.elements.push_back(line({"彼", "は", "来", "た。"}, 0));
  page.elements.push_back(line({"猫", "が", "鳴", "い", "た。"}, 1));
  const PageModel model = buildPageModel(page, measure, kEm, kAscender);
  ASSERT_EQ(model.lines.size(), 2u);
  EXPECT_EQ(model.lines[0].tokens, (std::vector<std::string>{"彼", "は", "来", "た。"}));
  EXPECT_EQ(model.lines[1].tokens[2], "鳴");
}

TEST(PageModelAdapter, RubyIsNeverInTheText) {
  Page page;
  page.elements.push_back(line({"漢", "字", "を", "読", "む。"}, 0, 0, {"かん", "じ", "", "よ", ""}));
  const PageModel model = buildPageModel(page, measure, kEm, kAscender);
  const auto s = buildSentence(model, {0, 3}, Script::Japanese);
  ASSERT_TRUE(s);
  EXPECT_EQ(s->text, "漢字を読む。");
  EXPECT_EQ(s->tapOffset, 3u);
}

TEST(PageModelAdapter, ImagesAndRulesAreSkippedWithoutBreakingTheSentence) {
  Page page;
  page.elements.push_back(line(fullLine(), 0));
  page.elements.push_back(std::make_unique<PageImage>(std::make_unique<ImageBlock>("a.png", "a.png", 10, 10), 0, 40));
  page.elements.push_back(line({"い", "た。"}, 2));
  const PageModel model = buildPageModel(page, measure, kEm, kAscender);
  ASSERT_EQ(model.lines.size(), 2u);  // line indexes count text lines only, like extractWords()
  EXPECT_EQ(model.lines[1].tokens[0], "い");
}

TEST(PageModelAdapter, ParagraphStartsFromGeometry) {
  Page page;
  page.elements.push_back(line(fullLine(), 0));
  page.elements.push_back(line({"「", "そ", "う", "」"}, 1));  // short: the paragraph ends here
  page.elements.push_back(line({"「", "行", "こ", "う", "」"}, 2));
  page.elements.push_back(line({"\xE3\x80\x80", "彼", "は"}, 3));  // ideographic-space indent
  const PageModel model = buildPageModel(page, measure, kEm, kAscender);
  ASSERT_EQ(model.lines.size(), 4u);
  EXPECT_FALSE(model.lines[0].startsParagraph);
  EXPECT_FALSE(model.lines[1].startsParagraph);
  EXPECT_TRUE(model.lines[2].startsParagraph);
  EXPECT_TRUE(model.lines[3].startsParagraph);
}
