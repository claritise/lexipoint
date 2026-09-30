// C3 (V6): the ⋯ tab's sentence preview: clauses, Shorter and Longer (docs/v0.2/00-overview.md "V6 design" 3).

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "lexirise/card/SentencePreview.h"

using namespace lexipoint::card;
using lexipoint::Language;

namespace {

std::vector<std::string> clauses(const std::string& text, const Language language = Language::Japanese) {
  std::vector<std::string> out;
  for (const std::string_view c : splitClauses(text, language)) out.emplace_back(c);
  return out;
}

// `text` with `word` marked (its first occurrence), and the page after it.
SentenceForSave sentence(const std::string& text, const std::string& word, const std::string& after = {},
                         const Language language = Language::Japanese) {
  return {language, text, text.find(word), word.size(),
          after.empty() ? std::vector<LaterSentence>{} : std::vector<LaterSentence>{{after, ""}}};
}

std::string shown(const SentencePreview& p) { return p.shown().text; }

}  // namespace

TEST(Clauses, SplitAfterEachClauseMark) {
  EXPECT_EQ(clauses("駅は遠く、道は混んでいて、彼は歩いた。"),
            (std::vector<std::string>{"駅は遠く、", "道は混んでいて、", "彼は歩いた。"}));
  EXPECT_EQ(clauses("几年不见，他长得很像他的父亲；院子里的树也长高了。", Language::Chinese),
            (std::vector<std::string>{"几年不见，", "他长得很像他的父亲；", "院子里的树也长高了。"}));
  EXPECT_EQ(clauses("一日中雨だった。"), (std::vector<std::string>{"一日中雨だった。"}));
  // A sentence's end ends a clause too (the page's next sentences come one clause at a time).
  EXPECT_EQ(clauses("雨が降る。晴れた！本当？"), (std::vector<std::string>{"雨が降る。", "晴れた！", "本当？"}));
  // 、 is a clause mark in Japanese, the list comma in Chinese.
  EXPECT_EQ(clauses("我买了苹果、香蕉，然后回家了。", Language::Chinese),
            (std::vector<std::string>{"我买了苹果、香蕉，", "然后回家了。"}));
  EXPECT_EQ(clauses("りんご、バナナ"), (std::vector<std::string>{"りんご、", "バナナ"}));
  EXPECT_EQ(clauses("そして、"), (std::vector<std::string>{"そして、"}));
  EXPECT_EQ(clauses("A, b; c"), (std::vector<std::string>{"A, b; c"}));  // ASCII punctuation isn't either
  EXPECT_TRUE(clauses("").empty());
  std::string joined;
  for (const std::string& c : clauses("あ、い，う；え")) joined += c;
  EXPECT_EQ(joined, "あ、い，う；え");
}

TEST(SentencePreview, ShorterDropsTheFirstWhileOneIsBeforeTheWordThenTheLast) {
  SentencePreview p(sentence("駅は遠く、道は混んでいて、彼は歩いた、疲れていた。", "混んで"));
  EXPECT_EQ(shown(p), "駅は遠く、道は混んでいて、彼は歩いた、疲れていた。");
  EXPECT_TRUE(p.shorter());
  EXPECT_EQ(shown(p), "道は混んでいて、彼は歩いた、疲れていた。");
  EXPECT_TRUE(p.shorter());  // none before the word's: the last
  EXPECT_EQ(shown(p), "道は混んでいて、彼は歩いた、");
  EXPECT_TRUE(p.shorter());
  EXPECT_EQ(shown(p), "道は混んでいて、");
  EXPECT_FALSE(p.shorter());  // the word's own clause always stays
  EXPECT_EQ(shown(p), "道は混んでいて、");
  const MarkedText m = p.shown();
  EXPECT_EQ(m.text.substr(m.markStart, m.markLength), "混んで");
}

