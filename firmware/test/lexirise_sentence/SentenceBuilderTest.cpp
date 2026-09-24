// SentenceBuilder cases from sentence-extraction.md §4 and languages.md §7.

#include <Utf8.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/text/SentenceBuilder.h"

using lexipoint::text::buildSentence;
using lexipoint::text::BuiltSentence;
using lexipoint::text::PageModel;
using lexipoint::text::Script;
using lexipoint::text::TextLine;
using lexipoint::text::TokenRef;
using lexipoint::text::utf16Length;

namespace {

bool isCjkish(const uint32_t cp) {
  return utf8IsCjkCodepoint(cp) || cp == 0x201C || cp == 0x201D || cp == 0x2018 || cp == 0x2019 || cp == 0x2026;
}
// Glued to the token before (CrossPoint's no-break-before punctuation) / to the token after.
bool gluesLeft(const uint32_t cp) {
  for (const uint32_t c : {0x3002u, 0x3001u, 0xFF0Cu, 0xFF0Eu, 0xFF01u, 0xFF1Fu, 0x300Du, 0x300Fu, 0xFF09u, 0x3011u,
                           0x300Bu, 0x201Du, 0x2019u, 0x2026u, 0xFF1Au, 0xFF1Bu}) {
    if (cp == c) return true;
  }
  return false;
}
bool gluesRight(const uint32_t cp) {
  for (const uint32_t c : {0x300Cu, 0x300Eu, 0xFF08u, 0x3010u, 0x300Au, 0x201Cu, 0x2018u}) {
    if (cp == c) return true;
  }
  return false;
}

// Splits one line the way CrossPoint lays it out: one CJK character per token (with the punctuation
// that can't start or end a line glued on), Latin words whole, spaces dropped.
TextLine layout(const std::string& text, const bool startsParagraph = false) {
  TextLine line;
  line.startsParagraph = startsParagraph;
  std::string latin;
  bool glueNext = false;
  const auto flushLatin = [&] {
    if (!latin.empty()) line.tokens.push_back(latin);
    latin.clear();
  };
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    std::string ch;
    utf8AppendCodepoint(cp, ch);
    if (cp == ' ') {
      flushLatin();
      continue;
    }
    if (!isCjkish(cp)) {
      if (glueNext && !line.tokens.empty() && latin.empty()) {
        latin = line.tokens.back();  // an opener glues onto the next word
        line.tokens.pop_back();
      }
      glueNext = false;
      latin += ch;
      continue;
    }
    flushLatin();
    if ((gluesLeft(cp) && !line.tokens.empty() && !glueNext) || (glueNext && !line.tokens.empty())) {
      line.tokens.back() += ch;
    } else {
      line.tokens.push_back(ch);
    }
    glueNext = gluesRight(cp);
  }
  flushLatin();
  return line;
}

// Finds the token containing `needle` (the first match at or after `from` in reading order).
TokenRef find(const PageModel& page, const std::string& needle, size_t skip = 0) {
  for (size_t l = 0; l < page.lines.size(); l++) {
    for (size_t t = 0; t < page.lines[l].tokens.size(); t++) {
      if (page.lines[l].tokens[t].find(needle) != std::string::npos) {
        if (skip == 0) return {l, t};
        skip--;
      }
    }
  }
  ADD_FAILURE() << "no token contains " << needle;
  return {};
}

BuiltSentence build(const PageModel& page, const std::string& tapped, const Script script = Script::Japanese,
                    const size_t skip = 0) {
  const auto result = buildSentence(page, find(page, tapped, skip), script);
  EXPECT_TRUE(result.has_value());
  return result.value_or(BuiltSentence{});
}

// The tapped token as the server will see it: the substring at [tapOffset, tapOffset + tapLength).
std::string tapped(const BuiltSentence& s) {
  std::string out;
  uint32_t units = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(s.text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    if (units >= s.tapOffset && units < s.tapOffset + s.tapLength) utf8AppendCodepoint(cp, out);
    units += cp > 0xFFFF ? 2 : 1;
  }
  return out;
}

}  // namespace

TEST(SentenceJa, PlainSentenceInTheMiddleOfALine) {
  const PageModel page{{layout("前の文だ。彼は東京へ行った。次の文。", true)}};
  const auto s = build(page, "京");
  EXPECT_EQ(s.text, "彼は東京へ行った。");
  EXPECT_EQ(s.tapOffset, 3u);
  EXPECT_EQ(s.tapLength, 1u);
  EXPECT_FALSE(s.truncatedLeft);
  EXPECT_FALSE(s.truncatedRight);
}

