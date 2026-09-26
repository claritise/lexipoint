// C16: the conjugated form a Japanese word is in, named from its dictionary form.

#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <vector>

#include "lexirise/text/Conjugation.h"

using lexipoint::text::conjugationOf;

// The form is the sentence's last word: nothing follows it on the page.
constexpr std::string_view kSentenceEnd;

namespace {

struct Case {
  const char* surface;
  const char* lemma;
  const char* name;  // nullptr: no name
};

std::vector<std::string> forms(const char* surface, const char* lemma) {
  std::vector<std::string> out;
  if (const auto c = conjugationOf(surface, lemma, kSentenceEnd)) {
    for (const auto& step : c->steps) out.push_back(step.form + " " + step.label);
  }
  return out;
}

}  // namespace

TEST(Conjugation, NamesTheCommonForms) {
  const Case cases[] = {
      // Ichidan
      {"食べた", "食べる", "past"},
      {"食べて", "食べる", "te-form"},
      {"食べない", "食べる", "negative"},
      {"食べなかった", "食べる", "negative past"},
      {"食べます", "食べる", "polite"},
      {"食べました", "食べる", "polite past"},
      {"食べません", "食べる", "polite negative"},
      {"食べませんでした", "食べる", "polite negative past"},
      {"食べている", "食べる", "progressive"},
      {"食べていた", "食べる", "progressive past"},
      {"食べてる", "食べる", "progressive"},
      {"食べたい", "食べる", "-tai"},
      {"食べたかった", "食べる", "-tai past"},
      {"食べたがる", "食べる", "-tagaru"},
      {"食べたがっている", "食べる", "-tagaru progressive"},
      {"行きたがらない", "行く", "-tagaru negative"},
      {"食べられる", "食べる", "passive or potential"},
      {"食べれる", "食べる", "potential"},
      {"食べさせる", "食べる", "causative"},
      {"食べさせられた", "食べる", "causative-passive past"},
      {"食べよう", "食べる", "volitional"},
      {"食べろ", "食べる", "imperative"},
      {"食べれ", "食べる", nullptr},  // not standard: no name
      {"食べれば", "食べる", "-ba conditional"},
      {"食べたら", "食べる", "-tara conditional"},
      {"食べないで", "食べる", "negative te-form"},
      // Godan, each sound change
      {"買って", "買う", "te-form"},
      {"待った", "待つ", "past"},
      {"帰って", "帰る", "te-form"},
      {"読んで", "読む", "te-form"},
      {"遊んだ", "遊ぶ", "past"},
      {"死んで", "死ぬ", "te-form"},
      {"書いて", "書く", "te-form"},
      {"泳いだ", "泳ぐ", "past"},
      {"話して", "話す", "te-form"},
      {"行って", "行く", "te-form"},
      {"行った", "行く", "past"},
      {"逝った", "逝く", "past"},  // 行く's sound change: 逝って, 逝った
      {"書かない", "書く", "negative"},
      {"会わない", "会う", "negative"},
      {"書きます", "書く", "polite"},
      {"書ける", "書く", "potential"},
      {"書かれた", "書く", "passive past"},
      {"書かせる", "書く", "causative"},
      {"書かされる", "書く", "causative-passive"},
      {"書かせられた", "書く", "causative-passive past"},
      {"書こう", "書く", "volitional"},
      {"書け", "書く", "imperative"},
      {"書き", "書く", "continuative"},
      {"書けば", "書く", "-ba conditional"},
      {"書いたら", "書く", "-tara conditional"},
      {"書いている", "書く", "progressive"},
      // する, 来る
      {"した", "する", "past"},
      {"勉強して", "勉強する", "te-form"},
      {"しない", "する", "negative"},
      {"される", "する", "passive"},
      {"させられた", "する", "causative-passive past"},
      {"できる", "する", "potential"},
      {"しよう", "する", "volitional"},
      {"来た", "来る", "past"},
      {"来ない", "来る", "negative"},
      {"来られる", "来る", "passive or potential"},
      {"きて", "くる", "te-form"},
      {"こない", "くる", "negative"},
      {"くれば", "くる", "-ba conditional"},
      // i-adjectives
      {"煩わしくて", "煩わしい", "te-form"},
      {"高かった", "高い", "past"},
      {"高くない", "高い", "negative"},
      {"高くなかった", "高い", "negative past"},
      {"高く", "高い", "adverbial"},
      {"高ければ", "高い", "-ba conditional"},
      {"よかった", "いい", "past"},
      // Not a form it knows, or not this lemma's: no name
      {"食べる", "食べる", nullptr},
      {"食べ", "食べる", "continuative"},
      // る verbs whose class the dictionary form doesn't show: kana i/e-row, or a kanji, before る
      {"かえった", "かえる", "past"},
      {"はいって", "はいる", "te-form"},
      {"しゃべった", "しゃべる", "past"},
      {"いじって", "いじる", "te-form"},
      {"混じった", "混じる", "past"},
      {"帰ろ", "帰る", "volitional"},  // a kanji before る: godan unless listed ichidan
      {"走ろ", "走る", "volitional"},
      {"かえろ", "かえる", nullptr},  // kana alone: 帰る's casual volitional or 変える's imperative
      {"かえった", "かえる", "past"},
      {"しゃべろ", "しゃべる", "volitional"},  // on the kana godan list
      {"はいられる", "はいる", "passive"},
      {"しゃべられる", "しゃべる", "passive"},
      {"見れ", "見る", nullptr},                     // not standard (regional imperative): no name
      {"見られる", "見る", "passive or potential"},  // 見る: listed ichidan
      {"見ろ", "見る", "imperative"},
      {"着ろ", "着る", "imperative"},
      {"切れ", "切る", "imperative"},
      {"切った", "切る", "past"},
      {"起きろ", "起きる", "imperative"},  // a kanji-written i/e-row verb: ichidan
      {"混じろ", "混じる", "volitional"},  // on the godan list
      {"蘇えろ", "蘇える", "volitional"},  // 蘇る with more okurigana: godan, listed
      {"罵しった", "罵しる", "past"},
      {"入いろ", "入いる", "volitional"},
      {"翻えろ", "翻える", "volitional"},
      {"攀じった", "攀じる", "past"},  // よじる: godan, listed
      {"攀じられる", "攀じる", "passive"},
      {"顧ろ", "顧る", "imperative"},
      {"陥いられる", "陥いる", "passive"},
      {"〆ろ", "〆る", nullptr},  // 〆る (締める): either class, so 〆ろ is either's
      {"〆た", "〆る", "past"},
      {"〆られる", "〆る", nullptr},
      {"雑じろ", "雑じる", "volitional"},
      {"帰れ", "帰る", "imperative"},
      {"取ろ", "取る", "volitional"},
      {"おちた", "おちる", "past"},  // kana alone: either class; only the ichidan reaches it
      {"夢見た", "夢見る", "past"},
      {"くれ", "くれる", nullptr},  // くれる's imperative, or its continuative (くれ-ます)
      {"呉れ", "呉れる", nullptr},
      {"くれた", "くれる", "past"},
      {"くれて", "くれる", "te-form"},  // くれる's imperative is its own, not a derived verb's
      {"食べてくれて", "食べてくれる", "te-form"},
      {"食べてくれ", "食べてくれる", nullptr},
      {"くれさせ", "くれる", nullptr},
      {"くれれ", "くれる", nullptr},
      {"くれられ", "くれる", nullptr},
      {"くれてい", "くれる", nullptr},
      {"捻じろ", "捻じる", "volitional"},  // godan, listed
      {"捻じられる", "捻じる", "passive"},
      {"脂ぎった", "脂ぎる", "past"},
      {"入り混じった", "入り混じる", "past"},  // a compound of a listed godan verb
      {"申し出た", "申し出る", "past"},        // ... of a listed ichidan one
      {"心得ている", "心得る", "progressive"},
      {"出来た", "出来る", "past"},  // listed ichidan, not 来る
      {"出来ない", "出来る", "negative"},
      {"できた", "できる", "past"},
      {"来れる", "来る", "potential"},  // ら-less
      {"これる", "くる", "potential"},
      {"来れない", "来る", "potential negative"},
      // A kana run of two or more before る after the last kanji decides, as in a kana-only verb
      {"見くびられた", "見くびる", "passive past"},  // ends in a listed kana godan verb
      {"見くびった", "見くびる", "past"},
      {"踏みにじられた", "踏みにじる", "passive past"},
      {"踏みにじろ", "踏みにじる", "volitional"},
      {"踏みにじれ", "踏みにじる", "imperative"},
      {"食いちぎられた", "食いちぎる", "passive past"},
      {"使いきられた", "使いきる", nullptr},  // きる: 切る (godan) or 着る (ichidan)
      {"使いきった", "使いきる", "past"},
      {"見つけられた", "見つける", nullptr},  // either class: godan passive, or ichidan passive or potential
      {"見つけた", "見つける", "past"},
      {"見つけろ", "見つける", nullptr},
      {"生まれた", "生まれる", "past"},
      {"食べられた", "食べる", "passive or potential past"},  // a kanji directly before べ: ichidan
      // Katakana is read as kana: an i/e-row kana before る, either class
      {"キレた", "キレる", "past"},
      {"キレて", "キレる", "te-form"},
      {"キレない", "キレる", "negative"},
      {"キレろ", "キレる", nullptr},  // godan's casual volitional or ichidan's imperative
      {"バレた", "バレる", "past"},
      {"バレろ", "バレる", nullptr},
      {"バレられた", "バレる", nullptr},
      // Either class: stem + れ is godan's imperative and ichidan's regional one, so no name
      {"かえれ", "かえる", nullptr},
      {"たべれ", "たべる", nullptr},
      {"みれ", "みる", nullptr},
      {"キレれ", "キレる", nullptr},
      {"しった", "しる", "past"},  // a listed whole kana run
      {"おちろ", "おちる", nullptr},
      {"すべった", "すべる", "past"},  // 滑る or 統べる: only 滑る's reading reaches it
      {"すべろ", "すべる", nullptr},   // 滑る's casual volitional or 統べる's imperative
      {"すべられた", "すべる", nullptr},
      {"ひねった", "ひねる", "past"},  // 捻る or 陳ねる: only 捻る's reading reaches it
      {"ひねろ", "ひねる", nullptr},
      {"めいじた", "めいじる", "past"},  // kana 命じる: either class, only the ichidan reaches it
      {"めいじろ", "めいじる", nullptr},
      {"みちた", "みちる", "past"},
      {"こすった", "こする", nullptr},  // not する: no name rather than a wrong one
      {"サボった", "サボる", "past"},   // katakana: godan
      {"かっこよかった", "かっこいい", "past"},
      {"仲よくない", "仲いい", "negative"},
      {"走れ", "走る", "imperative"},
      {"やれ", "やる", "imperative"},
      {"待て", "待つ", "imperative"},
      {"行け", "行く", "imperative"},
      {"言われていませんでした", "言う", "passive progressive polite negative past"},
      {"書いてない", "書く", nullptr},  // 書いている's or 書いてある's negative
      {"書いてる", "書く", "progressive"},
      {"書いてた", "書く", "progressive past"},
      {"取ろう", "取る", "volitional"},
      {"とろ", "とる", "volitional"},  // an a/u/o-row kana before る: godan only
      {"とられる", "とる", "passive"},
      // Progressive after a passive
      {"書かれている", "書く", "passive progressive"},
      {"知られている", "知る", "passive progressive"},
      {"言われていた", "言う", "passive progressive past"},
      // Kana 行く compounds, and いい only as itself
      {"持っていった", "持っていく", "past"},
      {"かわいかった", "かわいい", "past"},
      {"たべた", "食べる", nullptr},
      {"食べちゃった", "食べる", nullptr},
      {"猫", "猫", nullptr},
      {"静かな", "静か", nullptr},
      {"本", "", nullptr},
  };
  for (const Case& c : cases) {
    const auto got = conjugationOf(c.surface, c.lemma, kSentenceEnd);
    if (!c.name) {
      EXPECT_FALSE(got) << c.surface << " → " << (got ? got->name : "");
      continue;
    }
    EXPECT_TRUE(got) << c.surface;
    if (!got) continue;
    EXPECT_EQ(got->name, c.name) << c.surface;
    EXPECT_EQ(got->steps.back().form, c.surface) << c.surface;
  }
}

