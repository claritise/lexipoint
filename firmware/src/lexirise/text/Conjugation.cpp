#if LEXIRISE

// Written for Lexipoint from Japanese grammar (the textbook rules for each verb class), not from any other
// project's rule tables. The verb lists below (which る verbs are godan or ichidan where the spelling doesn't
// show it) are well-known words written down from grammar knowledge, not copied from any dictionary's or
// deinflector's data file.

#include "Conjugation.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "CharClass.h"
#include "Kana.h"
#include "Punctuation.h"
#include "Utf8Prefix.h"

namespace lexipoint::text {
namespace {

constexpr size_t kKanaBytes = 3;  // one kana in UTF-8
constexpr int kMaxSteps = 5;      // 食べさせられませんでした: causative, passive, polite, negative, past
// The longer-form check reaches the stems' other forms only if they're built: the deepest it needs is causative,
// passive, -tai, past (食べさせられた + か: 食べさせられたかった).
static_assert(kMaxSteps >= 4);
constexpr size_t kNextsExpected = 24;  // the steps one form can take (a verb's: about 20)

// What a word conjugates as.
enum class Kind : uint8_t {
  Godan,         // 書く: the last kana changes row
  Ichidan,       // 食べる: る drops
  Suru,          // する, 勉強する
  Kuru,          // 来る, くる
  Adjective,     // 煩わしい (and -tai, -nai forms)
  Adverbial,     // 煩わしく: takes て, ない
  Nai,           // 食べない: an adjective whose past is なかった
  Masu,          // 食べます
  MasuNegative,  // 食べません
  Final,         // nothing follows
};

// A godan verb's last kana and its rows: a (negative), i (polite), e (potential), o (volitional), and the te / ta
// endings with their sound change.
struct GodanRow {
  const char* u;
  const char* a;
  const char* i;
  const char* e;
  const char* o;
  const char* te;
  const char* ta;
};
constexpr GodanRow kGodanRows[] = {
    {"う", "わ", "い", "え", "お", "って", "った"}, {"く", "か", "き", "け", "こ", "いて", "いた"},
    {"ぐ", "が", "ぎ", "げ", "ご", "いで", "いだ"}, {"す", "さ", "し", "せ", "そ", "して", "した"},
    {"つ", "た", "ち", "て", "と", "って", "った"}, {"ぬ", "な", "に", "ね", "の", "んで", "んだ"},
    {"ぶ", "ば", "び", "べ", "ぼ", "んで", "んだ"}, {"む", "ま", "み", "め", "も", "んで", "んだ"},
    {"る", "ら", "り", "れ", "ろ", "って", "った"},
};

// The i- and e-row hiragana an ichidan verb's stem ends in (食べる, 見る in kana: みる); katakana is folded to
// hiragana before the lookup (キレる).
constexpr std::string_view kIchidanRowKana[] = {"い", "き", "ぎ", "し", "じ", "ち", "ぢ", "に", "ひ",
                                                "び", "ぴ", "み", "り", "え", "け", "げ", "せ", "ぜ",
                                                "て", "で", "ね", "へ", "べ", "ぺ", "め", "れ"};

// Verbs written with a kanji right before る hide the vowel before it: most are godan (取る, 作る, 帰る, 切る), so
// that's the default, and these ichidan ones (and compounds ending in them: 夢見る, 飛び出る) are the exceptions.
// 出来る is here, not with 来る: it conjugates as ichidan (出来ない, 出来た), its 来 read き throughout; 顧る, 省る,
// 試る are old spellings of 顧みる, 省みる, 試みる.
constexpr std::string_view kIchidanKanjiRu[] = {"見る", "着る", "寝る",   "出る", "似る", "煮る", "居る",
                                                "得る", "経る", "射る",   "干る", "診る", "観る", "看る",
                                                "視る", "鋳る", "出来る", "顧る", "省る", "試る"};

// Godan verbs with a kanji directly before an i/e-row kana before る (and compounds ending in them: 入り混じる),
// rarer okurigana spellings of godan verbs among them (蘇える, 罵しる). Every other verb spelled that way is ichidan
// (食べる, 起きる).
constexpr std::string_view kGodanIERuKanji[] = {
    "混じる", "交じる", "雑じる", "捩じる", "捻じる", "脂ぎる", "油ぎる", "罵しる", "蘇える", "甦える", "入いる",
    "陥いる", "翻える", "覆える", "嘲ける", "攀じる", "喋べる", "遮ぎる", "限ぎる", "握ぎる", "漲ぎる", "帰える"};

// Godan verbs whose kana after the last kanji (or whole kana spelling) is an i/e-row kana before る, with no ichidan
// verb spelled the same (しゃべる, not かえる: 帰る or 変える). A kana run ending in one is that verb or a compound
// of it (見くびる, 踏みにじる, 食いちぎる): no ichidan verb ends in one of these (the ichidan じる verbs end in
// んじる or a long vowel + じる: 信じる, 命じる). Left off, as either class: すべる (滑る, or ichidan 統べる),
// ひねる (捻る, or ichidan 陳ねる).
constexpr std::string_view kGodanIERuKana[] = {"しゃべる", "はいる", "はしる", "にぎる", "まいる",
                                               "かぎる",   "くびる", "にじる", "ちぎる", "たぎる"};
// ... and ones that match only the whole run: ichidan verbs end in them too (おちる, みちる; めいじる: 命じる).
constexpr std::string_view kGodanIERuKanaWhole[] = {"しる", "ちる", "いじる"};

// A step's label on the Form tab, and its part of the form's name ("" when it's left out of the name).
struct Label {
  const char* step;
  const char* name;
  ShortForm stem = ShortForm::None;
};
constexpr Label kPast{"past", "past", ShortForm::Past};
constexpr Label kProgressivePast{"past", "past", ShortForm::ProgressivePast};  // 見ていた: builds no -tai/-tagaru
constexpr Label kTe{"te-form", "te-form"};
constexpr Label kProgressive{"progressive (-te iru)", "progressive"};  // a state too: 知っている, 結婚している
constexpr Label kNegative{"negative", "negative"};
constexpr Label kPolite{"polite (-masu)", "polite"};
constexpr Label kTai{"wanting to (-tai)", "-tai"};
constexpr Label kTagaru{"wanting to (-tagaru)", "-tagaru"};  // someone else's wish: 食べたがる
constexpr Label kPotential{"potential", "potential"};
constexpr Label kPassive{"passive", "passive"};
constexpr Label kPassiveOrPotential{"passive or potential", "passive or potential"};
constexpr Label kCausative{"causative", "causative"};
constexpr Label kCausativePassive{"causative-passive", "causative-passive"};
constexpr Label kVolitional{"volitional", "volitional"};
constexpr Label kNonGodanVolitional{"volitional", "volitional", ShortForm::Volitional};    // 食べよう, しよう
constexpr Label kImperative{"imperative", "imperative", ShortForm::Imperative};            // 食べろ, しろ, 来い, くれ
constexpr Label kGodanImperative{"imperative", "imperative", ShortForm::GodanImperative};  // 書け: also 書ける's stem
constexpr Label kBa{"conditional (-ba)", "-ba conditional"};
constexpr Label kTara{"conditional (-tara)", "-tara conditional"};
constexpr Label kAdverbial{"adverbial stem", "adverbial"};
constexpr Label kCasualVolitional{"volitional (casual)", "volitional",
                                  ShortForm::CasualVolitional};  // 帰ろ: without う
constexpr Label kContinuative{"continuative (-masu stem)", "continuative",
                              ShortForm::Continuative};  // 食べ, 書き: ます…

// A bare stem or short form (書け, 食べ, 帰ろ, 書いた) may be one the analysis split off a longer word: before a
// hiragana it keeps its name only when that kana is one known to follow the form itself (書けよ, 食べに行く, 書いたよ).
// Any other may be the rest of another form (行け + そう: 行けそう, potential; 書け + ず; 食べ + ず; 書いた + り:
// -tari). A kana that starts a form the search makes (書け + よ: 書けよう) is caught by the longer-form check anyway.
// Each list is safe against the stem's other readings: a godan e-row imperative is also the potential's stem (書け-る);
// an ichidan-type continuative is also every other stem (食べ-ない, 食べ-よう, 食べ-られる); an ichidan-type past
// (masu stem + た: 食べた, 話した, 読ませた) is also the stem of -tai / -tagaru / -tasa (食べたい, 食べたがる, 見たさ).
// 書けよ, 書けと: よ starts 書けよう (caught); と starts no form of 書ける. Not っ: 書けっこない (the potential's).
constexpr std::string_view kAfterGodanImperative[] = {"よ", "と"};
// 食べろよ, 食べろと, 食べろって, 来いよ: nothing is built on ろ / しろ / 来い.
constexpr std::string_view kAfterImperative[] = {"よ", "と", "っ"};
// 帰ろっか, 帰ろか, 帰ろと, 帰ろよ: only う is built on the o-row stem (帰ろう).
constexpr std::string_view kAfterCasualVolitional[] = {"っ", "か", "と", "よ"};
// 食べに行く, 食べつつ, 食べそう, 食べやすい, 食べすぎる, 食べはしない, 食べもしない, 食べかける: no form of the verb
// goes on from the stem with one of these (the others start with な よ ら さ ろ れ ま て た ず ぬ ん).
constexpr std::string_view kAfterContinuative[] = {"に", "つ", "そ", "や", "す", "は", "も", "か"};
// Particles, and nouns and endings a past form modifies (書いたよ, 書いたこと, 書いたのに, 書いたんだ, 書いたほう…).
// Masu stem + た goes on only with い く か け が (-tai, -tagaru: for every verb the search builds them from,
// 食べたかった, 食べたければ, 食べたがる are caught by the longer-form check), さ げ そ ま (見たさ, 食べたげ,
// 食べたそう, 食べたまえ: not listed), and ら り (-tara, -tari).
constexpr std::string_view kAfterPast[] = {"よ", "と", "か", "ね", "の", "ん", "っ", "け", "が", "し", "も",
                                           "こ", "は", "わ", "ぞ", "な", "だ", "で", "じ", "ほ", "ば", "せ"};
// A progressive's past (見ていた, 食べてた): the search builds no -tai / -tagaru from a progressive, so the longer-form
// check can't catch 見ていたかった, 見ていたければ, 見ていたがる: kAfterPast without か け が.
constexpr std::string_view kAfterProgressivePast[] = {"よ", "と", "ね", "の", "ん", "っ", "し", "も", "こ", "は",
                                                      "わ", "ぞ", "な", "だ", "で", "じ", "ほ", "ば", "せ"};
// An ichidan-type volitional (食べよう, しよう, 来よう) is also the continuative + よう "way": 食べようがない,
// しようもない, 書きようによって. Kept only before the volitional's own particles: 食べようと, 食べようか, 食べようよ,
// 食べようね, 食べようっと, 食べようぜ, 食べようなんて; not が, も, に.
constexpr std::string_view kAfterNonGodanVolitional[] = {"と", "か", "よ", "ね", "っ", "ぜ", "な"};

struct Word {
  std::string form;
  Kind kind = Kind::Final;
  std::string stem;               // what the endings attach to (godan: without its last kana)
  const GodanRow* row = nullptr;  // godan
  bool voiced = false;            // a potential, passive or causative-passive step was taken
  bool causative = false;         // a causative step was taken (only passive may follow)
  bool progressive = false;
  bool wanting = false;     // a -tagaru step was taken (no second: 食べたがりたがる)
  bool contracted = false;  // the progressive without い (食べてる)
};

// What only the dictionary form has: its irregularities, never carried into a verb derived from it (くれる's
// imperative くれ isn't くれさせる's くれさせ).
struct Lemma {
  bool iku = false;          // 行く: 行って, 行った
  bool kanjiKuru = true;     // 来る (else くる)
  bool eitherClass = false;  // a godan reading of a verb that may be ichidan: see the imperative below
  bool kureru = false;       // くれる, 呉れる: imperative くれ
  // A する verb made from a noun lemma (勉強 → 勉強する): no bare continuative, which would name any noun + し
  // (話し‹話›, 見出し‹見出›: nouns whose lemma comes back without their okurigana).
  bool suruFromNoun = false;
};
constexpr Lemma kDerived{};

// A way the dictionary form can conjugate.
struct Start {
  Word word;
  Lemma lemma;
};

struct Chain {
  std::vector<std::pair<std::string, Label>> steps;
  size_t start = 0;  // which dictionary form it starts from (conjugationOf's readings)
};

bool startsWithKana(const std::string_view s) { return chars::isKana(utf8FirstCodepoint(s)); }

// The kana a word ends in, after its last kanji (or other non-kana), in hiragana: 見くびる → くびる, キレる → きれる.
std::string trailingKana(const std::string_view word) {
  std::string_view rest = word;
  while (!rest.empty() && startsWithKana(utf8LastChar(rest))) rest = utf8WithoutLastChar(rest);
  return katakanaToHiragana(word.substr(rest.size()));
}

Word ichidan(std::string stem, Word base) {
  base.form = stem + "る";
  base.stem = std::move(stem);
  base.kind = Kind::Ichidan;
  base.row = nullptr;
  return base;
}

Word adjectiveLike(std::string form, const Kind kind, Word base) {
  base.stem = form.substr(0, form.size() - std::string_view("い").size());
  base.form = std::move(form);
  base.kind = kind;
  return base;
}

// A godan る verb made from another (食べたがる).
Word godanRu(std::string stem) {
  Word w;
  w.form = stem + "る";
  w.stem = std::move(stem);
  w.kind = Kind::Godan;
  static_assert(std::string_view(kGodanRows[std::size(kGodanRows) - 1].u) == "る");
  w.row = &kGodanRows[std::size(kGodanRows) - 1];
  return w;
}

Word finalWord(std::string form) {
  Word w;
  w.form = std::move(form);
  return w;
}

// Every word one step from `w`: the step's label, the next word.
struct Next {
  std::vector<std::pair<std::string, Label>> steps;  // usually one; the progressive shows its te-form first
  Word word;
};

// `lemma`: `w` is the dictionary form itself (no step taken yet) and these are its traits; nullptr for a later word.
void verbSteps(const Word& w, const Lemma* lemma, std::vector<Next>& out) {
  const bool fromDictionary = lemma != nullptr;
  const Lemma& traits = lemma ? *lemma : kDerived;
  // The stems each ending attaches to, by class.
  std::string nai, masu, te, ta, potential, passive, causative, volitional, imperative, ba, casualVolitional,
      irregularImperative;
  switch (w.kind) {
    case Kind::Godan:
      nai = w.stem + w.row->a;
      masu = w.stem + w.row->i;
      te = w.stem + (traits.iku ? "って" : w.row->te);
      ta = w.stem + (traits.iku ? "った" : w.row->ta);
      potential = w.stem + w.row->e;  // + る
      passive = w.stem + w.row->a + "れ";
      causative = w.stem + w.row->a + "せ";
      volitional = w.stem + w.row->o + "う";
      casualVolitional = w.stem + w.row->o;
      // Not when the verb may be ichidan: its 帰れ is then also the ichidan reading's regional imperative (変えれ,
      // たべれ), which no chain reaches (見れ, 食べれ go unnamed too).
      if (!traits.eitherClass) imperative = w.stem + w.row->e;
      ba = w.stem + w.row->e + "ば";
      break;
    case Kind::Ichidan:
      nai = masu = w.stem;
      if (traits.kureru) irregularImperative = w.stem;  // くれ: くれる's imperative is its stem
      te = w.stem + "て";
      ta = w.stem + "た";
      potential = w.stem + "れ";  // ら-less
      passive = w.stem + "られ";
      causative = w.stem + "させ";
      volitional = w.stem + "よう";
      imperative = w.stem + "ろ";
      ba = w.stem + "れば";
      break;
    case Kind::Suru:
      nai = masu = w.stem + "し";
      te = w.stem + "して";
      ta = w.stem + "した";
      potential = w.stem + "でき";
      passive = w.stem + "され";
      causative = w.stem + "させ";
      volitional = w.stem + "しよう";
      imperative = w.stem + "しろ";
      ba = w.stem + "すれば";
      break;
    case Kind::Kuru: {
      const std::string ko = w.stem + (traits.kanjiKuru ? "来" : "こ");
      const std::string ki = w.stem + (traits.kanjiKuru ? "来" : "き");
      nai = ko;
      masu = ki;
      te = ki + "て";
      ta = ki + "た";
      passive = ko + "られ";
      causative = ko + "させ";
      potential = ko + "れ";  // ら-less: 来れる, これる
      volitional = ko + "よう";
      imperative = ko + "い";
      ba = w.stem + (traits.kanjiKuru ? "来" : "く") + "れば";
      break;
    }
    default:
      return;
  }
  const auto add = [&out](std::string form, const Label label, Word word) {
    word.form = form;
    out.push_back({{{std::move(form), label}}, std::move(word)});
  };
  add(ta, w.progressive ? kProgressivePast : kPast, finalWord(ta));
  add(te, kTe, finalWord(te));
  // The continuative only of the dictionary form: a derived verb's (書け of 書ける) rarely stands alone, and would
  // take every imperative's name away (書け, 帰れ).
  if (fromDictionary && !traits.suruFromNoun) add(masu, kContinuative, finalWord(masu));
  if (!casualVolitional.empty()) add(casualVolitional, kCasualVolitional, finalWord(casualVolitional));
  add(ta + "ら", kTara, finalWord(ta + "ら"));
  if (!w.progressive) {  // 食べている; 書かれている too
    Word base = w;
    base.progressive = true;
    Word iru = ichidan(te + "い", base);
    out.push_back({{{te, kTe}, {iru.form, kProgressive}}, iru});
    Word contracted = ichidan(te, base);  // 食べてる; its negative 書いてない is also 書いてある's: not offered
    contracted.contracted = true;
    out.push_back({{{te, kTe}, {contracted.form, kProgressive}}, contracted});
  }
  if (!w.contracted) add(nai + "ない", kNegative, adjectiveLike(nai + "ない", Kind::Nai, w));
  Word polite = w;
  polite.form = masu + "ます";
  polite.stem = masu + "ま";
  polite.kind = Kind::Masu;
  out.push_back({{{polite.form, kPolite}}, polite});
  if (!w.progressive && !w.wanting) {
    add(masu + "たい", kTai, adjectiveLike(masu + "たい", Kind::Adjective, w));
    Word tagaru = godanRu(masu + "たが");
    tagaru.voiced = true;  // no potential, passive or causative after it (rare, and they'd multiply the search)
    tagaru.wanting = true;
    add(tagaru.form, kTagaru, tagaru);
  }
  // Not godan's (書こう): an ichidan-type volitional is also the continuative + よう "way" (しようがない).
  add(volitional, w.kind == Kind::Godan ? kVolitional : kNonGodanVolitional, finalWord(volitional));
  if (!imperative.empty()) {
    add(imperative, w.kind == Kind::Godan ? kGodanImperative : kImperative, finalWord(imperative));
  }
  if (!irregularImperative.empty()) add(irregularImperative, kImperative, finalWord(irregularImperative));
  add(ba, kBa, finalWord(ba));
  if (w.progressive) return;
  Word voiced = w;
  voiced.voiced = true;
  if (w.causative && !w.voiced) {  // 食べさせ-られる: after a causative, られる is its passive
    add(passive + "る", kPassive, ichidan(passive, voiced));
    return;
  }
  if (w.voiced || w.causative) return;
  if (w.kind == Kind::Ichidan || w.kind == Kind::Kuru) {  // られる: passive and potential alike
    add(passive + "る", kPassiveOrPotential, ichidan(passive, voiced));
  } else {
    add(passive + "る", kPassive, ichidan(passive, voiced));
  }
  if (!potential.empty()) add(potential + "る", kPotential, ichidan(potential, voiced));
  Word caused = w;
  caused.causative = true;
  add(causative + "る", kCausative, ichidan(causative, caused));
  if (w.kind == Kind::Godan && std::string_view(w.row->u) != "す") {  // 書かされる (話さされる isn't said)
    add(w.stem + w.row->a + "される", kCausativePassive, ichidan(w.stem + w.row->a + "され", voiced));
  }
}

void adjectiveSteps(const Word& w, std::vector<Next>& out) {
  const auto add = [&out](std::string form, const Label label, Word word) {
    word.form = form;
    out.push_back({{{std::move(form), label}}, std::move(word)});
  };
  switch (w.kind) {
    case Kind::Adjective:
    case Kind::Nai: {
      Word adverbial = w;
      adverbial.form = w.stem + "く";
      adverbial.kind = Kind::Adverbial;
      if (w.kind == Kind::Adjective) out.push_back({{{adverbial.form, kAdverbial}}, adverbial});
      add(w.stem + "かった", kPast, finalWord(w.stem + "かった"));
      add(w.stem + "かったら", kTara, finalWord(w.stem + "かったら"));
      add(w.stem + "ければ", kBa, finalWord(w.stem + "ければ"));
      if (w.kind == Kind::Nai) {
        add(w.stem + "くて", kTe, finalWord(w.stem + "くて"));
        add(w.form + "で", kTe, finalWord(w.form + "で"));  // 食べないで
      }
      return;
    }
    case Kind::Adverbial:
      add(w.form + "て", kTe, finalWord(w.form + "て"));
      add(w.form + "ない", kNegative, adjectiveLike(w.form + "ない", Kind::Nai, w));
      return;
    case Kind::Masu: {
      add(w.stem + "した", kPast, finalWord(w.stem + "した"));
      add(w.stem + "して", kTe, finalWord(w.stem + "して"));
      add(w.stem + "しょう", kVolitional, finalWord(w.stem + "しょう"));
      Word negative = w;
      negative.form = w.stem + "せん";
      negative.kind = Kind::MasuNegative;
      out.push_back({{{negative.form, kNegative}}, negative});
      return;
    }
    case Kind::MasuNegative:
      add(w.form + "でした", kPast, finalWord(w.form + "でした"));
      return;
    default:
      return;
  }
}

// What a search looks for, and what it finds.
struct Search {
  std::string_view surface;
  std::string cut;  // the page's form and its next character (just the form when that isn't known); empty: no check
  std::vector<Chain> found;
  size_t start = 0;         // the dictionary form being searched from, stamped on what's found
  bool longerForm = false;  // a longer form starts with `cut`: the page's form may be one cut short

