// languages.md §3a: romaji → kana on the device, exact or not at all.

#include <gtest/gtest.h>

#include "lexirise/text/Kana.h"

using lexipoint::text::isAllKatakana;
using lexipoint::text::kanaReading;
using lexipoint::text::katakanaToHiragana;
using lexipoint::text::romajiToHiragana;

namespace {

std::string kana(const char* romaji) { return romajiToHiragana(romaji).value_or("<none>"); }

}  // namespace

TEST(Kana, LexirisesSpellings) {
  // Long vowels spelled out.
  EXPECT_EQ(kana("toukyou"), "とうきょう");
  EXPECT_EQ(kana("ookii"), "おおきい");
  EXPECT_EQ(kana("toori"), "とおり");
  EXPECT_EQ(kana("otousan"), "おとうさん");
  EXPECT_EQ(kana("kyou"), "きょう");
  EXPECT_EQ(kana("gakkou"), "がっこう");
  EXPECT_EQ(kana("eiga"), "えいが");
  // ん before a vowel or y: an apostrophe.
  EXPECT_EQ(kana("kin'youbi"), "きんようび");
  EXPECT_EQ(kana("fun'iki"), "ふんいき");
  EXPECT_EQ(kana("man'indensha"), "まんいんでんしゃ");
  // Doubled consonants.
  EXPECT_EQ(kana("kakkoii"), "かっこいい");
  EXPECT_EQ(kana("chotto"), "ちょっと");
  EXPECT_EQ(kana("kekkon"), "けっこん");
  EXPECT_EQ(kana("matcha"), "まっちゃ");
}

TEST(Kana, TheReferenceWords) {
  EXPECT_EQ(kana("maiasa"), "まいあさ");
  EXPECT_EQ(kana("wazurawashii"), "わずらわしい");
  EXPECT_EQ(kana("kare"), "かれ");
  EXPECT_EQ(kana("kaisha"), "かいしゃ");
  EXPECT_EQ(kana("yameru"), "やめる");
}

TEST(Kana, TheNEdgeCases) {
  EXPECT_EQ(kana("kin'en"), "きんえん");
  EXPECT_EQ(kana("kinen"), "きねん");
  EXPECT_EQ(kana("shinbun"), "しんぶん");
  EXPECT_EQ(kana("shimbun"), "しんぶん");  // traditional Hepburn: m before b, m, p
  EXPECT_EQ(kana("tempura"), "てんぷら");
  EXPECT_EQ(kana("sammai"), "さんまい");
  EXPECT_EQ(kana("mame"), "まめ");  // m before a vowel is ま-row
  // nn + vowel: ん then に. The particle は comes out わ: romaji can't tell them apart (a documented
  // limit, languages.md §3a).
  EXPECT_EQ(kana("konnichiwa"), "こんにちわ");
  EXPECT_EQ(kana("hon"), "ほん");
  EXPECT_EQ(kana("benkyou"), "べんきょう");
  EXPECT_EQ(kana("hon'ya"), "ほんや");
  EXPECT_EQ(kana("Kare"), "かれ");  // case doesn't matter
}

TEST(Kana, MacronsForMixedWords) {
  EXPECT_EQ(kana("kōhī"), "こうひい");  // only for mixed words: katakana surfaces are used as they are
  EXPECT_EQ(kana("ō"), "おう");
  EXPECT_EQ(kana("nō"), "のう");  // n before a macron vowel is な-row, not ん
  EXPECT_EQ(kana("kyō"), "きょう");
  EXPECT_EQ(kana("shōgun"), "しょうぐん");
  EXPECT_EQ(kana("ryōri"), "りょうり");
  EXPECT_EQ(kana("tōkyō"), "とうきょう");
  EXPECT_EQ(kana("Ōsaka"), "おうさか");  // capital macrons too
  EXPECT_EQ(kana("TŌKYŌ"), "とうきょう");
}

TEST(Kana, UnconvertibleMeansNothing) {
  for (const char* bad : {"", "x", "kx", "ka ta", "ka-ta", "'a", "q", "kyi", "vu", "1ka", "ichiitoguchi?"}) {
    EXPECT_FALSE(romajiToHiragana(bad)) << bad;
  }
}

TEST(Kana, KatakanaWordsAreTheirOwnReading) {
  EXPECT_TRUE(isAllKatakana("コーヒー"));
  EXPECT_TRUE(isAllKatakana("ハリー・ポッター"));
  EXPECT_FALSE(isAllKatakana("コーヒー豆"));
  EXPECT_FALSE(isAllKatakana(""));
  EXPECT_EQ(kanaReading("コーヒー", "kōhī").value_or(""), "コーヒー");
  EXPECT_EQ(kanaReading("ビール", "bīru").value_or(""), "ビール");
  EXPECT_EQ(kanaReading("煩わしい", "wazurawashii").value_or(""), "わずらわしい");
  EXPECT_FALSE(kanaReading("一緒", "ic hi"));  // → the card shows the romaji
}

TEST(Kana, KatakanaFoldsToHiragana) {
  EXPECT_EQ(katakanaToHiragana("キレる"), "きれる");
  EXPECT_EQ(katakanaToHiragana("ヴァイオリン"), "ゔぁいおりん");
  EXPECT_EQ(katakanaToHiragana("コーヒー"), "こーひー");  // ー kept
  EXPECT_EQ(katakanaToHiragana("ヽヾ"), "ゝゞ");
  EXPECT_EQ(katakanaToHiragana("食べるabc"), "食べるabc");
  EXPECT_EQ(katakanaToHiragana(""), "");
}
