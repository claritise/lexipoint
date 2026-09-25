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

TEST(ParagraphBreaks, FuriganaLineHeightIsNotAGap) {
  // Line 1 carries furigana: the next line sits rubyShift lower, which isn't paragraph spacing.
  std::vector<LineShape> lines = {full(0), full(1), full(2), full(3)};
  lines[1].rubyShift = 16;
  lines[2].top += 16;
  lines[3].top += 16;
  EXPECT_EQ(paragraphStarts(lines, kEm), (std::vector<bool>{false, false, false, false}));
}

TEST(ParagraphBreaks, HangingIndentIsNotAParagraphPerLine) {
  // First line at the column, the rest indented (text-indent < 0): one paragraph.
  std::vector<LineShape> lines = {full(0), {20, 400, kAdvance}, {20, 400, 2 * kAdvance}, {20, 400, 3 * kAdvance}};
  EXPECT_EQ(paragraphStarts(lines, kEm), (std::vector<bool>{false, false, false, false}));
}

TEST(ParagraphBreaks, WithoutAnEmOnlyUnmeasuredSignalsCount) {
  std::vector<LineShape> lines = {full(0), {0, 100, kAdvance}, full(2)};  // would be "short" with an em
  lines[2].startsWithIdeographicSpace = false;
  EXPECT_EQ(paragraphStarts(lines, 0), (std::vector<bool>{false, false, false}));
  lines[2].startsWithIdeographicSpace = true;
  EXPECT_TRUE(paragraphStarts(lines, 0)[2]);
}

TEST(ParagraphBreaks, ASpaceAfterAPauseIsNotAnIndent) {
  // ？　 wrapping: the 　 lands at a line start but the paragraph goes on.
  std::vector<LineShape> lines = {full(0), full(1)};
  lines[0].endsWithPause = true;
  lines[1].startsWithIdeographicSpace = true;
  EXPECT_FALSE(paragraphStarts(lines, kEm)[1]);
  lines[0].endsWithPause = false;  // after anything else it is the paragraph indent
  EXPECT_TRUE(paragraphStarts(lines, kEm)[1]);
}

TEST(ParagraphBreaks, EllipsisIsNotAPause) {
  // A paragraph ending in a bare …… (nearly full line) then a 　-indented one: still a paragraph start.
  std::vector<LineShape> lines = {full(0), full(1)};
  lines[1].startsWithIdeographicSpace = true;
  EXPECT_TRUE(paragraphStarts(lines, kEm)[1]);
}

TEST(ParagraphBreaks, UsualAdvanceSurvivesDialoguePages) {
  // Extra paragraph spacing on a dialogue page: most gaps are paragraph gaps (40), line gaps (30) rarer.
  std::vector<LineShape> lines = {{0, 400, 0},   {0, 400, 40},  {0, 400, 80}, {0, 400, 110},
                                  {0, 400, 140}, {0, 400, 180}, {0, 400, 220}};
  const auto starts = paragraphStarts(lines, 0);  // no em: only the gap signal can fire
  EXPECT_EQ(starts, (std::vector<bool>{false, true, true, false, false, true, true}));
}

TEST(ParagraphBreaks, OneWrappedLineIsEnoughForTheAdvance) {
  // Dialogue page, extra paragraph spacing: a single line step (16), the rest paragraph gaps (24).
  std::vector<LineShape> lines = {{0, 400, 0}, {0, 400, 16}, {0, 400, 40}, {0, 400, 64}, {0, 400, 88}};
  EXPECT_EQ(paragraphStarts(lines, 0), (std::vector<bool>{false, false, true, true, true}));
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
  EXPECT_EQ(zh.language.detected, Language::Chinese);
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

TEST(TapContext, ASwitchedOffLanguageStillPicksItsPunctuation) {
  Settings s;
  s.japanese.enabled = false;
  const PageModel page{{{{"「う", "ん」", "「行", "く」"}, true}}};
  const auto t = describeTap(page, {0, 2}, BookLanguage("ja", std::nullopt), s);
  EXPECT_FALSE(t.language.language);  // not sent
  EXPECT_EQ(t.language.detected, Language::Japanese);
  EXPECT_EQ(t.script, Script::Japanese);  // but cut as Japanese: 」「 splits
  ASSERT_TRUE(t.sentence);
  EXPECT_EQ(t.sentence->text, "「行く」");
}

TEST(TapContext, TheNextSentenceIsDescribedLikeATap) {
  const PageModel page{{{{"彼", "は", "来", "た。", "雨", "が", "降", "る。"}, true}}};
  const Settings s;
  const BookLanguage untagged("", std::nullopt);
  const auto first = describeTap(page, {0, 1}, untagged, s);
  ASSERT_TRUE(first.sentence);
  EXPECT_EQ(first.sentence->text, "彼は来た。");
  const auto next = lexipoint::text::describeNextSentence(page, first, untagged, s);
  ASSERT_TRUE(next.sentence);
  EXPECT_EQ(next.sentence->text, "雨が降る。");
  EXPECT_EQ(next.sentence->tapOffset, 0u);
  EXPECT_EQ(next.language.language, s.fallbackLanguage());  // Han-only: decided again, from its own text
  EXPECT_FALSE(lexipoint::text::describeNextSentence(page, next, untagged, s).sentence);  // the page ends
  EXPECT_FALSE(lexipoint::text::describeNextSentence(page, lexipoint::text::TapContext{}, untagged, s).sentence);
}

TEST(TapContext, TheNextSentenceIsCutWithItsOwnLanguagesRules) {
  // Japanese, then Chinese-only text in a book that doesn't say (Han-only → Chinese here). ”“ only splits
  // dialogue with Chinese rules, so it's cut again once its language is known.
  const PageModel page{{{{"彼", "は", "来", "た。"}, true}, {{"他", "说", "“好", "”", "“走", "吧", "”"}, true}}};
  Settings s;
  s.defaultLanguage = Language::Chinese;
  const BookLanguage untagged("", std::nullopt);
  const auto first = describeTap(page, {0, 1}, untagged, s);
  ASSERT_EQ(first.script, Script::Japanese);
  const auto next = lexipoint::text::describeNextSentence(page, first, untagged, s);
  ASSERT_TRUE(next.sentence);
  EXPECT_EQ(next.script, Script::Chinese);
  EXPECT_EQ(next.language.language, Language::Chinese);
  EXPECT_EQ(next.sentence->text, "他说“好”");
}
