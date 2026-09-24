// End to end on the host: XHTML → CrossPoint's real ChapterHtmlSlimParser (with the parser suite's stub
// renderer: 8px per byte, 16px lines) → Pages → PageModelAdapter → SentenceBuilder. Catches any gap
// between what the builder assumes about tokens/lines and what the reader actually lays out.

#include <Epub/Page.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "lexirise/text/PageModelAdapter.h"
#include "lexirise/text/SentenceBuilder.h"

using lexipoint::text::buildPageModel;
using lexipoint::text::buildSentence;
using lexipoint::text::PageModel;
using lexipoint::text::Script;
using lexipoint::text::TokenRef;

namespace {

constexpr int kFontId = 0;
constexpr uint16_t kViewportWidth = 480;

class LayoutSentence : public ::testing::Test {
 protected:
  GfxRenderer renderer;
  CssParser css{"/tmp"};
  std::vector<std::unique_ptr<Page>> pages;
  // The parser keeps a reference to its path: it must outlive the parse.
  std::string path = (std::filesystem::temp_directory_path() / "lexipoint-layout-test.xhtml").string();

  // Lays out a body of XHTML into pages, as the reader would.
  void layout(const std::string& body, const bool extraParagraphSpacing = false) {
    {
      std::FILE* f = std::fopen(path.c_str(), "wb");
      ASSERT_NE(f, nullptr);
      const std::string doc =
          "<?xml version=\"1.0\" encoding=\"utf-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\">"
          "<body>" +
          body + "</body></html>";
      std::fwrite(doc.data(), 1, doc.size(), f);
      std::fclose(f);
    }
    pages.clear();
    ChapterHtmlSlimParser parser(
        nullptr, path, renderer, kFontId, 1.0f, extraParagraphSpacing, /*alignment=*/0, kViewportWidth,
        static_cast<uint16_t>(renderer.getScreenHeight()), /*hyphenation=*/false, /*focusReading=*/false,
        [this](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) { pages.push_back(std::move(page)); },
        /*embeddedStyle=*/true, "", "", 0, {}, nullptr, &css);
    ASSERT_TRUE(parser.parseAndBuildPages());
    ASSERT_FALSE(pages.empty());
  }

  PageModel model(const size_t page = 0) const {
    const int em = renderer.getTextAdvanceX(kFontId, "\xE5\x9B\xBD", EpdFontFamily::REGULAR);
    return buildPageModel(
        *pages.at(page),
        [this](const char* text, EpdFontFamily::Style style) { return renderer.getTextAdvanceX(kFontId, text, style); },
        em, renderer.getFontAscenderSize(kFontId));
  }

  // The sentence around the first token containing `needle`.
  std::string sentence(const std::string& needle, const Script script) const {
    const PageModel m = model();
    for (size_t l = 0; l < m.lines.size(); l++) {
      for (size_t t = 0; t < m.lines[l].tokens.size(); t++) {
        if (m.lines[l].tokens[t].find(needle) == std::string::npos) continue;
        const auto s = buildSentence(m, TokenRef{l, t}, script);
        return s ? s->text : "<none>";
      }
    }
    return "<no token " + needle + ">";
  }
};

}  // namespace

TEST_F(LayoutSentence, JapaneseParagraphsAndQuotes) {
  layout(
      "<p>「行こう。」と彼は言った。</p><p>「うん」</"
      "p><p>彼女は笑って、窓の外を見ながら長い間なにも言わなかった。そして立った。</p>");
  EXPECT_EQ(sentence("言", Script::Japanese), "「行こう。」と彼は言った。");
  EXPECT_EQ(sentence("ん", Script::Japanese), "「うん」");
  // A sentence that wraps onto the next line stays whole.
  EXPECT_EQ(sentence("窓", Script::Japanese), "彼女は笑って、窓の外を見ながら長い間なにも言わなかった。");
}

TEST_F(LayoutSentence, ExtraParagraphSpacingKeepsTheSameSentences) {
  layout(
      "<p>「行こう。」と彼は言った。</p><p>「うん」</"
      "p><p>彼女は笑って、窓の外を見ながら長い間なにも言わなかった。そして立った。</p>",
      /*extraParagraphSpacing=*/true);
  EXPECT_EQ(sentence("ん", Script::Japanese), "「うん」");
  EXPECT_EQ(sentence("窓", Script::Japanese), "彼女は笑って、窓の外を見ながら長い間なにも言わなかった。");
}

TEST_F(LayoutSentence, FuriganaLineDoesNotBreakTheSentence) {
  layout("<p>彼は<ruby>漢字<rt>かんじ</rt></ruby>の本を読みながら、ゆっくりと静かな午後の時間を過ごしていた。</p>");
  const PageModel m = model();
  ASSERT_GE(m.lines.size(), 2u);  // the sentence wraps
  const auto& last = m.lines.back();
  const auto s = buildSentence(m, TokenRef{m.lines.size() - 1, last.tokens.size() / 2}, Script::Japanese);
  ASSERT_TRUE(s);
  EXPECT_EQ(s->text, "彼は漢字の本を読みながら、ゆっくりと静かな午後の時間を過ごしていた。");  // no ruby, not cut
}

TEST_F(LayoutSentence, TerminatorRunsInsideQuotes) {
  layout("<p>「本当ですか！？」と彼は聞いた。</p><p>“你疯了吗？！”他问。</p>");
  EXPECT_EQ(sentence("本", Script::Japanese), "「本当ですか！？」と彼は聞いた。");
  EXPECT_EQ(sentence("疯", Script::Chinese), "“你疯了吗？！”");
}

TEST_F(LayoutSentence, ChineseDialogueInOneToken) {
  layout("<p>他说：</p><p>“好。”“走吧。”</p>");
  EXPECT_EQ(sentence("吧", Script::Chinese), "“走吧。”");
  EXPECT_EQ(sentence("好", Script::Chinese), "“好。”");
}

TEST_F(LayoutSentence, EnglishWrapsWithSpaces) {
  layout("<p>It was late. The old man opened the door slowly and looked out at the empty street for a while.</p>");
  EXPECT_EQ(sentence("street", Script::Latin),
            "The old man opened the door slowly and looked out at the empty street for a while.");
}