  bool extends(const std::string_view form) const {
    return !cut.empty() && form.size() > surface.size() && form.starts_with(cut);
  }
};

// `depth`: the steps taken (the progressive's te-form, shown on the Form tab, isn't one of its own).
// `lemma`: the dictionary form's traits, at depth 0 only.
void search(const Word& w, const Lemma* lemma, const int depth, Chain& chain, Search& s) {
  const std::string_view surface = s.surface;
  if (s.longerForm) return;  // no name whatever else is found
  if (w.form == surface && depth > 0) {
    s.found.push_back(chain);
    s.found.back().start = s.start;
  }
  // At the last step's depth the next steps are still made for the longer-form check (食べさせられていません + で:
  // 食べさせられていませんでした), not followed.
  if (depth >= kMaxSteps && s.cut.empty()) return;
  std::vector<Next> nexts;
  nexts.reserve(kNextsExpected);
  verbSteps(w, lemma, nexts);
  adjectiveSteps(w, nexts);
  for (const Next& next : nexts) {
    for (const auto& step : next.steps) s.longerForm = s.longerForm || s.extends(step.first);
    // Only the dictionary form's continuative is shorter, by its last kana, and nothing follows it: a form longer
    // than the page's by more than that can't lead to it.
    if (depth >= kMaxSteps || next.word.form.size() > surface.size() + kKanaBytes) continue;
    for (const auto& step : next.steps) chain.steps.push_back(step);
    search(next.word, nullptr, depth + 1, chain, s);
    chain.steps.resize(chain.steps.size() - next.steps.size());
  }
}

// The name of a chain: its steps' name parts, a te-form or adverbial stem that only leads on left out, and a
// causative then a passive said as one.
std::string nameOf(const Chain& chain) {
  std::vector<std::string> parts;
  for (size_t i = 0; i < chain.steps.size(); i++) {
    const std::string_view name = chain.steps[i].second.name;
    const bool last = i + 1 == chain.steps.size();
    if (!last && (name == kTe.name || name == kAdverbial.name)) continue;
    if (name == kPassive.name && !parts.empty() && parts.back() == kCausative.name) {
      parts.back() = kCausativePassive.name;
      continue;
    }
    parts.emplace_back(name);
  }
  std::string name;
  for (const std::string& part : parts) {
    if (!name.empty()) name += ' ';
    name += part;
  }
  return name;
}

// The ways the dictionary form can conjugate.
std::vector<Start> startingWords(const std::string_view lemma) {
  std::vector<Start> words;
  words.reserve(2);
  const auto prefixBefore = [lemma](const std::string_view end) {
    return std::string(lemma.substr(0, lemma.size() - end.size()));
  };
  if (lemma.ends_with(kSuruEnding)) {
    Word w;
    w.form = std::string(lemma);
    w.kind = Kind::Suru;
    w.stem = prefixBefore(kSuruEnding);
    words.push_back({w, {}});
    return words;
  }
  if ((lemma.ends_with("来る") && !lemma.ends_with("出来る")) || lemma == "くる" || lemma.ends_with("てくる") ||
      lemma.ends_with("でくる")) {
    Word w;
    w.form = std::string(lemma);
    w.kind = Kind::Kuru;
    Lemma traits;
    traits.kanjiKuru = lemma.ends_with("来る");
    w.stem = prefixBefore(traits.kanjiKuru ? "来る" : "くる");
    words.push_back({w, traits});
    return words;
  }
  if (lemma.ends_with("い")) {
    Word w = adjectiveLike(std::string(lemma), Kind::Adjective, Word{});
    // いい and its compounds (かっこいい, 仲いい): よかった, よくない; 良い, よい and かわいい are regular.
    if (lemma.ends_with("いい") && lemma != "かわいい") w.stem = prefixBefore("いい") + "よ";
    words.push_back({w, {}});
    return words;
  }
  const std::string_view last = utf8LastChar(lemma);
  const auto row =
      std::find_if(std::begin(kGodanRows), std::end(kGodanRows), [last](const GodanRow& r) { return last == r.u; });
  if (row == std::end(kGodanRows) || lemma.size() == last.size()) return words;
  Word godan;
  godan.form = std::string(lemma);
  godan.kind = Kind::Godan;
  godan.stem = prefixBefore(last);
  godan.row = &*row;
  Lemma godanTraits;
  godanTraits.iku = lemma.ends_with("行く") || lemma.ends_with("逝く") || lemma == "いく" || lemma == "ゆく" ||
                    lemma.ends_with("ていく") || lemma.ends_with("でいく") || lemma.ends_with("てゆく") ||
                    lemma.ends_with("でゆく");
  if (last != "る") {
    words.push_back({godan, godanTraits});
    return words;
  }
  const RuVerbClass verbClass = ruVerbClass(lemma);
  const bool isGodan = verbClass != RuVerbClass::Ichidan;
  const bool isIchidan = verbClass != RuVerbClass::Godan;
  godanTraits.eitherClass = verbClass == RuVerbClass::Either;
  if (isGodan) words.push_back({godan, godanTraits});
  if (isIchidan) {
    Lemma ichidanTraits;
    ichidanTraits.kureru =
        lemma == "くれる" || lemma == "呉れる" || lemma.ends_with("てくれる") || lemma.ends_with("でくれる");
    words.push_back({ichidan(godan.stem, Word{}), ichidanTraits});
  }
  return words;
}

// What ends a sentence or a quote after an imperative (書け。 書け！ 「書け」 書け…): the sentence builder's Japanese
// terminators, its closers for every script (」』）】, ”’》, ")), the ellipsis, and a few more a book may use.
constexpr uint32_t kMoreClauseEnds[] = {
    0xFF61,  // ｡ half-width full stop
    0xFF63,  // ｣ half-width closing quote
    0x3009,  // 〉
    0x226B,  // ≫
    0x203C,  // ‼
    0x2049,  // ⁉
};
bool endsAClause(const uint32_t cp) {
  return Punctuation::isTerminator(cp, Script::Japanese) || Punctuation::isCloser(cp, Script::Japanese) ||
         Punctuation::isCloser(cp, Script::Chinese) || Punctuation::isCloser(cp, Script::Latin) ||
         Punctuation::isEllipsis(cp) ||
         std::find(std::begin(kMoreClauseEnds), std::end(kMoreClauseEnds), cp) != std::end(kMoreClauseEnds);
}

// Whether a chain ending in `last` (a bare stem or short form) may be split off a longer word, from the page's next
// character: a hiragana not on the form's list of followers (above).
bool splitOff(const Label& last, const std::optional<std::string_view> next) {
  if (!next || last.stem == ShortForm::None) return false;
  const uint32_t cp = utf8FirstCodepoint(*next);
  if (!chars::isHiragana(cp)) {
    // A godan e-row stem before anything but kana is the potential's continuative in prose as often as an imperative
    // (日本語が話せ、英語も…; 字が読め本も…): an imperative only at the sentence's end or before what ends it.
    return last.stem == ShortForm::GodanImperative && !next->empty() && !endsAClause(cp);
  }
  const std::string_view kana = utf8FirstChars(*next, 1);
  const std::span<const std::string_view> followers = followersOf(last.stem);
  return std::find(followers.begin(), followers.end(), kana) == followers.end();
}

// Whether the page's form may be a する verb whose lemma analyze/text gave as the noun alone (勉強した‹勉強›): it
// starts with the noun, the noun isn't a する verb already, and what follows it begins as a form of する does
// (し-, さ-, す-, and でき- for the potential). The noun ends in a kanji or katakana (勉強, テスト, お話, コピー):
// one ending in hiragana (高い, 静か, で, さ) is taken as it is. Only a gate: the search from noun + する decides.
bool suruVerbOfNoun(const std::string_view surface, const std::string_view lemma) {
  if (lemma.ends_with(kSuruEnding) || surface.size() <= lemma.size() || !surface.starts_with(lemma)) return false;
  const uint32_t last = utf8FirstCodepoint(utf8LastChar(lemma));
  const bool katakana = (chars::isKana(last) && !chars::isHiragana(last)) || last == chars::kProlongedSoundMark;
  if (!chars::isHan(last) && !katakana) return false;
  // One character alone: a kana (で, さ), or a kanji, which a godan す verb's stem is too (話した‹話す›, 出した,
  // 貸した); a one-kanji する verb comes back with its own lemma (愛した‹愛す›: lexirise-api-notes.md, "Nouns and
  // する").
  if (utf8WithoutLastChar(lemma).empty()) return false;
  constexpr std::string_view kSuruStarts[] = {"し", "さ", "す", "でき"};
  const std::string_view rest = surface.substr(lemma.size());
  return std::any_of(std::begin(kSuruStarts), std::end(kSuruStarts),
                     [rest](const std::string_view start) { return rest.starts_with(start); });
}

}  // namespace

std::span<const std::string_view> followersOf(const ShortForm form) {
  switch (form) {
    case ShortForm::GodanImperative:
      return kAfterGodanImperative;
    case ShortForm::Imperative:
      return kAfterImperative;
    case ShortForm::CasualVolitional:
      return kAfterCasualVolitional;
    case ShortForm::Continuative:
      return kAfterContinuative;
    case ShortForm::Past:
      return kAfterPast;
    case ShortForm::ProgressivePast:
      return kAfterProgressivePast;
    case ShortForm::Volitional:
      return kAfterNonGodanVolitional;
    case ShortForm::None:
      break;
  }
  return {};
}

RuVerbClass ruVerbClass(const std::string_view lemma) {
  // A kanji right before る: godan unless it's one of the few ichidan ones (見る, 着る; 切る, 取る
  // are godan). An i/e-row kana before it with a kanji directly before that (食べる, 起きる): ichidan unless listed
  // godan (混じる). Two or more kana after the last kanji, or kana alone: that kana run decides, as a kana-only
  // verb's would: godan when it's (or ends in) a listed kana godan verb (しゃべる, 見くびる), else either class
  // (かえる: 帰る or 変える; 見つける, 生まれる), and the page's form tells them apart. Any other kana: godan (とる).
  const std::string_view stem = utf8WithoutLastChar(lemma);
  const std::string_view before = utf8LastChar(stem);
  // ヵ ヶ are kana by Unicode but stand for 箇 / 個 (一ヶ月), not a sound a verb ends in.
  const uint32_t cp = utf8FirstCodepoint(before);
  const bool kana = chars::isKana(cp) && cp != chars::kSmallKatakanaKa && cp != chars::kSmallKatakanaKe;
  // Neither kana nor a kanji (〆る, 々, ヶ, ー, Latin, digits): the spelling says nothing, so both classes are tried.
  if (!kana && !chars::isHan(cp)) return RuVerbClass::Either;
  if (!kana) {
    const bool listed = std::any_of(std::begin(kIchidanKanjiRu), std::end(kIchidanKanjiRu),
                                    [lemma](const std::string_view w) { return lemma.ends_with(w); });
    return listed ? RuVerbClass::Ichidan : RuVerbClass::Godan;
  }
  if (std::find(std::begin(kIchidanRowKana), std::end(kIchidanRowKana), katakanaToHiragana(before)) ==
      std::end(kIchidanRowKana)) {
    return RuVerbClass::Godan;
  }
  if (chars::isHan(utf8FirstCodepoint(utf8LastChar(utf8WithoutLastChar(stem))))) {
    const bool listed = std::any_of(std::begin(kGodanIERuKanji), std::end(kGodanIERuKanji),
                                    [lemma](const std::string_view w) { return lemma.ends_with(w); });
    return listed ? RuVerbClass::Godan : RuVerbClass::Ichidan;
  }
  const std::string run = trailingKana(lemma);
  const bool listed =
      std::any_of(std::begin(kGodanIERuKana), std::end(kGodanIERuKana),
                  [&run](const std::string_view w) { return run.ends_with(w); }) ||
      std::find(std::begin(kGodanIERuKanaWhole), std::end(kGodanIERuKanaWhole), run) != std::end(kGodanIERuKanaWhole);
  return listed ? RuVerbClass::Godan : RuVerbClass::Either;
}

std::optional<Conjugation> conjugationOf(const std::string_view surface, const std::string_view lemma,
                                         const std::optional<std::string_view> next) {
  if (surface.empty() || lemma.empty() || surface == lemma) return std::nullopt;
  Search s;
  s.surface = surface;
  if (!next) {
    s.cut = std::string(surface);
  } else if (!next->empty()) {
    s.cut = std::string(surface) + std::string(*next);
  }
  // The dictionary forms to search from: the lemma, and for a する verb given as its noun (analyze/text's lemma of
  // 勉強した is 勉強: lexirise-api-notes.md, "How analyze/text splits conjugated verbs"), the noun + する when the
  // page's form goes on from the noun as a form of する does. Both are searched together: the ambiguity rule holds.
  std::vector<std::string> dictionaryForms{std::string(lemma)};
  if (suruVerbOfNoun(surface, lemma)) dictionaryForms.push_back(std::string(lemma) + std::string(kSuruEnding));
  for (size_t i = 0; i < dictionaryForms.size(); i++) {
    s.start = i;
    for (Start start : startingWords(dictionaryForms[i])) {
      start.lemma.suruFromNoun = i > 0;
      Chain chain;
      search(start.word, &start.lemma, 0, chain, s);
    }
  }
  // The page's form and the next character (or, that unknown, the form alone) begin a longer form of this word
  // (書け + な: 書けない; 見 + る: 見る): the analysis may have cut it short, and the shorter form's name would be
  // wrong.
  if (s.longerForm) return std::nullopt;
  for (const std::string& form : dictionaryForms) {
    if (s.extends(form)) return std::nullopt;
  }
  const std::vector<Chain>& found = s.found;
  if (found.empty()) return std::nullopt;
  // The shortest chain; any other that reaches the same form must mean the same, or there's no telling which.
  const auto shortest = std::min_element(
      found.begin(), found.end(), [](const Chain& a, const Chain& b) { return a.steps.size() < b.steps.size(); });
  const std::string name = nameOf(*shortest);
  for (const Chain& other : found) {
    if (nameOf(other) != name || splitOff(other.steps.back().second, next)) return std::nullopt;
  }
  Conjugation out;
  out.name = name;
  out.dictionaryForm = dictionaryForms[shortest->start];
  out.steps.reserve(shortest->steps.size());
  for (const auto& [form, label] : shortest->steps) out.steps.push_back({form, label.step});
  return out;
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