TEST(Conjugation, AStemTheNextCharacterShowsWasCutShortHasNoName) {
  // The form and the page's next character begin a longer form of the word: analyze/text may have cut it short.
  const std::tuple<const char*, const char*, const char*> cut[] = {
      {"書け", "書く", "ば"},       {"帰れ", "帰る", "ば"},
      {"書け", "書く", "る"},                                // 書けば, 帰れば, 書ける
      {"書こ", "書く", "う"},       {"帰ろ", "帰る", "う"},  // 書こう, 帰ろう
      {"書け", "書く", "な"},       {"書け", "書く", "ま"},
      {"書け", "書く", "た"},  // 書けない, 書けます, 書けた
      {"書け", "書く", "て"},       {"知れ", "知る", "な"},
      {"しれ", "しる", "な"},                                // 書けて; かもしれない
      {"なれ", "なる", "な"},       {"読め", "読む", "な"},  // なれない, 読めない
      {"食べ", "食べる", "な"},     {"食べ", "食べる", "よ"},
      {"食べ", "食べる", "ら"},  // 食べない, 食べよう, 食べられる
      {"見", "見る", "な"},         {"見", "見る", "る"},
      {"し", "する", "な"},                                // 見ない, 見る, しない
      {"勉強し", "勉強する", "な"}, {"来", "来る", "な"},  // 勉強しない, 来ない
      {"書いた", "書く", "ら"},                            // 書いたら
      {"書け", "書く", "よ"},  // 書けよう (書ける's volitional): a name lost, never a wrong one
      {"書き", "書く", "ま"},  // 書きます
  };
  for (const auto& [surface, lemma, next] : cut) {
    EXPECT_FALSE(conjugationOf(surface, lemma, next)) << surface << " + " << next;
  }
  // No form of the word starts with the two: named as before.
  EXPECT_FALSE(conjugationOf("書け", "書く", "、"));  // 字が書け、…: the potential's continuative as often
  EXPECT_EQ(conjugationOf("書け", "書く", "")->name, "imperative");
  EXPECT_FALSE(conjugationOf("書け", "書く", "ね"));  // 書けねえ: 書けない, said casually
  EXPECT_EQ(conjugationOf("書け", "書く", "。")->name, "imperative");
  EXPECT_EQ(conjugationOf("書き", "書く", "")->name, "continuative");
  EXPECT_EQ(conjugationOf("食べろ", "食べる", "よ")->name, "imperative");  // 食べろよ
  EXPECT_EQ(conjugationOf("帰ろ", "帰る", "っ")->name, "volitional");      // 帰ろっか
  EXPECT_EQ(conjugationOf("書けば", "書く", "、")->name, "-ba conditional");
  EXPECT_EQ(conjugationOf("食べた", "食べる", "よ")->name, "past");
}