TEST(SentenceJa, SentenceAcrossThreeLines) {
  const PageModel page{{layout("雨だ。昨日の夜、彼", true), layout("は古い本を読みなが"), layout("ら眠った。翌朝")}};
  const auto s = build(page, "読");
  EXPECT_EQ(s.text, "昨日の夜、彼は古い本を読みながら眠った。");
  EXPECT_EQ(tapped(s), "読");
}

TEST(SentenceJa, TapOnTheTerminatorOrAClosingBracket) {
  const PageModel page{{layout("「行こう。」と彼は言った。そして歩いた。", true)}};
  EXPECT_EQ(build(page, "た。").text, "「行こう。」と彼は言った。");  // 」と: the quote belongs to it
  const PageModel tte{{layout("「いいよ。」って答えた。", true)}};
  EXPECT_EQ(build(tte, "答").text, "「いいよ。」って答えた。");
  const PageModel noQuotative{{layout("「行こう。」彼は歩き出した。", true)}};
  EXPECT_EQ(build(noQuotative, "歩").text, "彼は歩き出した。");
  // A closer on its own token after a terminator belongs to the sentence it closes.
  PageModel split{{{{"雨", "だ。", "」", "彼", "は", "来", "た。"}, true}}};
  const auto s = build(split, "」");
  EXPECT_EQ(s.text, "雨だ。」");
  EXPECT_EQ(tapped(s), "」");
}

TEST(SentenceJa, DialogueParagraphsWithoutTerminators) {
  const PageModel page{
      {layout("「そうだね」", true), layout("「でも、行かない」", true), layout("彼女は笑った。", true)}};
  EXPECT_EQ(build(page, "行").text, "「でも、行かない」");
  // Two lines of dialogue on one line of text: 」「 is a break too.
  const PageModel sameLine{{layout("「うん」「わかった」と答えた。", true)}};
  EXPECT_EQ(build(sameLine, "わ").text, "「わかった」と答えた。");
  EXPECT_EQ(build(sameLine, "う").text, "「うん」");
}

TEST(SentenceJa, EllipsisEndsOnlyBeforeACloserOrParagraphEnd) {
  const PageModel page{{layout("それは……」と言いかけた。", true)}};
  EXPECT_EQ(build(page, "言").text, "それは……」と言いかけた。");  // ……」と: one sentence
  const PageModel cut{{layout("それは……」彼は黙った。", true)}};
  EXPECT_EQ(build(cut, "黙").text, "彼は黙った。");  // ……」 then no quotative: a break
  const PageModel midSentence{{layout("彼は…少し考えた。", true)}};
  EXPECT_EQ(build(midSentence, "考").text, "彼は…少し考えた。");
  const PageModel paragraphEnd{{layout("まさか…", true), layout("本当に？", true)}};
  EXPECT_EQ(build(paragraphEnd, "ま").text, "まさか…");
}

TEST(SentenceJa, TruncatedAtThePageTopAndBottom) {
  const PageModel page{{layout("いた本を閉じた。彼は窓を開けて外を"), layout("見た")}};
  const auto top = build(page, "閉");
  EXPECT_EQ(top.text, "いた本を閉じた。");
  EXPECT_TRUE(top.truncatedLeft);
  EXPECT_FALSE(top.truncatedRight);
  const auto bottom = build(page, "窓");
  EXPECT_EQ(bottom.text, "彼は窓を開けて外を見た");
  EXPECT_FALSE(bottom.truncatedLeft);
  EXPECT_TRUE(bottom.truncatedRight);
  // A page that ends exactly on 。」 isn't truncated.
  const PageModel closed{{{{"行", "く", "。", "」"}, true}}};
  EXPECT_FALSE(build(closed, "行").truncatedRight);
}

TEST(SentenceJa, RunOnIsCappedAroundTheTap) {
  std::string runOn;
  for (int i = 0; i < 300; i++) runOn += (i == 200 ? "猫" : "あ");
  const PageModel page{{layout(runOn, true)}};
  const auto s = build(page, "猫");
  EXPECT_EQ(utf16Length(s.text), lexipoint::config::kMaxSentenceCodepoints);
  EXPECT_EQ(tapped(s), "猫");
  EXPECT_TRUE(s.truncatedLeft);
  EXPECT_TRUE(s.truncatedRight);
  // Centred: roughly as much before the tap as after.
  EXPECT_NEAR(static_cast<int>(s.tapOffset), static_cast<int>(lexipoint::config::kMaxSentenceCodepoints / 2), 1);
}