TEST(SentencePreview, LongerPutsBackTheLastDroppedThenAddsThePagesNextClause) {
  SentencePreview p(sentence("駅は遠く、道は混んでいて、彼は歩いた。", "混んで", "春の風が、少し優しかった。"));
  p.shorter();  // the first
  p.shorter();  // the last
  EXPECT_EQ(shown(p), "道は混んでいて、");
  EXPECT_EQ(p.longerShown()->text, "道は混んでいて、彼は歩いた。");
  EXPECT_TRUE(p.longer());  // the last dropped: the last
  EXPECT_EQ(shown(p), "道は混んでいて、彼は歩いた。");
  EXPECT_TRUE(p.longer());  // then the first
  EXPECT_EQ(shown(p), "駅は遠く、道は混んでいて、彼は歩いた。");
  EXPECT_TRUE(p.longer());  // none dropped: the next clause on the page
  EXPECT_EQ(shown(p), "駅は遠く、道は混んでいて、彼は歩いた。春の風が、");
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "駅は遠く、道は混んでいて、彼は歩いた。春の風が、少し優しかった。");
  EXPECT_FALSE(p.longerShown());
  EXPECT_FALSE(p.longer());  // never past the page
  const MarkedText m = p.shown();
  EXPECT_EQ(m.text.substr(m.markStart, m.markLength), "混んで");
  // Shorter after that: the first clause before the word's goes first, then the page's added ones from the end.
  EXPECT_TRUE(p.shorter());
  EXPECT_EQ(shown(p), "道は混んでいて、彼は歩いた。春の風が、少し優しかった。");
  EXPECT_TRUE(p.shorter());
  EXPECT_EQ(shown(p), "道は混んでいて、彼は歩いた。春の風が、");
}

TEST(SentencePreview, NothingToDoChangesNothing) {
  SentencePreview p(sentence("彼は歩いた。", "彼"));
  EXPECT_FALSE(p.shorter());
  EXPECT_FALSE(p.longer());
  EXPECT_FALSE(p.longerShown());
  EXPECT_EQ(shown(p), "彼は歩いた。");
  EXPECT_EQ(p.shown().markStart, 0u);
}

TEST(SentencePreview, TheShownSentenceHasNoSpacesAtItsEnds) {
  // A paragraph's indent (　) isn't part of the card; the word's mark follows the trim. The next paragraph's indent
  // comes as its separator (what the page has between the two sentences).
  SentenceForSave with = sentence("\xE3\x80\x80毎朝の電車が煩わしくて、彼は辞めた。", "煩わしくて");
  with.after = {{"春が来た。", "\xE3\x80\x80"}};
  SentencePreview p(with);
  const MarkedText m = p.shown();
  EXPECT_EQ(m.text, "毎朝の電車が煩わしくて、彼は辞めた。");
  EXPECT_EQ(m.text.substr(m.markStart, m.markLength), "煩わしくて");
  p.longer();
  EXPECT_EQ(shown(p), "毎朝の電車が煩わしくて、彼は辞めた。\xE3\x80\x80春が来た。");
}

TEST(SentencePreview, AWordInTheLastClauseKeepsIt) {
  SentencePreview p(sentence("雨が降って、風が吹いて、彼は歩いた。", "歩いた"));
  EXPECT_TRUE(p.shorter());
  EXPECT_TRUE(p.shorter());
  EXPECT_EQ(shown(p), "彼は歩いた。");
  EXPECT_FALSE(p.shorter());
}

TEST(SentencePreview, ChineseClauses) {
  SentencePreview p(sentence("几年不见，他长得很像他的父亲了。", "长", {}, Language::Chinese));
  EXPECT_TRUE(p.shorter());
  EXPECT_EQ(shown(p), "他长得很像他的父亲了。");
  const MarkedText m = p.shown();
  EXPECT_EQ(m.text.substr(m.markStart, m.markLength), "长");
}

TEST(SentencePreview, LongerAddsThePagesNextSentenceAClauseAtATime) {
  SentencePreview p(sentence("彼は歩いた。", "歩いた", "雨が降る。晴れた。春の風が、少し。"));
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "彼は歩いた。雨が降る。");
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "彼は歩いた。雨が降る。晴れた。");
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "彼は歩いた。雨が降る。晴れた。春の風が、");
}

TEST(SentencePreview, ChineseKeepsAListTogether) {
  SentencePreview p(sentence("我买了苹果、香蕉，然后回家了。", "香蕉", {}, Language::Chinese));
  EXPECT_TRUE(p.shorter());  // the word's clause holds the whole list
  EXPECT_EQ(shown(p), "我买了苹果、香蕉，");
  EXPECT_FALSE(p.shorter());
}