TEST(Conjugation, ABareStemBeforeAKanaThatDoesntFollowItHasNoName) {
  // A potential's stem, a continuative before a negative, a past before り: split off a longer word.
  const std::tuple<const char*, const char*, const char*> split[] = {
      {"行け", "行く", "そ"},     {"書け", "書く", "ず"},     {"読め", "読む", "ぬ"},  // 行けそう, 書けず, 読めぬ
      {"帰れ", "帰る", "そ"},     {"話せ", "話す", "ず"},     {"入れ", "入る", "そ"},  // 帰れそう, 話せず, 入れそう
      {"買え", "買う", "そ"},     {"待て", "待つ", "ず"},                              // 買えそう, 待てず
      {"食べ", "食べる", "ず"},   {"見", "見る", "ず"},       {"来", "来る", "ず"},    // 食べず, 見ず, 来ず
      {"し", "する", "ず"},       {"食べ", "食べる", "ん"},                            // せず's し: しず; 食べん
      {"食べた", "食べる", "り"}, {"書いた", "書く", "り"},                            // -tari
      {"帰ろ", "帰る", "う"},     {"帰ろ", "帰る", "ぜ"},                              // 帰ろうぜ cut short
      {"食べた", "食べる", "が"}, {"見た", "見る", "が"},     {"した", "する", "が"},  // -tagaru: 食べたがる…
      {"来た", "来る", "が"},     {"いた", "いる", "が"},     {"話した", "話す", "が"},
      {"読ませた", "読む", "が"}, {"させた", "する", "が"},    // a causative's -tagaru
      {"見た", "見る", "さ"},     {"読ませた", "読む", "さ"},  // -tasa: 見たさ
      {"食べた", "食べる", "ま"},                              // 食べたまえ
      {"勝て", "勝つ", "っ"},     {"書け", "書く", "っ"},      // 勝てっこない: the potential's
  };
  for (const auto& [surface, lemma, next] : split) {
    EXPECT_FALSE(conjugationOf(surface, lemma, next)) << surface << " + " << next;
  }
  // A godan imperative before anything but kana: only before what ends the sentence or the quote (日本語が話せ、…;
  // 字が読め本も…: the potential's continuative as often).
  for (const char* next : {"、", "「", "本", "ア", "a"}) {
    EXPECT_FALSE(conjugationOf("書け", "書く", next)) << next;
  }
  EXPECT_FALSE(conjugationOf("話せ", "話す", "、"));
  EXPECT_FALSE(conjugationOf("読め", "読む", "本"));
  // A kana on the form's list, or what ends the sentence: named.
  const std::tuple<const char*, const char*, const char*, const char*> kept[] = {
      {"書け", "書く", "と", "imperative"},
      {"書け", "書く", "。", "imperative"},
      {"書け", "書く", "」", "imperative"},
      {"書け", "書く", "！", "imperative"},
      {"書け", "書く", "?", "imperative"},
      {"書け", "書く", "…", "imperative"},
      {"書け", "書く", "｡", "imperative"},
      {"書け", "書く", "”", "imperative"},
      {"書け", "書く", "‼", "imperative"},
      {"食べろ", "食べる", "、", "imperative"},  // not a stem of anything: before 、 too
      {"帰ろ", "帰る", "、", "volitional"},
      {"食べ", "食べる", "、", "continuative"},  // 食べ、飲み…: the continuative indeed
      {"書いた", "書く", "、", "past"},
      {"食べ", "食べる", "に", "continuative"},  // 食べに行く
      {"書き", "書く", "そ", "continuative"},
      {"書いた", "書く", "。", "past"},
      {"書いた", "書く", "よ", "past"},
      {"書いた", "書く", "こ", "past"},  // 書いたこと
      {"書こ", "書く", "っ", "volitional"},
      {"食べろ", "食べる", "よ", "imperative"},
      {"食べました", "食べる", "よ", "polite past"},
      {"書いた", "書く", "が", "past"},          // godan: 書いた isn't 書き + た
      {"食べろ", "食べる", "っ", "imperative"},  // 食べろって
      {"来い", "来る", "よ", "imperative"},
  };
  for (const auto& [surface, lemma, next, name] : kept) {
    const auto got = conjugationOf(surface, lemma, next);
    ASSERT_TRUE(got) << surface << " + " << next;
    EXPECT_EQ(got->name, name) << surface << " + " << next;
  }
  EXPECT_FALSE(conjugationOf("書け", "書く", "よ"));  // 書けよう, 書ける's volitional, may be it
}

