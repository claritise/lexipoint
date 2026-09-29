#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <set>
#include <string>

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class

namespace {

class ChapterHtmlSlimParserTest : public ::testing::TestWithParam<const char*> {
 protected:
  std::string filepath = "unused.xhtml";
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               0,
                               static_cast<uint16_t>(renderer.getScreenWidth()),
                               static_cast<uint16_t>(renderer.getScreenHeight()),
                               false,
                               false,
                               {},
                               true,
                               "",
                               "",
                               0,
                               {},
                               nullptr,
                               &cssParser};

  void SetUp() override { parser.currentTextBlock = std::make_unique<ParsedText>(false); }
};

// V8 R5: a paragraph whose direction is rtl (dir="rtl", CSS direction: rtl) keeps its words in logical order, left to
// right, placed as CrossPoint's MiniBidi placed a line with no Hebrew or Arabic: at the right margin (the default
// alignment and Justify), or centred. The stub font is 8 px a byte with 4 px spaces; extra paragraph spacing keeps
// the first line unindented.
namespace {

struct LaidLine {
  std::vector<int> x;
  std::vector<int> width;
};

std::vector<LaidLine> layOutRtl(const GfxRenderer& renderer, const std::vector<std::string>& words,
                                const uint16_t viewport, const CssTextAlign alignment, const bool alignDefined,
                                const int16_t textIndent = 0, const std::string& lastWordRuby = "") {
  BlockStyle style;
  style.isRtl = true;
  style.directionDefined = true;
  style.alignment = alignment;
  style.textAlignDefined = alignDefined;
  style.textIndent = textIndent;
  style.textIndentDefined = textIndent != 0;
  // Extra paragraph spacing drops a defined indent: without it, the indent is used.
  ParsedText text(/*extraParagraphSpacing=*/textIndent == 0, false, false, style);
  for (const std::string& w : words) text.addWord(w, EpdFontFamily::REGULAR);
  if (!lastWordRuby.empty()) text.setRubyForWordAt(words.size() - 1, lastWordRuby);
  std::vector<LaidLine> lines;
  text.layoutAndExtractLines(renderer, 0, viewport, [&](std::unique_ptr<TextBlock> line, auto) {
    LaidLine laid;
    for (uint16_t i = 0; i < line->wordCount(); i++) {
      laid.x.push_back(line->wordXpos(i));
      laid.width.push_back(renderer.getTextAdvanceX(0, line->wordText(i), EpdFontFamily::REGULAR));
    }
    lines.push_back(laid);
  });
  return lines;
}

void expectLeftToRightEndingAt(const LaidLine& line, const int right) {
  ASSERT_FALSE(line.x.empty());
  for (size_t i = 1; i < line.x.size(); i++) EXPECT_GT(line.x[i], line.x[i - 1]) << "word " << i;
  EXPECT_EQ(line.x.back() + line.width.back(), right);
}

}  // namespace

TEST_F(ChapterHtmlSlimParserTest, AnRtlParagraphOfLatinWordsRunsLeftToRightToTheRightMargin) {
  const auto lines = layOutRtl(renderer, {"aa", "bb", "cc"}, 200, CssTextAlign::Left, /*alignDefined=*/false);
  ASSERT_EQ(lines.size(), 1u);
  expectLeftToRightEndingAt(lines[0], 200);
  EXPECT_EQ(lines[0].x, (std::vector<int>{144, 164, 184}));
}

TEST_F(ChapterHtmlSlimParserTest, AnRtlParagraphOfCjkWordsRunsLeftToRightToTheRightMargin) {
  const auto lines = layOutRtl(renderer, {"\xE6\x97\xA5\xE6\x9C\xAC", "\xE8\xAA\x9E"}, 200, CssTextAlign::Left, false);
  ASSERT_EQ(lines.size(), 1u);
  expectLeftToRightEndingAt(lines[0], 200);
}