TEST(Clauses, ClosersAndDoubledMarksStayWithTheMarkBeforeThem) {
  EXPECT_EQ(clauses("「本当か？」と彼は言った。"), (std::vector<std::string>{"「本当か？」", "と彼は言った。"}));
  EXPECT_EQ(clauses("「本当？！」"), (std::vector<std::string>{"「本当？！」"}));
  EXPECT_EQ(clauses("「はい。」"), (std::vector<std::string>{"「はい。」"}));
  EXPECT_EQ(clauses("「行こう。」と彼は言った。"), (std::vector<std::string>{"「行こう。」", "と彼は言った。"}));
  EXPECT_EQ(clauses("本当！？うそだ。"), (std::vector<std::string>{"本当！？", "うそだ。"}));
  EXPECT_EQ(clauses("“你好！”他说。", Language::Chinese), (std::vector<std::string>{"“你好！”", "他说。"}));
}

TEST(SentencePreview, EachLaterSentenceEndsAClauseEvenWithoutAMark) {
  SentenceForSave with = sentence("私は聞いた。", "聞いた");
  with.after = {{"「はい」", ""}, {"「そうか」", ""}, {"彼は言った。", ""}};
  SentencePreview p(with);
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "私は聞いた。「はい」");
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "私は聞いた。「はい」「そうか」");
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "私は聞いた。「はい」「そうか」彼は言った。");
  EXPECT_FALSE(p.longer());
}

TEST(SentencePreview, ThePagesOwnSeparatorBetweenSentences) {
  SentenceForSave with = sentence("彼は言った。", "言った");
  with.after = {{"He left.", ""}, {"“Go,” she said.", " "}, {"’Tis late.", " "}};
  SentencePreview p(with);
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "彼は言った。He left.");
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "彼は言った。He left. “Go,” she said.");
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "彼は言った。He left. “Go,” she said. ’Tis late.");
  const MarkedText m = p.shown();
  EXPECT_EQ(m.text.substr(m.markStart, m.markLength), "言った");
}

TEST(SentencePreview, NoSpaceWhereThePageHadNone) {
  // Chinese: “好。”“走 and ”3 sit in one laid-out token (text::separatorBetween gives "").
  SentencePreview quotes(sentence("“好。”", "好", "“走吧。”", Language::Chinese));
  EXPECT_TRUE(quotes.longer());
  EXPECT_EQ(shown(quotes), "“好。”“走吧。”");
  SentencePreview digit(sentence("他说：“好。”", "说", "3点走。", Language::Chinese));
  EXPECT_TRUE(digit.longer());
  EXPECT_EQ(shown(digit), "他说：“好。”3点走。");
}

TEST(SentencePreview, AnIdeographicSpaceBetweenSentencesIsKept) {
  SentenceForSave with = sentence("何だって！", "何");
  with.after = {{"もう一度言え。", "\xE3\x80\x80"}};
  SentencePreview p(with);
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "何だって！\xE3\x80\x80もう一度言え。");
}

TEST(SentencePreview, TheSeparatorGoesOnlyBeforeALaterSentencesFirstClause) {
  SentencePreview p({Language::Chinese, "He left.", 3, 4, {{"Tom说，他笑了。", " "}}});
  EXPECT_TRUE(p.longer());
  EXPECT_TRUE(p.longer());
  EXPECT_EQ(shown(p), "He left. Tom说，他笑了。");
}

TEST(SentencePreview, LongerShownIsWhatLongerWouldShowAtEveryStep) {
  SentencePreview p(sentence("駅は遠く、道は混んでいて、彼は歩いた。", "混んで", "春の風が、少し優しかった。"));
  const auto check = [&p] {
    const std::optional<MarkedText> next = p.longerShown();
    SentencePreview copy = p;
    ASSERT_EQ(copy.longer(), next.has_value());
    if (next) EXPECT_EQ(*next, copy.shown());
  };
  check();
  EXPECT_EQ(p.longerShown()->text, "駅は遠く、道は混んでいて、彼は歩いた。春の風が、");
  p.shorter();
  check();
  EXPECT_EQ(p.longerShown()->text, "駅は遠く、道は混んでいて、彼は歩いた。");
  p.shorter();
  check();
  while (p.longer()) check();
  check();
}