TEST(SentenceJa, LatinInsideJapaneseJoinsWithoutSpaces) {
  const PageModel page{{layout("彼はiPhoneを買った。", true)}};
  const auto s = build(page, "iPhone");
  EXPECT_EQ(s.text, "彼はiPhoneを買った。");
  EXPECT_EQ(s.tapOffset, 2u);
  EXPECT_EQ(s.tapLength, 6u);
  const PageModel words{{layout("彼はHarry Potterを読んだ。", true)}};
  EXPECT_EQ(build(words, "Potter").text, "彼はHarry Potterを読んだ。");  // Latin-Latin keeps its space
}

TEST(SentenceJa, OffsetsAreUtf16Units) {
  // 𠮟 (U+20B9F) is two UTF-16 units: 猫 starts at 5, not 4 (codepoints) or 10 (bytes).
  const PageModel page{{layout("𠮟られた猫。", true)}};
  const auto s = build(page, "猫");  // the token is 猫。 (the layout glues 。 on)
  EXPECT_EQ(s.tapOffset, 5u);
  EXPECT_EQ(s.tapLength, 2u);
  const auto nonBmpTap = build(page, "𠮟");
  EXPECT_EQ(nonBmpTap.tapOffset, 0u);
  EXPECT_EQ(nonBmpTap.tapLength, 2u);
}

TEST(SentenceJa, InvisibleCharactersAreDropped) {
  PageModel page{{{{"彼", "​は", "来", "\xC2\xAD", "た。"}, true}}};
  const auto s = build(page, "来");
  EXPECT_EQ(s.text, "彼は来た。");
  EXPECT_EQ(s.tapOffset, 2u);
  // Tapping a token that is only invisible characters gives nothing to look up.
  EXPECT_FALSE(buildSentence(page, {0, 3}, Script::Japanese).has_value());
  EXPECT_FALSE(buildSentence(page, {1, 0}, Script::Japanese).has_value());
  EXPECT_FALSE(buildSentence(page, {0, 9}, Script::Japanese).has_value());
}

TEST(SentenceZh, DialogueTitlesEllipsisAndMixedText) {
  const PageModel dialogue{{layout("他说：", true), layout("“明天再来吧。”", true), layout("她点了点头。", true)}};
  EXPECT_EQ(build(dialogue, "明", Script::Chinese).text, "“明天再来吧。”");
  const PageModel title{{layout("我读了《红楼梦》这本书。然后睡了。", true)}};
  EXPECT_EQ(build(title, "楼", Script::Chinese).text, "我读了《红楼梦》这本书。");
  const PageModel ellipsis{{layout("他想了想……", true), layout("没有说话。", true)}};
  EXPECT_EQ(build(ellipsis, "想", Script::Chinese).text, "他想了想……");
  const PageModel mixed{{layout("我用iPhone拍照。", true)}};
  const auto s = build(mixed, "iPhone", Script::Chinese);
  EXPECT_EQ(s.text, "我用iPhone拍照。");
  EXPECT_EQ(s.tapOffset, 2u);
}

TEST(SentenceZh, SemicolonIsAFallbackCutOnlyPastTheCap) {
  const PageModel shortOne{{layout("天很冷；他穿上外套出门了。", true)}};
  EXPECT_EQ(build(shortOne, "外", Script::Chinese).text, "天很冷；他穿上外套出门了。");  // under the cap: kept whole
  std::string longText;
  for (int i = 0; i < 80; i++) longText += "长";
  longText += "；他走了";
  for (int i = 0; i < 80; i++) longText += "远";
  longText += "。";
  const PageModel page{{layout(longText, true)}};
  const auto s = build(page, "他", Script::Chinese);
  EXPECT_EQ(s.text.rfind("他走了", 0), 0u);  // cut right after the ；
  EXPECT_TRUE(s.truncatedLeft);
  EXPECT_LE(utf16Length(s.text), lexipoint::config::kMaxSentenceCodepoints);
}

TEST(SentenceZh, CurlyQuotesAreClosersOnlyInChinese) {
  // English keeps treating ” as an ordinary closer, but never as a dialogue break.
  const PageModel en{{layout("He said “no” “yes” and left.", true)}};
  EXPECT_EQ(build(en, "yes", Script::Latin).text, "He said “no” “yes” and left.");
}

TEST(SentenceEn, WordsJoinWithSpacesAndStopAtPunctuation) {
  const PageModel page{{layout("It was late. The old man opened", true), layout("the door slowly! Then he won-"),
                        layout("dered why. Nobody")}};
  EXPECT_EQ(build(page, "opened", Script::Latin).text, "The old man opened the door slowly!");
  const auto s = build(page, "dered", Script::Latin);
  EXPECT_EQ(s.text, "Then he won-dered why.");
  EXPECT_EQ(tapped(s), "dered");
  const auto question = build(page, "late", Script::Latin);
  EXPECT_EQ(question.text, "It was late.");
  EXPECT_EQ(question.tapOffset, 7u);
}