TEST_F(ChapterHtmlSlimParserTest, AJustifiedRtlParagraphFillsItsLinesAndPushesTheLastRight) {
  const auto lines =
      layOutRtl(renderer, {"aaaa", "bbbb", "cccc", "dddd", "ee"}, 100, CssTextAlign::Justify, /*alignDefined=*/true);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].x, (std::vector<int>{0, 68}));  // stretched from margin to margin
  expectLeftToRightEndingAt(lines[0], 100);
  EXPECT_EQ(lines[1].x, (std::vector<int>{12, 48, 84}));  // the last line isn't stretched: it sits at the right margin
  expectLeftToRightEndingAt(lines[1], 100);
}

TEST_F(ChapterHtmlSlimParserTest, AnRtlParagraphWithAnExplicitLeftAlignmentStartsAtTheLeft) {
  const auto lines = layOutRtl(renderer, {"aa", "bb", "cc"}, 200, CssTextAlign::Left, /*alignDefined=*/true);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].x, (std::vector<int>{0, 20, 40}));
}

TEST_F(ChapterHtmlSlimParserTest, AJustifiedRtlParagraphKeepsItsIndentAtTheRightEnd) {
  const auto lines = layOutRtl(renderer, {"aaaa", "bbbb", "cccc", "dddd", "ee"}, 100, CssTextAlign::Justify,
                               /*alignDefined=*/true, /*textIndent=*/12);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].x, (std::vector<int>{0, 56}));  // stretched to 100 - 12: the indent is at the right end
  expectLeftToRightEndingAt(lines[0], 88);
  expectLeftToRightEndingAt(lines[1], 100);  // later lines: no indent
}

TEST_F(ChapterHtmlSlimParserTest, AnRtlParagraphWithRubyRunsLeftToRightInsideTheMargins) {
  const auto lines = layOutRtl(renderer, {"aa", "bb", "cc"}, 200, CssTextAlign::Left, false, 0, "cccccc");
  ASSERT_EQ(lines.size(), 1u);
  for (size_t i = 1; i < lines[0].x.size(); i++) EXPECT_GT(lines[0].x[i], lines[0].x[i - 1]);
  EXPECT_GE(lines[0].x.front(), 0);
  EXPECT_LE(lines[0].x.back() + lines[0].width.back(), 200);
}

TEST_F(ChapterHtmlSlimParserTest, ACentredRtlParagraphIsCentredLeftToRight) {
  const auto lines = layOutRtl(renderer, {"aa", "bb", "cc"}, 200, CssTextAlign::Center, /*alignDefined=*/true);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].x, (std::vector<int>{72, 92, 112}));  // (200 - 56) / 2
}

TEST_F(ChapterHtmlSlimParserTest, RubySurvivesPartialParagraphExtraction) {
  ParsedText text(false);
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);
  text.setRubyForWordAt(2, "c");
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 20,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        EXPECT_TRUE(line->getRubyTexts().empty());
      },
      false);
  EXPECT_EQ(lines, 1u);
  const size_t retainedWords = text.size();
  ASSERT_GT(retainedWords, 0u);
  ASSERT_LT(retainedWords, 3u);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_EQ(line->getRubyTexts().size(), retainedWords);
    EXPECT_EQ(line->getRubyTexts().back(), "c");
    for (size_t i = 0; i + 1 < retainedWords; ++i) EXPECT_TRUE(line->getRubyTexts()[i].empty());
  });
  EXPECT_EQ(lines, 2u);
}