TEST(Conjugation, WhatFollowsUnknownTakesTheNameALongerFormCouldHave) {
  EXPECT_FALSE(conjugationOf("書け", "書く", std::nullopt));  // 書ける, 書けば, 書けない…
  EXPECT_EQ(conjugationOf("書け", "書く", kSentenceEnd)->name, "imperative");
  EXPECT_FALSE(conjugationOf("食べ", "食べる", std::nullopt));                        // 食べる itself
  EXPECT_FALSE(conjugationOf("書いた", "書く", std::nullopt));                        // 書いたら
  EXPECT_FALSE(conjugationOf("帰れ", "帰る", std::nullopt));                          // 帰れば
  EXPECT_FALSE(conjugationOf("書かない", "書く", std::nullopt));                      // 書かないで
  EXPECT_EQ(conjugationOf("書きました", "書く", std::nullopt)->name, "polite past");  // nothing longer starts so
  EXPECT_EQ(conjugationOf("食べませんでした", "食べる", std::nullopt)->name, "polite negative past");
}

TEST(Conjugation, KnownLimits) {
  // Unreachable, pinned so a change shows: a kana godan verb ending in する (こする) is read as a する verb, so its
  // forms go unnamed (こすった); 問う's te-form is 問うて, but the rule gives 問って, so 問うて goes unnamed.
  EXPECT_FALSE(conjugationOf("こすった", "こする", kSentenceEnd));
  EXPECT_FALSE(conjugationOf("問うて", "問う", kSentenceEnd));
  EXPECT_EQ(conjugationOf("問って", "問う", kSentenceEnd)->name, "te-form");
  // A lemma whose class its spelling misreads: 居る is listed ichidan (いる), so 居ろ is "imperative", though for
  // おる (godan, the same kanji) it's the casual volitional. The lemma's reading could tell them apart; not used.
  EXPECT_EQ(conjugationOf("居ろ", "居る", kSentenceEnd)->name, "imperative");
}

TEST(Conjugation, StepsGoFromTheDictionaryFormToThePage) {
  EXPECT_EQ(forms("食べさせられた", "食べる"),
            (std::vector<std::string>{"食べさせる causative", "食べさせられる passive", "食べさせられた past"}));
  EXPECT_EQ(forms("煩わしくて", "煩わしい"),
            (std::vector<std::string>{"煩わしく adverbial stem", "煩わしくて te-form"}));
  EXPECT_EQ(forms("食べていた", "食べる"),
            (std::vector<std::string>{"食べて te-form", "食べている progressive (-te iru)", "食べていた past"}));
  EXPECT_EQ(forms("書きませんでした", "書く"),
            (std::vector<std::string>{"書きます polite (-masu)", "書きません negative", "書きませんでした past"}));
}

TEST(Conjugation, AVerbWhoseClassTheLemmaDoesntShowIsToldByItsForm) {
  // 切る (godan) and 着る (ichidan) look alike in kanji: the form decides.
  for (const auto& [surface, lemma, name] :
       {std::tuple{"切って", "切る", "te-form"}, std::tuple{"着て", "着る", "te-form"},
        std::tuple{"帰らない", "帰る", "negative"}, std::tuple{"見ない", "見る", "negative"}}) {
    const auto got = conjugationOf(surface, lemma, kSentenceEnd);
    ASSERT_TRUE(got) << surface;
    EXPECT_EQ(got->name, name) << surface;
  }
}

