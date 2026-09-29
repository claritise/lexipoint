// languages.md §3a: a Japanese reading in kana and romaji, whichever Lexirise gave; exact or not at all.

#include <Utf8.h>
#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "lexirise/text/Kana.h"

using lexipoint::text::isAllKatakana;
using lexipoint::text::japaneseReading;
using lexipoint::text::kanaReading;
using lexipoint::text::kanaToRomaji;
using lexipoint::text::katakanaToHiragana;
using lexipoint::text::romajiToHiragana;
using lexipoint::text::detail::kanaSyllables;

namespace {

std::string kana(const char* romaji) { return romajiToHiragana(romaji).value_or("<none>"); }
std::string romaji(const std::string& kana) { return kanaToRomaji(kana).value_or("<none>"); }
// The hiragana's katakana spelling (each kana 0x60 on: き → キ, ゔ → ヴ).
std::string katakanaOf(const std::string& hiragana) {
  std::string out;
  const auto* p = reinterpret_cast<const unsigned char*>(hiragana.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) utf8AppendCodepoint(cp + 0x60, out);
  return out;
}

}  // namespace

TEST(Kana, LexirisesSpellings) {
  // Long vowels spelled out.
  EXPECT_EQ(kana("toukyou"), "とうきょう");
  EXPECT_EQ(kana("kēki"), "けいき");  // ē draws out as い
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
  // nn + vowel: ん then に. wa is わ; Lexirise writes the particle は as ha (languages.md §3a).
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

// Lexirise spells づ "dzu" (気づく kidzuku, 続ける tsudzukeru): exact, so it converts both ways.
TEST(Kana, DzuIsDu) {
  EXPECT_EQ(kana("kidzuita"), "きづいた");
  EXPECT_EQ(kana("tsudzukeru"), "つづける");
  EXPECT_EQ(kana("kodzukai"), "こづかい");
  EXPECT_EQ(kana("hidzuke"), "ひづけ");
  EXPECT_EQ(kana("tedzukuri"), "てづくり");
  EXPECT_EQ(kana("kidzukanakatta"), "きづかなかった");  // before a doubled consonant
}

// Lexirise's spellings as (word, transliteration) pairs, synthetic but shaped like the ones measured in
// languages.md §3a: "dzu" is づ, "zu" is ず and "ji" is じ, so each reading converts to the word's own kana.
TEST(Kana, LexirisesZuDzuAndJiPairs) {
  const struct {
    const char* word;
    const char* romaji;
    const char* reading;
  } kPairs[] = {
      {"気づく", "kidzuku", "きづく"},
      {"手作り", "tedzukuri", "てづくり"},
      {"読み続ける", "yomitsudzukeru", "よみつづける"},
      {"釘付け", "kugidzuke", "くぎづけ"},
      {"必ず", "kanarazu", "かならず"},
      {"水", "mizu", "みず"},
      {"難しい", "muzukashii", "むずかしい"},
      {"同じ", "onaji", "おなじ"},
      {"自分", "jibun", "じぶん"},
      {"信じる", "shinjiru", "しんじる"},
      {"傷つく", "kizutsuku", "きずつく"},
  };
  for (const auto& p : kPairs) EXPECT_EQ(kanaReading(p.word, p.romaji).value_or("<none>"), p.reading) << p.word;
}

// A transliteration in kana (claritise's account, the device 2026-09-29) is read back in Lexirise's own romaji.
TEST(Kana, KanaReadsBackAsLexirisesRomaji) {
  EXPECT_EQ(romaji("きょうしつ"), "kyoushitsu");
  EXPECT_EQ(romaji("は"), "ha");
  EXPECT_EQ(romaji("とうきょう"), "toukyou");
  EXPECT_EQ(romaji("きんようび"), "kin'youbi");  // ん before a vowel or y
  EXPECT_EQ(romaji("しんぶん"), "shinbun");
  EXPECT_EQ(romaji("がっこう"), "gakkou");
  EXPECT_EQ(romaji("まっちゃ"), "matcha");  // っ before ch
  EXPECT_EQ(romaji("きづく"), "kidzuku");
  EXPECT_EQ(romaji("わずらわしい"), "wazurawashii");
  EXPECT_EQ(romaji("じてんしゃ"), "jitensha");
  EXPECT_EQ(romaji("コーヒー"), "kōhī");  // katakana: ー a macron, as Lexirise writes it
  EXPECT_EQ(romaji("パーティー"), "pātī");
  EXPECT_EQ(romaji("ヴァイオリン"), "vaiorin");
}

TEST(Kana, KanaThatCantBeReadMeansNothing) {
  for (const char* bad : {"",
                          "ー",
                          "ーあ",
                          "っ",
                          "あっい",
                          "ぁ",
                          "ゃ",
                          "ゝ",
                          "んゞ",
                          "んぁ",
                          "っか",
                          "ッス",
                          "あっー",
                          "あっっか",
                          "あっゝ",
                          "かんゞ",
                          "かーゝ",
                          "ねぇゝ",
                          "ゎ",
                          "ヵ",
                          "ゟ",
                          "ヿ",
                          "かゃ",
                          "かゎ",
                          "カヶ",
                          "ほゟ",
                          "教室",
                          "きょうshitsu",
                          "ハリー・ポッター"}) {
    EXPECT_FALSE(kanaToRomaji(bad)) << bad;
  }
}

// Every word the romaji converter reads, to kana and back: unchanged (for these words; kana → romaji → kana isn't
// always, as ぢ → ji → じ).
TEST(Kana, KanaAndRomajiRoundTrip) {
  for (const char* word :
       {"toukyou", "ookii",  "kin'youbi",    "fun'iki", "man'indensha", "kakkoii",  "chotto",     "kekkon",
        "matcha",  "maiasa", "wazurawashii", "kaisha",  "yameru",       "kidzuita", "tsudzukeru", "shinbun",
        "kinen",   "kin'en", "hon'ya",       "benkyou", "jibun",        "onaji"}) {
    EXPECT_EQ(romaji(kana(word).c_str()), word);
  }
}

// Both readings from either form of transliteration (languages.md §3a).
TEST(Kana, BothReadingsFromEitherTransliteration) {
  const auto both = [](const char* surface, const char* transliteration) {
    const auto r = japaneseReading(surface, transliteration);
    return r.kana + " " + r.romaji;
  };
  EXPECT_EQ(both("教室", "kyoushitsu"), "きょうしつ kyoushitsu");
  EXPECT_EQ(both("教室", "きょうしつ"), "きょうしつ kyoushitsu");
  EXPECT_EQ(both("コーヒー", "kōhī"), "コーヒー kōhī");
  EXPECT_EQ(both("コーヒー", "コーヒー"), "コーヒー kōhī");
  EXPECT_EQ(both("コーヒー", "こーひー"), "コーヒー kōhī");  // a katakana word is its own reading
  EXPECT_EQ(both("気づく", "きづく"), "きづく kidzuku");
  EXPECT_EQ(both("一緒", "ic hi"), "ic hi ic hi");  // neither converts: as given, both ways
  EXPECT_EQ(both("思っ", "omoっ"), "omoっ omoっ");
  EXPECT_EQ(both("何", ""), " ");
  EXPECT_EQ(both("コーヒー", ""), "コーヒー kōhī");  // none given: a katakana word reads as itself
  EXPECT_EQ(both("ハリー・ポッター", "harī pottā"), "ハリー・ポッター harī pottā");  // given: kept, though unreadable
  EXPECT_EQ(both("ハリー・ポッター", ""), "ハリー・ポッター ハリー・ポッター");      // none given, unreadable: as it is
  EXPECT_EQ(both("ハリー・ポッター", "はりーぽったー"), "ハリー・ポッター harīpottā");  // read from the given kana
  EXPECT_EQ(both("コーヒー", "ーあ"), "コーヒー ーあ");  // given kana that can't be read: as given
  EXPECT_EQ(both("何", "ーあ"), "ーあ ーあ");            // kana that can't be read: as given, both ways
}

// Every row of kanaToRomaji's table, one by one (a row changed or added: change this list too).
TEST(Kana, EveryKanaRow) {
  const std::vector<std::pair<std::string, std::string>> expected = {
      {"きゃ", "kya"}, {"きゅ", "kyu"}, {"きょ", "kyo"}, {"ぎゃ", "gya"}, {"ぎゅ", "gyu"}, {"ぎょ", "gyo"},
      {"しゃ", "sha"}, {"しゅ", "shu"}, {"しょ", "sho"}, {"しぇ", "she"}, {"じゃ", "ja"},  {"じゅ", "ju"},
      {"じょ", "jo"},  {"じぇ", "je"},  {"ちゃ", "cha"}, {"ちゅ", "chu"}, {"ちょ", "cho"}, {"ちぇ", "che"},
      {"ぢゃ", "ja"},  {"ぢゅ", "ju"},  {"ぢょ", "jo"},  {"にゃ", "nya"}, {"にゅ", "nyu"}, {"にょ", "nyo"},
      {"ひゃ", "hya"}, {"ひゅ", "hyu"}, {"ひょ", "hyo"}, {"びゃ", "bya"}, {"びゅ", "byu"}, {"びょ", "byo"},
      {"ぴゃ", "pya"}, {"ぴゅ", "pyu"}, {"ぴょ", "pyo"}, {"みゃ", "mya"}, {"みゅ", "myu"}, {"みょ", "myo"},
      {"りゃ", "rya"}, {"りゅ", "ryu"}, {"りょ", "ryo"}, {"ふぁ", "fa"},  {"ふぃ", "fi"},  {"ふぇ", "fe"},
      {"ふぉ", "fo"},  {"ふゅ", "fyu"}, {"てぃ", "ti"},  {"てゅ", "tyu"}, {"でぃ", "di"},  {"でゅ", "dyu"},
      {"とぅ", "tu"},  {"どぅ", "du"},  {"うぃ", "wi"},  {"うぇ", "we"},  {"うぉ", "wo"},  {"ゔぁ", "va"},
      {"ゔぃ", "vi"},  {"ゔぇ", "ve"},  {"ゔぉ", "vo"},  {"ゔゅ", "vyu"}, {"つぁ", "tsa"}, {"つぃ", "tsi"},
      {"つぇ", "tse"}, {"つぉ", "tso"}, {"くぁ", "kwa"}, {"くぃ", "kwi"}, {"くぇ", "kwe"}, {"くぉ", "kwo"},
      {"ぐぁ", "gwa"}, {"すぃ", "si"},  {"ずぃ", "zi"},  {"いぇ", "ye"},  {"あ", "a"},     {"い", "i"},
      {"う", "u"},     {"え", "e"},     {"お", "o"},     {"か", "ka"},    {"き", "ki"},    {"く", "ku"},
      {"け", "ke"},    {"こ", "ko"},    {"が", "ga"},    {"ぎ", "gi"},    {"ぐ", "gu"},    {"げ", "ge"},
      {"ご", "go"},    {"さ", "sa"},    {"し", "shi"},   {"す", "su"},    {"せ", "se"},    {"そ", "so"},
      {"ざ", "za"},    {"じ", "ji"},    {"ず", "zu"},    {"ぜ", "ze"},    {"ぞ", "zo"},    {"た", "ta"},
      {"ち", "chi"},   {"つ", "tsu"},   {"て", "te"},    {"と", "to"},    {"だ", "da"},    {"ぢ", "ji"},
      {"づ", "dzu"},   {"で", "de"},    {"ど", "do"},    {"な", "na"},    {"に", "ni"},    {"ぬ", "nu"},
      {"ね", "ne"},    {"の", "no"},    {"は", "ha"},    {"ひ", "hi"},    {"ふ", "fu"},    {"へ", "he"},
      {"ほ", "ho"},    {"ば", "ba"},    {"び", "bi"},    {"ぶ", "bu"},    {"べ", "be"},    {"ぼ", "bo"},
      {"ぱ", "pa"},    {"ぴ", "pi"},    {"ぷ", "pu"},    {"ぺ", "pe"},    {"ぽ", "po"},    {"ま", "ma"},
      {"み", "mi"},    {"む", "mu"},    {"め", "me"},    {"も", "mo"},    {"や", "ya"},    {"ゆ", "yu"},
      {"よ", "yo"},    {"ら", "ra"},    {"り", "ri"},    {"る", "ru"},    {"れ", "re"},    {"ろ", "ro"},
      {"わ", "wa"},    {"ゐ", "wi"},    {"ゑ", "we"},    {"を", "wo"},    {"ゔ", "vu"},
  };
  std::vector<std::pair<std::string, std::string>> table;
  for (const auto& row : kanaSyllables()) table.emplace_back(row.kana, row.romaji);
  EXPECT_EQ(table, expected);
  for (const auto& [kana, romaji] : expected) {
    EXPECT_EQ(kanaToRomaji(kana).value_or("<none>"), romaji) << kana;
    // Each in katakana too (ヴ → ゔ) and after another kana (two kana before one).
    EXPECT_EQ(kanaToRomaji(katakanaOf(kana)).value_or("<none>"), romaji) << katakanaOf(kana);
    EXPECT_EQ(kanaToRomaji("か" + kana).value_or("<none>"), "ka" + romaji) << kana;
  }
}

// Katakana words and colloquial spellings (the research/ kana words: all read, but fragments cut mid-word).
TEST(Kana, KatakanaWordsAndColloquialKana) {
  EXPECT_EQ(romaji("デューク"), "dyūku");
  EXPECT_EQ(romaji("クァルテット"), "kwarutetto");
  EXPECT_EQ(romaji("ツェッペリン"), "tsepperin");
  EXPECT_EQ(romaji("フュージョン"), "fyūjon");
  EXPECT_EQ(romaji("ウィスキー"), "wisukī");
  EXPECT_EQ(romaji("ヴォーカル"), "vōkaru");
  EXPECT_EQ(romaji("ねぇ"), "nee");  // a small vowel after a syllable: drawn out
  EXPECT_EQ(romaji("はぁ"), "haa");
  EXPECT_EQ(romaji("きぃ"), "kii");
  EXPECT_EQ(romaji("すごぃ"), "sugoi");  // its own vowel, not the one before
  EXPECT_EQ(romaji("よぉ"), "yoo");
  EXPECT_EQ(romaji("ふぅ"), "fuu");
  EXPECT_EQ(romaji("あっ"), "a");  // a final っ: not written
  EXPECT_EQ(romaji("ちぇっ"), "che");
  EXPECT_EQ(romaji("こゝろ"), "kokoro");  // ゝ: the kana before, again
  EXPECT_EQ(romaji("いすゞ"), "isuzu");   // ゞ: voiced
  EXPECT_EQ(romaji("ぶゝ"), "bufu");      // ゝ unvoiced after a voiced kana
  EXPECT_EQ(romaji("はゞ"), "haba");
  EXPECT_EQ(romaji("つゞく"), "tsudzuku");
  EXPECT_EQ(romaji("ほゞ"), "hobo");
  EXPECT_EQ(romaji("かゞ"), "kaga");
  EXPECT_EQ(romaji("ちゞ"), "chiji");
  EXPECT_EQ(romaji("ぱゝ"), "papa");      // ゝ keeps a ぱ
  EXPECT_EQ(romaji("こゝゝ"), "kokoko");  // each ゝ repeats the kana before
  EXPECT_EQ(romaji("ミヽ"), "mimi");      // katakana ヽ
  EXPECT_EQ(romaji("ケーキ"), "kēki");
  // ゞ after every kana with a voiced pair, and ゝ repeating it.
  const std::vector<std::pair<std::string, std::string>> voiced = {
      {"か", "ga"}, {"き", "gi"}, {"く", "gu"}, {"け", "ge"}, {"こ", "go"}, {"さ", "za"},  {"し", "ji"},
      {"す", "zu"}, {"せ", "ze"}, {"そ", "zo"}, {"た", "da"}, {"ち", "ji"}, {"つ", "dzu"}, {"て", "de"},
      {"と", "do"}, {"は", "ba"}, {"ひ", "bi"}, {"ふ", "bu"}, {"へ", "be"}, {"ほ", "bo"},
  };
  for (const auto& [kana, romajiVoiced] : voiced) {
    const std::string plain = romaji(kana);
    EXPECT_EQ(romaji(kana + "ゞ"), plain + romajiVoiced) << kana;
    EXPECT_EQ(romaji(kana + "ゝ"), plain + plain) << kana;
  }
  EXPECT_FALSE(kanaToRomaji("あゞ"));    // nothing to voice
  EXPECT_FALSE(kanaToRomaji("きゃゝ"));  // one kana only
}