TEST_F(ChapterHtmlSlimParserTest, UnequalTableCellsAndRubySurvivePageBreaks) {
  parser.viewportWidth = 240;
  parser.viewportHeight = 32;
  parser.tableRowCells.reserve(2);
  std::multiset<std::string> expected;
  for (int column = 0; column < 2; ++column) {
    auto cell = std::make_unique<ParsedText>(false);
    for (int index = 0; index < (column == 0 ? 30 : 3); ++index) {
      const auto word = std::string(column == 0 ? "left" : "right") + std::to_string(index);
      expected.insert(word);
      cell->addWord(word, EpdFontFamily::REGULAR);
    }
    if (column == 0) cell->setRubyGroupAt(0, 2, "reading");
    parser.tableRowCells.push_back(std::move(cell));
  }
  std::multiset<std::string> actual;
  unsigned pages = 0;
  unsigned rubyLines = 0;
  auto inspect = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ++pages;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = *line.getBlock();
      ASSERT_TRUE(block.valid());
      EXPECT_LE(element->yPos + 16 + block.getRubyShift(12), parser.viewportHeight);
      rubyLines += block.hasRuby();
      for (uint16_t word = 0; word < block.wordCount(); ++word) actual.insert(block.wordText(word));
    }
  };
  parser.completePageFn = inspect;
  parser.finishTableRow();
  ASSERT_NE(parser.currentPage, nullptr);
  inspect(std::move(parser.currentPage), 0, 0, 0);
  EXPECT_GT(pages, 2u);
  EXPECT_EQ(rubyLines, 1u);
  EXPECT_EQ(actual, expected);
  for (const auto& lines : parser.tableCellLines) EXPECT_TRUE(lines.empty());
}

TEST_F(ChapterHtmlSlimParserTest, PageImageDeserializeRejectsMissingImageBlock) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-missing-image-cache.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const int16_t coordinates[] = {0, 0};
    output.write(coordinates, sizeof(coordinates));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  EXPECT_EQ(PageImage::deserialize(input), nullptr);
}

TEST_P(ChapterHtmlSlimParserTest, KeepsCssVerticalAlignAndInternalLinkMetadata) {
  const char* verticalAlign = GetParam();
  const char* expectedHref = "#note-target";
  const XML_Char* attributes[] = {"href", expectedHref, "style", verticalAlign, nullptr};

  ChapterHtmlSlimParser::startElement(&parser, "a", attributes);
  const uint8_t linkId = parser.currentFootnoteLinkId;
  ASSERT_NE(linkId, 0u);
  ChapterHtmlSlimParser::characterData(&parser, "1", 1);
  ChapterHtmlSlimParser::endElement(&parser, "a");

  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  const auto style = parser.currentTextBlock->getWordStyleAt(0);
  const auto expectedStyle =
      std::string(verticalAlign).find("super") != std::string::npos ? EpdFontFamily::SUP : EpdFontFamily::SUB;
  EXPECT_NE(static_cast<uint8_t>(style) & static_cast<uint8_t>(expectedStyle), 0u);

  ASSERT_EQ(parser.pendingFootnotes.size(), 1u);
  const FootnoteEntry& footnote = parser.pendingFootnotes.front().second;
  EXPECT_STREQ(footnote.href, expectedHref);
  ASSERT_EQ(parser.currentTextBlock->wordLinkIds.size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->wordLinkIds.front(), linkId);
  EXPECT_TRUE(parser.currentTextBlock->linkTargetMatches(linkId, expectedHref));
}

INSTANTIATE_TEST_SUITE_P(CssVerticalAlign, ChapterHtmlSlimParserTest,
                         ::testing::Values("vertical-align: super", "vertical-align: sub"));

TEST_F(ChapterHtmlSlimParserTest, ParagraphWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, HeaderWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "h1", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, SpanWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Before ", 7);
  ChapterHtmlSlimParser::startElement(&parser, "span", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);
  ChapterHtmlSlimParser::endElement(&parser, "span");
  ChapterHtmlSlimParser::characterData(&parser, " After ", 7);

  ASSERT_EQ(parser.currentTextBlock->size(), 2);
  ASSERT_EQ(parser.currentTextBlock->words[0], "Before");
  ASSERT_EQ(parser.currentTextBlock->words[1], "After");
}

TEST_F(ChapterHtmlSlimParserTest, DivWithHiddenAttributeContentShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "div", attributes);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

}  // namespace
