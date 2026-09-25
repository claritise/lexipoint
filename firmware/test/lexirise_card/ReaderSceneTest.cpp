// The reader's page under the live card (ReaderScene.h): the highlight where the word stands, its strip
// line and the Context sentence, from a sentence built off the same page. Fake metrics: a CJK character
// is 26 px (Font::Page), anything else 13.

#include <gtest/gtest.h>

#include "FakeMetrics.h"
#include "lexirise/card/CardMetrics.h"
#include "lexirise/card/ReaderScene.h"

using namespace lexipoint::card;
using lexipoint::card::test::FakeMetrics;
using lexipoint::text::buildSentence;
using lexipoint::text::PageModel;
using lexipoint::text::Script;
using lexipoint::text::TextLine;

namespace {

const FakeMetrics kMetrics;
constexpr int kPad = lexipoint::card::metrics::kHighlightPadH;

// Two lines: 彼は本を / 読んだ。 (tokens one character each but 読んだ, one token of three).
struct Fixture {
  PageModel model;
  ReaderPage page;
  Fixture() {
    TextLine a;
    a.tokens = {"彼", "は", "本", "を"};
    a.startsParagraph = true;
    TextLine b;
    b.tokens = {"読んだ", "。"};
    model.lines = {a, b};
    page.lines = {{100, {{"彼", 20, 26}, {"は", 46, 26}, {"本", 72, 26}, {"を", 98, 26}}},
                  {140, {{"読んだ", 20, 78}, {"。", 98, 26}}}};
    page.pageNumber = 84;
  }
};

}  // namespace

TEST(ReaderScene, AWordIsHighlightedWhereItStands) {
  const Fixture f;
  const auto s = buildSentence(f.model, {0, 2}, Script::Japanese);  // 本
  ASSERT_TRUE(s);
  EXPECT_EQ(s->text, "彼は本を読んだ。");
  const PageScene scene = readerScene(f.page, *s, 2, 3, true, kMetrics);
  EXPECT_EQ(scene.wordOnPage, (Rect{72 - kPad, 100, 26 + 2 * kPad, 36}));
  ASSERT_EQ(scene.page.commands.size(), 2u);  // the fill, then the word in white
  EXPECT_EQ(scene.page.commands[1].text, "本");
  EXPECT_FALSE(scene.page.commands[1].black);
  EXPECT_EQ(scene.strip.tokens.size(), 4u);
  EXPECT_EQ(scene.strip.tokens[2].x, 52);  // from the line's start
  EXPECT_EQ(scene.strip.activeFirst, 2);
  EXPECT_EQ(scene.strip.activeLast, 2);
  EXPECT_EQ(scene.strip.lineNumber, 1);
  EXPECT_EQ(scene.strip.lineCount, 2);
  EXPECT_EQ(scene.sentence.text.substr(scene.sentence.markStart, scene.sentence.markLength), "本");
}

TEST(ReaderScene, PartOfATokenAndPhaseZerosFirstCharacter) {
  const Fixture f;
  const auto s = buildSentence(f.model, {1, 0}, Script::Japanese);
  ASSERT_TRUE(s);
  // Phase 0: only the tapped character (読), within the token 読んだ.
  const PageScene zero = readerScene(f.page, *s, 4, 5, true, kMetrics);
  EXPECT_EQ(zero.wordOnPage, (Rect{20 - kPad, 140, 26 + 2 * kPad, 36}));
  EXPECT_EQ(zero.page.commands[1].text, "読");
  // んだ alone: offset by 読's width.
  const PageScene tail = readerScene(f.page, *s, 5, 7, true, kMetrics);
  EXPECT_EQ(tail.wordOnPage.x, 20 + 26 - kPad);
  EXPECT_EQ(tail.page.commands[1].text, "んだ");
}

TEST(ReaderScene, AWordAcrossTokensAndLinesCoversThemAll) {
  const Fixture f;
  const auto s = buildSentence(f.model, {0, 3}, Script::Japanese);
  ASSERT_TRUE(s);
  const PageScene scene = readerScene(f.page, *s, 3, 5, true, kMetrics);  // を + 読 (as if one word)
  EXPECT_EQ(scene.page.commands.size(), 4u);                              // two pieces
  EXPECT_EQ(scene.wordOnPage.y, 100);
  EXPECT_EQ(scene.wordOnPage.bottom(), 140 + 36);  // down to the second line
  EXPECT_EQ(scene.strip.lineNumber, 1);            // the strip is the first line
  EXPECT_EQ(scene.strip.activeFirst, 3);
  // Each piece keeps its own box (P10: the card's own word is only those, not the whole of both lines).
  ASSERT_EQ(scene.wordPieces.size(), 2u);
  EXPECT_EQ(scene.wordPieces[0], (Rect{98 - kPad, 100, 26 + 2 * kPad, 36}));
  EXPECT_EQ(scene.wordPieces[1], (Rect{20 - kPad, 140, 26 + 2 * kPad, 36}));
  for (const Rect& piece : scene.wordPieces) {
    EXPECT_FALSE(piece.contains(46 + 13, 100 + 18));  // は, on the first line
    EXPECT_FALSE(piece.contains(98 + 13, 140 + 18));  // 。, on the second
  }
}

TEST(ReaderScene, NoHighlightInTheExpandedViewButTheSameBoxes) {
  const Fixture f;
  const auto s = buildSentence(f.model, {0, 2}, Script::Japanese);
  ASSERT_TRUE(s);
  const PageScene scene = readerScene(f.page, *s, 2, 3, false, kMetrics);
  EXPECT_TRUE(scene.page.commands.empty());
  EXPECT_EQ(scene.wordOnPage.x, 72 - kPad);
  EXPECT_EQ(scene.strip.activeFirst, 2);
}

namespace {

// Bold measures wider: 1.5× the fake width, so the box must follow the word's own style.
class BoldMetrics final : public lexipoint::card::TextMetrics {
 public:
  int lineHeight(const Font f) const override { return kMetrics.lineHeight(f); }
  int ascender(const Font f) const override { return kMetrics.ascender(f); }
  int width(const Font f, const std::string& t) const override { return kMetrics.width(f, t); }
  int pageWidth(const std::string& t, const uint8_t style) const override {
    return style == 1 ? kMetrics.width(Font::Page, t) * 3 / 2 : kMetrics.width(Font::Page, t);
  }
};

}  // namespace

TEST(ReaderScene, ABoldWordKeepsItsWeightAndItsBoxFits) {
  Fixture f;
  f.page.lines[0].tokens[2].style = 1;  // 本 in bold
  const auto s = buildSentence(f.model, {0, 2}, Script::Japanese);
  ASSERT_TRUE(s);
  const PageScene scene = readerScene(f.page, *s, 2, 3, true, BoldMetrics());
  EXPECT_EQ(scene.wordOnPage.w, 39 + 2 * kPad);  // 26 × 1.5
  EXPECT_EQ(scene.page.commands.at(1).pageStyle, 1);
}