TEST(Conjugation, ARuVerbsClassFromItsSpelling) {
  using lexipoint::text::RuVerbClass;
  using lexipoint::text::ruVerbClass;
  EXPECT_EQ(ruVerbClass("食べる"), RuVerbClass::Ichidan);  // kanji-written, e-row kana before る
  EXPECT_EQ(ruVerbClass("起きる"), RuVerbClass::Ichidan);
  EXPECT_EQ(ruVerbClass("見る"), RuVerbClass::Ichidan);  // a kanji before る, listed ichidan
  EXPECT_EQ(ruVerbClass("着る"), RuVerbClass::Ichidan);
  EXPECT_EQ(ruVerbClass("申し出る"), RuVerbClass::Ichidan);  // a compound of one
  EXPECT_EQ(ruVerbClass("心得る"), RuVerbClass::Ichidan);
  EXPECT_EQ(ruVerbClass("切る"), RuVerbClass::Godan);  // a kanji before る, not listed
  EXPECT_EQ(ruVerbClass("取る"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("帰る"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("混じる"), RuVerbClass::Godan);  // listed godan, i-row kana
  EXPECT_EQ(ruVerbClass("入り混じる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("捻じる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("しゃべる"), RuVerbClass::Godan);  // on the kana list
  EXPECT_EQ(ruVerbClass("かえる"), RuVerbClass::Either);   // kana alone: 帰る or 変える
  EXPECT_EQ(ruVerbClass("おちる"), RuVerbClass::Either);
  EXPECT_EQ(ruVerbClass("すべる"), RuVerbClass::Either);    // 滑る or 統べる
  EXPECT_EQ(ruVerbClass("ひねる"), RuVerbClass::Either);    // 捻る or 陳ねる
  EXPECT_EQ(ruVerbClass("いじる"), RuVerbClass::Godan);     // whole run only ...
  EXPECT_EQ(ruVerbClass("めいじる"), RuVerbClass::Either);  // ... not 命じる in kana
  EXPECT_EQ(ruVerbClass("はいる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("はしる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("にぎる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("まいる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("かぎる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("ちぎる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("ちる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("とる"), RuVerbClass::Godan);      // o-row kana
  EXPECT_EQ(ruVerbClass("サボる"), RuVerbClass::Godan);    // katakana o-row
  EXPECT_EQ(ruVerbClass("落ちる"), RuVerbClass::Ichidan);  // a kanji directly before the i/e-row kana
  EXPECT_EQ(ruVerbClass("生きる"), RuVerbClass::Ichidan);
  EXPECT_EQ(ruVerbClass("出来る"), RuVerbClass::Ichidan);  // listed
  // Two or more kana after the last kanji: the run decides, like a kana-only verb
  EXPECT_EQ(ruVerbClass("見くびる"), RuVerbClass::Godan);  // ends in a listed kana godan verb
  EXPECT_EQ(ruVerbClass("踏みにじる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("ふみにじる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("食いちぎる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("煮えたぎる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("使いきる"), RuVerbClass::Either);  // 切る or 着る
  EXPECT_EQ(ruVerbClass("見つける"), RuVerbClass::Either);
  EXPECT_EQ(ruVerbClass("生まれる"), RuVerbClass::Either);
  EXPECT_EQ(ruVerbClass("しる"), RuVerbClass::Godan);     // a whole-run entry ...
  EXPECT_EQ(ruVerbClass("おちる"), RuVerbClass::Either);  // ... that doesn't match a longer run
  EXPECT_EQ(ruVerbClass("みちる"), RuVerbClass::Either);
  EXPECT_EQ(ruVerbClass("キレる"), RuVerbClass::Either);  // katakana, folded
  EXPECT_EQ(ruVerbClass("バレる"), RuVerbClass::Either);
  EXPECT_EQ(ruVerbClass("シャベる"), RuVerbClass::Godan);  // folded, then listed
  EXPECT_EQ(ruVerbClass("〆る"), RuVerbClass::Either);     // not kana, not a kanji: the spelling says nothing
  EXPECT_EQ(ruVerbClass("々る"), RuVerbClass::Either);
  EXPECT_EQ(ruVerbClass("蘇える"), RuVerbClass::Godan);  // okurigana spellings of godan verbs, listed
  EXPECT_EQ(ruVerbClass("甦える"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("罵しる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("油ぎる"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("ヶる"), RuVerbClass::Either);   // ヶ is kana by Unicode, but not a sound
  EXPECT_EQ(ruVerbClass("入いる"), RuVerbClass::Godan);  // old okurigana: godan, listed
  EXPECT_EQ(ruVerbClass("帰える"), RuVerbClass::Godan);
  EXPECT_EQ(ruVerbClass("顧る"), RuVerbClass::Ichidan);  // old spelling of 顧みる: ichidan, listed
  EXPECT_EQ(ruVerbClass("起る"), RuVerbClass::Godan);    // 起こる or 起きる: left to the kanji rule
}

TEST(Conjugation, AProgressivesPastBeforeAKanaThatCouldGoOnHasNoName) {
  // 見ていたかった, 見ていたければ, 見ていたがる: the search builds no -tai / -tagaru from a progressive.
  EXPECT_FALSE(conjugationOf("見ていた", "見る", "か"));
  EXPECT_FALSE(conjugationOf("見ていた", "見る", "け"));
  EXPECT_FALSE(conjugationOf("見ていた", "見る", "が"));
  EXPECT_FALSE(conjugationOf("食べてた", "食べる", "か"));
  EXPECT_FALSE(conjugationOf("知ってた", "知る", "が"));
  EXPECT_FALSE(conjugationOf("勉強していた", "勉強する", "か"));
  EXPECT_EQ(conjugationOf("見ていた", "見る", "よ")->name, "progressive past");
  EXPECT_EQ(conjugationOf("書いていた", "書く", "の")->name, "progressive past");
}

namespace {

using lexipoint::text::followersOf;
using lexipoint::text::ShortForm;

// Every hiragana letter, ぁ to ゖ.
std::vector<std::string> allHiragana() {
  std::vector<std::string> out;
  out.reserve(0x3096 - 0x3041 + 1);
  for (uint32_t cp = 0x3041; cp <= 0x3096; cp++) {
    out.push_back({static_cast<char>(0xE0 | (cp >> 12)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
                   static_cast<char>(0x80 | (cp & 0x3F))});
  }
  return out;
}

struct Short {
  ShortForm form;
  const char* surface;
  const char* lemma;
  const char* name;  // at a sentence's end
};

// Forms ending in each short form, from plain and derived verbs.
const Short kShortForms[] = {
    {ShortForm::GodanImperative, "書け", "書く", "imperative"},
    {ShortForm::GodanImperative, "帰れ", "帰る", "imperative"},
    {ShortForm::GodanImperative, "話せ", "話す", "imperative"},
    {ShortForm::GodanImperative, "行け", "行く", "imperative"},
    {ShortForm::GodanImperative, "食べたがれ", "食べる", "-tagaru imperative"},
    {ShortForm::Imperative, "食べろ", "食べる", "imperative"},
    {ShortForm::Imperative, "しろ", "する", "imperative"},
    {ShortForm::Imperative, "来い", "来る", "imperative"},
    {ShortForm::Imperative, "書かせろ", "書く", "causative imperative"},
    {ShortForm::Imperative, "書かれろ", "書く", "passive imperative"},
    {ShortForm::Imperative, "食べさせろ", "食べる", "causative imperative"},
    {ShortForm::CasualVolitional, "帰ろ", "帰る", "volitional"},
    {ShortForm::CasualVolitional, "書こ", "書く", "volitional"},
    {ShortForm::CasualVolitional, "食べたがろ", "食べる", "-tagaru volitional"},
    {ShortForm::Continuative, "食べ", "食べる", "continuative"},
    {ShortForm::Continuative, "書き", "書く", "continuative"},
    {ShortForm::Continuative, "見", "見る", "continuative"},
    {ShortForm::Continuative, "し", "する", "continuative"},
    {ShortForm::Continuative, "勉強し", "勉強する", "continuative"},
    {ShortForm::Continuative, "来", "来る", "continuative"},
    {ShortForm::Past, "食べた", "食べる", "past"},
    {ShortForm::Past, "書いた", "書く", "past"},
    {ShortForm::Past, "話した", "話す", "past"},
    {ShortForm::Past, "した", "する", "past"},
    {ShortForm::Past, "来た", "来る", "past"},
    {ShortForm::Past, "読ませた", "読む", "causative past"},
    {ShortForm::Past, "書かれた", "書く", "passive past"},
    {ShortForm::Past, "書けた", "書く", "potential past"},
    {ShortForm::Past, "食べさせられた", "食べる", "causative-passive past"},
    {ShortForm::Past, "食べたがった", "食べる", "-tagaru past"},
    {ShortForm::Past, "食べました", "食べる", "polite past"},
    {ShortForm::Past, "食べなかった", "食べる", "negative past"},
    {ShortForm::Past, "高かった", "高い", "past"},
    {ShortForm::ProgressivePast, "見ていた", "見る", "progressive past"},
    {ShortForm::ProgressivePast, "食べてた", "食べる", "progressive past"},
    {ShortForm::ProgressivePast, "書いていた", "書く", "progressive past"},
    {ShortForm::ProgressivePast, "勉強していた", "勉強する", "progressive past"},
    {ShortForm::ProgressivePast, "知ってた", "知る", "progressive past"},
    {ShortForm::ProgressivePast, "書かれていた", "書く", "passive progressive past"},
    {ShortForm::Volitional, "食べよう", "食べる", "volitional"},
    {ShortForm::Volitional, "しよう", "する", "volitional"},
    {ShortForm::Volitional, "来よう", "来る", "volitional"},
    {ShortForm::Volitional, "書かせよう", "書く", "causative volitional"},
    {ShortForm::Volitional, "勉強しよう", "勉強", "volitional"},
};

// The lists as audited (Conjugation.cpp gives each kana's reason): a kana added to a list without auditing it here
// fails, and so does one on this list known to start another form.
struct Audited {
  ShortForm form;
  std::vector<std::string> followers;
  std::vector<std::string> neverFollowers;  // start another form of the word on some reading
};
const Audited kAudited[] = {
    {ShortForm::GodanImperative, {"よ", "と"}, {"っ", "ば", "る", "な", "ま", "た", "て", "ず", "ぬ", "そ", "ね"}},
    {ShortForm::Imperative, {"よ", "と", "っ"}, {}},
    {ShortForm::CasualVolitional, {"っ", "か", "と", "よ"}, {"う"}},
    {ShortForm::Continuative,
     {"に", "つ", "そ", "や", "す", "は", "も", "か"},
     {"な", "よ", "ら", "さ", "ろ", "れ", "ま", "て", "た", "ず", "ぬ", "ん"}},
    {ShortForm::Past,
     {"よ", "と", "か", "ね", "の", "ん", "っ", "け", "が", "し", "も",
      "こ", "は", "わ", "ぞ", "な", "だ", "で", "じ", "ほ", "ば", "せ"},
     {"い", "く", "さ", "げ", "そ", "ま", "ら", "り"}},
    {ShortForm::ProgressivePast,
     {"よ", "と", "ね", "の", "ん", "っ", "し", "も", "こ", "は", "わ", "ぞ", "な", "だ", "で", "じ", "ほ", "ば", "せ"},
     {"い", "く", "か", "け", "が", "さ", "げ", "そ", "ま", "ら", "り"}},
    {ShortForm::Volitional, {"と", "か", "よ", "ね", "っ", "ぜ", "な"}, {"が", "も", "に"}},
};

}  // namespace

TEST(Conjugation, EachShortFormsFollowersAreAudited) {
  const std::vector<std::string> hiragana = allHiragana();
  for (const Audited& a : kAudited) {
    const auto followers = followersOf(a.form);
    std::vector<std::string> listed;
    for (const std::string_view kana : followers) {
      EXPECT_NE(std::find(hiragana.begin(), hiragana.end(), kana), hiragana.end()) << kana << ": not one hiragana";
      listed.emplace_back(kana);
    }
    EXPECT_EQ(listed, a.followers) << static_cast<int>(a.form) << ": audit the change";
    for (const std::string& bad : a.neverFollowers) {
      EXPECT_EQ(std::find(listed.begin(), listed.end(), bad), listed.end()) << bad << " starts another form";
    }
  }
  EXPECT_TRUE(followersOf(ShortForm::None).empty());
}

TEST(Conjugation, AShortFormKeepsItsNameOnlyBeforeItsFollowers) {
  const std::vector<std::string> hiragana = allHiragana();
  for (const Short& s : kShortForms) {
    const auto end = conjugationOf(s.surface, s.lemma, kSentenceEnd);
    ASSERT_TRUE(end) << s.surface;
    EXPECT_EQ(end->name, s.name) << s.surface;
    const auto followers = followersOf(s.form);
    for (const std::string& kana : hiragana) {
      const auto got = conjugationOf(s.surface, s.lemma, kana);
      if (std::find(followers.begin(), followers.end(), kana) == followers.end()) {
        EXPECT_FALSE(got) << s.surface << " + " << kana;  // may be the rest of another form
      } else if (got) {
        EXPECT_EQ(got->name, s.name) << s.surface << " + " << kana;  // named as at the end, or not at all
      }
    }
  }
}

TEST(Conjugation, KaKeGaAfterEveryMasuStemPastHaveNoName) {
  // か け が are on kAfterPast only because -tai / -tagaru are built from every masu stem + た past, so the
  // longer-form check catches 食べたかった, 食べたければ, 食べたがる. Every such past, plain and derived:
  const std::pair<const char*, const char*> pasts[] = {
      {"食べた", "食べる"},     {"見た", "見る"},     {"した", "する"},           {"来た", "来る"},
      {"きた", "くる"},         {"話した", "話す"},   {"書かれた", "書く"},       {"書けた", "書く"},
      {"食べられた", "食べる"}, {"読ませた", "読む"}, {"読ませられた", "読む"},   {"読まされた", "読む"},
      {"させた", "する"},       {"来させた", "来る"}, {"勉強させた", "勉強する"}, {"食べさせられた", "食べる"},
  };
  for (const auto& [surface, lemma] : pasts) {
    ASSERT_TRUE(conjugationOf(surface, lemma, kSentenceEnd)) << surface;
    for (const char* kana : {"か", "け", "が"}) EXPECT_FALSE(conjugationOf(surface, lemma, kana)) << surface << kana;
  }
  // Not a masu stem + た: -tagaru's own past (食べたがっ-た) and a -tai past keep the name before them.
  EXPECT_EQ(conjugationOf("食べたがった", "食べる", "か")->name, "-tagaru past");
  EXPECT_EQ(conjugationOf("食べたかった", "食べる", "か")->name, "-tai past");
}

TEST(Conjugation, AFiveStepFormANextStepGoesOnFromHasNoName) {
  // Five steps, the most the search follows; the sixth is still made for the longer-form check.
  EXPECT_EQ(conjugationOf("食べさせられていません", "食べる", kSentenceEnd)->name,
            "causative-passive progressive polite negative");
  EXPECT_FALSE(conjugationOf("食べさせられていません", "食べる", "で"));  // …でした
  EXPECT_FALSE(conjugationOf("食べさせられていません", "食べる", std::nullopt));
  EXPECT_EQ(conjugationOf("食べさせられたくない", "食べる", kSentenceEnd)->name, "causative-passive -tai negative");
  EXPECT_FALSE(conjugationOf("食べさせられたくない", "食べる", "で"));  // …ないで
  EXPECT_FALSE(conjugationOf("食べさせられたくない", "食べる", std::nullopt));
}

TEST(Conjugation, TheLongestChainTheLongerFormCheckNeedsIsBuilt) {
  // Causative, passive, -tai, past: 食べさせられた + か is caught only if 食べさせられたかった is reached.
  EXPECT_EQ(conjugationOf("食べさせられたかった", "食べる", kSentenceEnd)->name, "causative-passive -tai past");
  EXPECT_EQ(conjugationOf("食べさせられたがった", "食べる", kSentenceEnd)->name, "causative-passive -tagaru past");
}

TEST(Conjugation, EachShortFormBeforeAKanaStartingAnotherOfItsFormsHasNoName) {
  struct Other {
    const char* surface;
    const char* lemma;
    const char* next;
    const char* why;
  };
  const Other others[] = {
      // Godan imperative: also the potential's stem
      {"書け", "書く", "る", "書ける"},
      {"書け", "書く", "ば", "書けば"},
      {"書け", "書く", "な", "書けない"},
      {"書け", "書く", "っ", "書けっこない"},
      {"書け", "書く", "そ", "書けそう"},
      {"書け", "書く", "ず", "書けず"},
      {"書け", "書く", "ね", "書けねえ"},
      // Other imperatives (ろ, しろ, 来い): nothing is built on them, so no pair exists; the audit test keeps their
      // list to よ と っ.
      // Casual volitional
      {"帰ろ", "帰る", "う", "帰ろう"},
      // Continuative: also every other stem of an ichidan-type verb
      {"食べ", "食べる", "な", "食べない"},
      {"食べ", "食べる", "よ", "食べよう"},
      {"食べ", "食べる", "ら", "食べられる"},
      {"食べ", "食べる", "さ", "食べさせる"},
      {"食べ", "食べる", "ろ", "食べろ"},
      {"食べ", "食べる", "れ", "食べれる"},
      {"食べ", "食べる", "ま", "食べます"},
      {"食べ", "食べる", "て", "食べて"},
      {"食べ", "食べる", "た", "食べた"},
      {"食べ", "食べる", "ず", "食べず"},
      {"食べ", "食べる", "ぬ", "食べぬ"},
      {"食べ", "食べる", "ん", "食べん"},
      {"来", "来る", "な", "来ない (こ)"},
      // Past
      {"食べた", "食べる", "い", "食べたい"},
      {"食べた", "食べる", "く", "食べたくない"},
      {"食べた", "食べる", "か", "食べたかった"},
      {"食べた", "食べる", "け", "食べたければ"},
      {"食べた", "食べる", "が", "食べたがる"},
      {"見た", "見る", "さ", "見たさ"},
      {"食べた", "食べる", "げ", "食べたげ"},
      {"食べた", "食べる", "そ", "食べたそう"},
      {"食べた", "食べる", "ま", "食べたまえ"},
      {"書いた", "書く", "ら", "書いたら"},
      {"書いた", "書く", "り", "書いたり"},
      // A progressive's past: no -tai / -tagaru built from it
      {"見ていた", "見る", "か", "見ていたかった"},
      {"見ていた", "見る", "け", "見ていたければ"},
      {"見ていた", "見る", "が", "見ていたがる"},
      {"見ていた", "見る", "い", "見ていたい"},
      {"見ていた", "見る", "ら", "見ていたら"},
      // A volitional that isn't godan: also the continuative + よう "way"
      {"しよう", "する", "が", "しようがない"},
      {"食べよう", "食べる", "も", "食べようもない"},
      {"勉強しよう", "勉強", "が", "勉強しようがない"},
  };
  for (const Other& o : others) EXPECT_FALSE(conjugationOf(o.surface, o.lemma, o.next)) << o.surface << " + " << o.why;
}

TEST(Conjugation, TheTokensAnalyzeTextGaveNameAsMeasured) {
  // The tokens analyze/text gave (surface‹lemma› and the page's next character), measured from the Mac on 2026-09-27:
  // docs/reference/lexirise-api-notes.md, "How analyze/text splits conjugated verbs". nullptr: no name.
  struct Token {
    const char* surface;
    const char* lemma;
    const char* next;
    const char* name;
  };
  const Token tokens[] = {
      {"書けない", "書く", "。", "potential negative"},
      {"書けば", "書く", "い", "-ba conditional"},
      {"書こう", "書く", "。", "volitional"},
      {"行け", "行く", "そ", nullptr},  // 行け · そう: the split the follower rule guards against
      {"行けそうだ", "行く", "。", nullptr},
      {"書けず", "書く", "に", nullptr},
      {"食べたがる", "食べる", "。", "-tagaru"},
      {"したがる", "する", "。", "-tagaru"},
      {"てっこない", "てる", "。", nullptr},  // 勝 · てっこない: a bad split
      {"見ていたかった", "見る", "。", nullptr},
      {"降るかもしれない", "降る", "。", nullptr},
      {"食べないで", "食べる", "学", "negative te-form"},
      {"なれない", "なれる", "。", "negative"},          // the first answer's lemma
      {"なれない", "なる", "。", "potential negative"},  // the refined answer's
      {"食べちゃう", "食べる", "よ", nullptr},
      {"着いたら", "着く", "電", "-tara conditional"},
      {"読んだり", "読む", "寝", nullptr},
      {"話せ", "話す", "、", nullptr},
      {"書け", "書く", "！", "imperative"},
      {"食べろ", "食べる", "。", "imperative"},
      {"帰ろう", "帰る", "。", "volitional"},
      {"書ける", "書く", "よ", "potential"},
      {"食べられる", "食べる", "。", "passive or potential"},
      {"呼ばれた", "呼ぶ", "。", "passive past"},
      {"起きれば", "起きる", "よ", "-ba conditional"},
      {"読んでいる", "読む", "。", "progressive"},
      {"寒くなかった", "寒い", "。", "negative past"},
      {"食べさせられていませんでした", "食べる", "。", nullptr},  // six steps: past the search's depth
      // A する verb's lemma is its noun
      {"勉強した", "勉強", "。", "past"},
      {"勉強しています", "勉強", "。", "progressive polite"},
      {"電話して", "電話", "、", "te-form"},
      {"心配させる", "心配", "。", "causative"},
      {"結婚している", "結婚", "。", "progressive"},
      {"掃除させられた", "掃除", "。", "causative-passive past"},
      // Nouns and する, measured 2026-09-27 (the section's "Nouns and する")
      {"して", "する", "笑", "te-form"},          // 二人 · して‹する› · 笑った: not merged with the noun
      {"騒い", "騒ぐ", "だ", nullptr},            // 騒い · だ: not a form of its own
      {"できた", "できる", "？", "past"},         // 彼氏 · できた‹できる›
      {"勉強できる", "勉強", "。", "potential"},  // merged: the noun lemma
      {"愛した", "愛す", "。", "past"},           // a one-kanji する verb: its own lemma, 愛す
      {"話した", "話す", "。", "past"},
      {"貸して", "貸す", "。", "te-form"},
      {"出した", "出す", "。", "past"},
      // 食べない · です: 食べない + で could be 食べないで (a form) cut short, so no name; one character can't tell
      {"食べない", "食べる", "で", nullptr},
      {"書いたらしい", "書く", "。", nullptr},  // merged whole: not a form
  };
  for (const Token& t : tokens) {
    const auto got = conjugationOf(t.surface, t.lemma, t.next);
    if (!t.name) {
      EXPECT_FALSE(got) << t.surface << "‹" << t.lemma << "› → " << (got ? got->name : "");
      continue;
    }
    ASSERT_TRUE(got) << t.surface << "‹" << t.lemma << "›";
    EXPECT_EQ(got->name, t.name) << t.surface << "‹" << t.lemma << "›";
  }
}

TEST(Conjugation, ASuruVerbGivenAsItsNounIsNamedFromNounPlusSuru) {
  const auto studied = conjugationOf("勉強した", "勉強", kSentenceEnd);
  ASSERT_TRUE(studied);
  EXPECT_EQ(studied->name, "past");
  EXPECT_EQ(studied->dictionaryForm, "勉強する");  // the Form tab starts from it
  EXPECT_EQ(conjugationOf("勉強できる", "勉強", kSentenceEnd)->name, "potential");
  EXPECT_FALSE(conjugationOf("勉強", "勉強", kSentenceEnd));           // the noun itself
  EXPECT_FALSE(conjugationOf("勉強中", "勉強", kSentenceEnd));         // the noun, then something else
  EXPECT_FALSE(conjugationOf("勉強しか", "勉強", "な"));               // し, but no form of する
  EXPECT_FALSE(conjugationOf("勉強した", "勉強", "が"));               // a short form before が still: 勉強したがる
  const auto loved = conjugationOf("愛した", "愛する", kSentenceEnd);  // a する verb's lemma already: as before
  ASSERT_TRUE(loved);
  EXPECT_EQ(loved->name, "past");
  EXPECT_EQ(loved->dictionaryForm, "愛する");
  EXPECT_EQ(conjugationOf("食べた", "食べる", kSentenceEnd)->dictionaryForm, "食べる");
}

TEST(Conjugation, ANounWithOkuriganaShiIsntASuruVerbsContinuative) {
  // Nouns written with okurigana し can come back with a lemma without it: no noun + する continuative for them.
  EXPECT_FALSE(conjugationOf("話し", "話", "は"));
  EXPECT_FALSE(conjugationOf("話し", "話", "、"));
  EXPECT_FALSE(conjugationOf("見出し", "見出", "は"));
  EXPECT_FALSE(conjugationOf("お話し", "お話", "が"));
  EXPECT_FALSE(conjugationOf("勉強し", "勉強", "、"));    // the one loss: a する verb's bare continuative
  EXPECT_FALSE(conjugationOf("勉強せず", "勉強", "。"));  // せ starts no noun + する form the gate takes
  // A noun ending in hiragana, or one kana alone, isn't taken for a する verb's
  EXPECT_FALSE(conjugationOf("高いし", "高い", "、"));
  EXPECT_FALSE(conjugationOf("静かし", "静か", "、"));
  EXPECT_FALSE(conjugationOf("でした", "で", "。"));
  EXPECT_FALSE(conjugationOf("さした", "さ", "。"));
  EXPECT_EQ(conjugationOf("勉強した", "勉強", "。")->name, "past");
  EXPECT_EQ(conjugationOf("テストした", "テスト", "。")->name, "past");
  EXPECT_EQ(conjugationOf("コピーした", "コピー", "。")->name, "past");
  // One kanji alone is a godan す verb's stem as often (話す, 出す, 貸す): not taken for a する noun
  EXPECT_FALSE(conjugationOf("話した", "話", "。"));
  EXPECT_FALSE(conjugationOf("出した", "出", "。"));
  EXPECT_FALSE(conjugationOf("貸して", "貸", "。"));
  EXPECT_EQ(conjugationOf("勉強できる", "勉強", "。")->name, "potential");
  EXPECT_EQ(conjugationOf("愛した", "愛す", "。")->name, "past");  // a one-kanji する verb, as analyze/text gives it
}

TEST(Conjugation, AVolitionalThatIsntGodanKeepsItsNameOnlyBeforeItsParticles) {
  EXPECT_FALSE(conjugationOf("しよう", "する", "が"));  // しようがない: no way to
  EXPECT_FALSE(conjugationOf("食べよう", "食べる", "も"));
  EXPECT_EQ(conjugationOf("しよう", "する", "と")->name, "volitional");
  EXPECT_EQ(conjugationOf("食べよう", "食べる", "よ")->name, "volitional");
  EXPECT_EQ(conjugationOf("食べよう", "食べる", "。")->name, "volitional");
  EXPECT_EQ(conjugationOf("書こう", "書く", "が")->name, "volitional");  // godan: 書きよう differs
}
