#include <gtest/gtest.h>

#include "lexirise/text/ParagraphBreaks.h"
#include "lexirise/text/TapContext.h"

using lexipoint::Language;
using lexipoint::Settings;
using lexipoint::text::BookLanguage;
using lexipoint::text::describeTap;
using lexipoint::text::LanguageSource;
using lexipoint::text::LineShape;
using lexipoint::text::PageModel;
using lexipoint::text::paragraphStarts;
using lexipoint::text::Script;

namespace {
constexpr int kEm = 20;
constexpr int kAdvance = 30;
// A full-width line of a justified column from x=0 to x=400.
LineShape full(const int row) { return {0, 400, row * kAdvance}; }
}  // namespace

TEST(ParagraphBreaks, ShortLineEndsTheParagraph) {
  std::vector<LineShape> lines = {full(0), full(1), {0, 200, 2 * kAdvance}, full(3), full(4)};
  EXPECT_EQ(paragraphStarts(lines, kEm), (std::vector<bool>{false, false, false, true, false}));
}

TEST(ParagraphBreaks, GapIndentIdeographicSpaceAndStyle) {
  std::vector<LineShape> gap = {full(0), full(1), {0, 400, 2 * kAdvance + 20}, full(4)};
  gap[3].top = gap[2].top + kAdvance;
  EXPECT_TRUE(paragraphStarts(gap, kEm)[2]);

  std::vector<LineShape> indent = {full(0), {20, 400, kAdvance}, full(2)};
  EXPECT_EQ(paragraphStarts(indent, kEm), (std::vector<bool>{false, true, false}));

  std::vector<LineShape> ideographic = {full(0), full(1)};
  ideographic[1].startsWithIdeographicSpace = true;
  EXPECT_TRUE(paragraphStarts(ideographic, kEm)[1]);

  std::vector<LineShape> blockquote = {full(0), full(1)};
  blockquote[1].blockInset = 40;
  EXPECT_TRUE(paragraphStarts(blockquote, kEm)[1]);

  std::vector<LineShape> center = {full(0), full(1)};
  center[1].alignment = 2;
  EXPECT_TRUE(paragraphStarts(center, kEm)[1]);
}

TEST(ParagraphBreaks, ThePageTopOnlyByIndent) {
  EXPECT_FALSE(paragraphStarts({full(0), full(1)}, kEm)[0]);
  EXPECT_TRUE(paragraphStarts({{20, 400, 0}, full(1)}, kEm)[0]);
  EXPECT_TRUE(paragraphStarts({}, kEm).empty());
}

TEST(TapContext, MetadataPicksThePunctuation) {
  // 」「 splits dialogue only with Japanese/Chinese rules; ”“ only with Chinese.
  const PageModel page{{{{"他", "说", "“好", "”", "“走", "吧", "”"}, true}}};
  const Settings s;
  const auto zh = describeTap(page, {0, 5}, BookLanguage("zh", std::nullopt), s);
  EXPECT_EQ(zh.script, Script::Chinese);
  ASSERT_TRUE(zh.sentence);
  EXPECT_EQ(zh.sentence->text, "“走吧”");
  EXPECT_EQ(zh.language.language, Language::Chinese);
  EXPECT_EQ(zh.language.source, LanguageSource::Metadata);
}

TEST(TapContext, NoMetadataDecidesFromTheSentenceThenRecuts) {
  Settings s;
  s.defaultLanguage = Language::Chinese;
  const PageModel page{{{{"他", "说", "“好", "”", "“走", "吧", "”"}, true}}};
  const auto t = describeTap(page, {0, 5}, BookLanguage("und", std::nullopt), s);
  EXPECT_EQ(t.language.language, Language::Chinese);  // Han, no kana: the default
  EXPECT_EQ(t.language.source, LanguageSource::DefaultForHan);
  EXPECT_EQ(t.script, Script::Chinese);
  ASSERT_TRUE(t.sentence);
  EXPECT_EQ(t.sentence->text, "“走吧”");  // recut with Chinese rules

  const PageModel english{{{{"The", "cat", "sat."}, true}}};
  const auto en = describeTap(english, {0, 1}, BookLanguage("", std::nullopt), s);
  EXPECT_FALSE(en.language.language);
  EXPECT_EQ(en.script, Script::Latin);
  ASSERT_TRUE(en.sentence);
  EXPECT_EQ(en.sentence->text, "The cat sat.");

  const PageModel empty{{{{"​"}, true}}};
  EXPECT_FALSE(describeTap(empty, {0, 0}, BookLanguage("", std::nullopt), s).sentence);
}
