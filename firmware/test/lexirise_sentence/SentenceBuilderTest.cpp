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

// Reference copy of CrossPoint's CJK line-break rules (ParsedText.cpp: isNoBreakBefore/AfterCjkPunctuation,
// hasCjkBreakOpportunityBetween), so the tokens here are the tokens the reader lays out. The end-to-end
// suite (test/lexirise_layout) runs the real parser and catches any drift.
bool noBreakBefore(const uint32_t cp) {
  for (const uint32_t c :
       {0x2Eu,   0x2Cu,   0x3Au,   0x3Bu,   0x21u,   0x3Fu,   0x29u,   0x5Du,   0x7Du,   0xBBu,   0x2019u,
        0x201Du, 0x3001u, 0x3002u, 0x3009u, 0x300Bu, 0x300Du, 0x300Fu, 0x3011u, 0x3015u, 0x3017u, 0x3019u,
        0x301Bu, 0xFF01u, 0xFF09u, 0xFF0Cu, 0xFF0Eu, 0xFF1Au, 0xFF1Bu, 0xFF1Fu, 0xFF3Du, 0xFF5Du}) {
    if (cp == c) return true;
  }
  return false;
}
bool noBreakAfter(const uint32_t cp) {
  for (const uint32_t c : {0x28u, 0x5Bu, 0x7Bu, 0xABu, 0x2018u, 0x201Cu, 0x3008u, 0x300Au, 0x300Cu, 0x300Eu, 0x3010u,
                           0x3014u, 0x3016u, 0x3018u, 0x301Au, 0xFF08u, 0xFF3Bu, 0xFF5Bu}) {
    if (cp == c) return true;
  }
  return false;
}
bool breakBetween(const uint32_t left, const uint32_t right) {
  if (!utf8IsCjkBreakable(left) && !utf8IsCjkBreakable(right)) return false;
  if (noBreakAfter(left) || noBreakBefore(right)) return false;
  return !utf8IsCombiningMark(right);
}

// Splits one line the way CrossPoint lays it out: words at spaces, then CJK break opportunities
// within each word.
TextLine layout(const std::string& text, const bool startsParagraph = false) {
  TextLine line;
  line.startsParagraph = startsParagraph;
  std::string word;
  uint32_t prev = 0;
  const auto flush = [&] {
    if (!word.empty()) line.tokens.push_back(word);
    word.clear();
    prev = 0;
  };
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    if (cp == ' ') {
      flush();
      continue;
    }
    if (prev != 0 && breakBetween(prev, cp)) flush();
    utf8AppendCodepoint(cp, word);
    prev = cp;
  }
  flush();
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
  EXPECT_EQ(utf16Length(s.text), lexipoint::config::kMaxSentenceUnits);
  EXPECT_EQ(tapped(s), "猫");
  EXPECT_TRUE(s.truncatedLeft);
  EXPECT_TRUE(s.truncatedRight);
  // Centred: roughly as much before the tap as after.
  EXPECT_NEAR(static_cast<int>(s.tapOffset), static_cast<int>(lexipoint::config::kMaxSentenceUnits / 2), 1);
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

TEST(SentenceZh, OneTokenHoldingTwoSentences) {
  // CrossPoint never splits two non-CJK characters, so “好。”“走 is one laid-out token.
  const TextLine line = layout("“好。”“走吧。”", true);
  ASSERT_EQ(line.tokens.front(), "“好。”“走");
  const PageModel page{{line}};
  const auto s = build(page, "好", Script::Chinese);  // the tap lands on its first piece with a letter
  EXPECT_EQ(s.text, "“好。”");
  EXPECT_EQ(tapped(s), "“好。”");
  EXPECT_EQ(build(page, "吧", Script::Chinese).text, "“走吧。”");
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
  EXPECT_LE(utf16Length(s.text), lexipoint::config::kMaxSentenceUnits);
}

TEST(SentenceZh, CurlyQuotesAreClosersOnlyInChinese) {
  // English keeps treating ” as an ordinary closer, but never as a dialogue break.
  const PageModel en{{layout("He said “no” “yes” and left.", true)}};
  EXPECT_EQ(build(en, "yes", Script::Latin).text, "He said “no” “yes” and left.");
}

TEST(SentenceEn, NonBreakingSpaceTokensAreOneSpace) {
  PageModel page{{{{"Mr", "\xC2\xA0", "Smith", "left."}, true}}};
  EXPECT_EQ(build(page, "Smith", Script::Latin).text, "Mr Smith left.");
  EXPECT_FALSE(buildSentence(page, {0, 1}, Script::Latin).has_value());  // nothing to look up in a space
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

// Terminator runs and dotted words stay whole; only a real sentence break inside a token splits it.
TEST(SentenceRuns, TerminatorRunsStayTogether) {
  const PageModel ja{{layout("「本当ですか！？」と彼は聞いた。次。", true)}};
  EXPECT_EQ(build(ja, "本").text, "「本当ですか！？」と彼は聞いた。");
  EXPECT_EQ(build(ja, "聞").text, "「本当ですか！？」と彼は聞いた。");
  const PageModel jaAscii{{layout("本当に!?と思った。", true), layout("すごい!!次だ。", true)}};
  EXPECT_EQ(build(jaAscii, "思").text, "本当に!?と思った。");
  EXPECT_EQ(build(jaAscii, "す").text, "すごい!!");
  const PageModel zh{{layout("“你疯了吗？！”他问。", true)}};
  EXPECT_EQ(build(zh, "疯", Script::Chinese).text, "“你疯了吗？！”");
  EXPECT_EQ(build(zh, "问", Script::Chinese).text, "他问。");
}

TEST(SentenceRuns, DottedLatinWordsStayWhole) {
  const PageModel en{{layout("It costs 3.50 dollars today. See example.com for more. Wait... what?", true)}};
  EXPECT_EQ(build(en, "costs", Script::Latin).text, "It costs 3.50 dollars today.");
  const auto url = build(en, "example", Script::Latin);
  EXPECT_EQ(url.text, "See example.com for more.");
  EXPECT_EQ(tapped(url), "example.com");
  EXPECT_EQ(build(en, "Wait", Script::Latin).text, "Wait... what?");
}

// Round-3 shapes: brackets and titles aren't dialogue, the full-width space is spacing, dots inside numbers.
TEST(SentenceShapes, BracketsAndTitlesDoNotBreak) {
  const PageModel ja{{layout("彼は『ノルウェイの森』（村上春樹）を読んだ。", true)}};
  EXPECT_EQ(build(ja, "読").text, "彼は『ノルウェイの森』（村上春樹）を読んだ。");
  const PageModel zh{{layout("我读了《红楼梦》（曹雪芹著）。", true)}};
  EXPECT_EQ(build(zh, "读", Script::Chinese).text, "我读了《红楼梦》（曹雪芹著）。");
  const PageModel news{{layout("【速報】【重要】地震が起きた。", true)}};
  EXPECT_EQ(build(news, "地").text, "【速報】【重要】地震が起きた。");
  // Known trade-off (sentence-extraction.md §2 rule 3): quoted words in a row read as dialogue lines.
  const PageModel words{{layout("彼は「東京」「大阪」を訪れた。", true)}};
  EXPECT_EQ(build(words, "訪").text, "「大阪」を訪れた。");
}

TEST(SentenceShapes, FullWidthSpaceIsSpacingNotText) {
  const PageModel indent{{layout("　彼は来た。", true)}};
  const auto s = build(indent, "来");
  EXPECT_EQ(s.text, "彼は来た。");
  EXPECT_EQ(s.tapOffset, 2u);
  const PageModel after{{layout("本当？　嘘でしょ。", true)}};
  EXPECT_EQ(build(after, "嘘").text, "嘘でしょ。");
  EXPECT_EQ(build(after, "本").text, "本当？");
  const PageModel quotative{{layout("何だ？　と思った。", true)}};
  EXPECT_EQ(build(quotative, "思").text, "何だ？　と思った。");  // the quote runs on, its 　 kept inside
  const PageModel bracketed{{layout("「何だ？」　と思った。", true)}};
  EXPECT_EQ(build(bracketed, "思").text, "「何だ？」　と思った。");
  // Tapping the space itself gives nothing to look up.
  const PageModel only{{{{"　", "彼", "は"}, true}}};
  EXPECT_FALSE(buildSentence(only, {0, 0}, Script::Japanese).has_value());
}

TEST(SentenceShapes, DotsInsideNumbersAndAbbreviations) {
  const PageModel ja{{layout("値は３．５だった。次へ。", true)}};
  EXPECT_EQ(build(ja, "値").text, "値は３．５だった。");
  EXPECT_EQ(build(ja, "だ").text, "値は３．５だった。");
  const PageModel usa{{layout("彼はＵ．Ｓ．Ａ．に行った。", true)}};
  EXPECT_EQ(build(usa, "行").text, "彼はＵ．Ｓ．Ａ．に行った。");
  const PageModel stop{{layout("三時だ。３時に会おう。", true)}};  // 。 before a digit still ends it
  EXPECT_EQ(build(stop, "会").text, "３時に会おう。");
}

TEST(SentenceShapes, LatinPunctuationHugsItsWord) {
  // Styled words arrive as their own tokens: “ go ” and "bold ," would be wrong.
  PageModel page{{{{"A", "bold", ",", "word", "(really)", "and", "“", "go", "”", "now."}, true}}};
  EXPECT_EQ(build(page, "word", Script::Latin).text, "A bold, word (really) and “go” now.");
}

TEST(SentenceShapes, DialogueAcrossASpaceAndApostrophes) {
  const PageModel ja{{layout("「うん」　「わかった」と答えた。", true)}};
  EXPECT_EQ(build(ja, "わ").text, "「わかった」と答えた。");
  EXPECT_EQ(build(ja, "う").text, "「うん」");
  PageModel en{{{{"Rock", "\u2019n\u2019", "roll", "don\u2019t", "stop", "\u201cgo", "\u2019", "now."}, true}}};
  EXPECT_EQ(build(en, "roll", Script::Latin).text, "Rock \u2019n\u2019 roll don\u2019t stop \u201cgo\u2019 now.");
}

TEST(SentenceShapes, AbbreviationBeforeASpaceEnds) {
  const PageModel ja{{layout("彼はＵ．Ｓ．Ａ．　次の文だ。", true)}};
  EXPECT_EQ(build(ja, "次").text, "次の文だ。");
}

TEST(SentenceShapes, ContractionsSplitByStylesRejoin) {
  PageModel en{{{{"Dickens", "\u2019s", "novel", "and", "Rock", "\u2019n\u2019", "roll", "\u2019twas"}, true}}};
  EXPECT_EQ(build(en, "novel", Script::Latin).text, "Dickens\u2019s novel and Rock \u2019n\u2019 roll \u2019twas");
}
