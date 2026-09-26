#pragma once

// Which conjugated form a Japanese word is in on the page (C16): 食べさせられた is 食べる's causative-passive past,
// 煩わしくて 煩わしい's te-form. Written from Japanese grammar for Lexipoint (no rule data taken from any other
// project): it conjugates the dictionary form forward, a few steps deep, and names the chain that gives exactly
// the page's form. A form it can't reach, or one two different chains reach, gets no name.
// Pure; tests: test/lexirise_kana/ConjugationTest.cpp.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lexipoint::text {

// A する verb's ending (勉強する: 勉強 + する).
constexpr std::string_view kSuruEnding = "する";

struct ConjugationStep {
  std::string form;   // the word after this step (食べさせる)
  std::string label;  // what the step is ("causative")
};

struct Conjugation {
  std::string name;                    // the whole form ("causative-passive past")
  std::string dictionaryForm;          // what the steps start from: the lemma, or a する noun's lemma + する (勉強する)
  std::vector<ConjugationStep> steps;  // from the dictionary form (not included) to the page's form
};

// `surface`: the form on the page; `lemma`: its dictionary form (analyze/text's; a する verb's may be its noun alone,
// 勉強 for 勉強した: then noun + する is searched too, and `dictionaryForm` says which). nullopt when they're the same,
// or the form isn't one of the common ones below (polite, negative, past, te-form, progressive, -tai, -tagaru,
// potential, passive, causative, causative-passive, volitional and godan's casual volitional, imperative,
// conditional -ba / -tara, the dictionary form's continuative; i-adjectives' negative, past, te-form, adverbial,
// conditional), or the verb's class can't tell two readings apart. `next`: the page's character right after the form
// ("" when nothing follows it); when the form and it begin a longer form of the word (書け + な: 書けない; 見 + る:
// 見る), the analysis may have cut that form short, and the form gets no name. nullopt when what follows isn't known (a
// sentence the page cut): then any longer form of the word starting with the form (書け: 書ける, 書けば) takes the name
// away. A bare stem or short form (imperative, continuative, volitional, past) before a hiragana keeps its name only
// when that kana is known to follow the form itself (書け + よ/と/っ, 食べ + に, 書いた + よ/こ…); any other (行け +
// そう, 書け + ず, 食べ + ず, 書いた + り) may be the rest of another form. Punctuation, kanji and katakana keep it,
// except that a godan imperative (書け, also the potential's continuative: 話せ、…) keeps it only before what ends a
// sentence or a quote.
std::optional<Conjugation> conjugationOf(std::string_view surface, std::string_view lemma,
                                         std::optional<std::string_view> next);

// A bare stem or short form a step gives, whose name a following hiragana can take away (C16): the godan imperative
// (書け, also the potential's stem), the other imperatives (食べろ, しろ, 来い), the casual volitional (帰ろ), the
// continuative (食べ, 書き), the past (書いた), a progressive's past (見ていた), and a volitional that isn't godan
// (食べよう, しよう: also the continuative + よう "way", しようがない).
enum class ShortForm : uint8_t {
  None,
  GodanImperative,
  Imperative,
  CasualVolitional,
  Continuative,
  Past,
  ProgressivePast,
  Volitional
};
// The hiragana it keeps its name before (each one a single kana; empty for None). Tests audit them.
std::span<const std::string_view> followersOf(ShortForm form);

// Which class a る verb conjugates as, where its spelling hides it (from short word lists written from grammar
// knowledge, not copied from any dictionary's or deinflector's data). A kanji right before る: godan unless listed
// ichidan (見る). Anything else that isn't kana (〆る, 々, ヵ ヶ, ー, Latin): Either. An i/e-row kana before る with a
// kanji directly before it: ichidan unless listed godan (混じる). Otherwise the kana after the last kanji (katakana
// read as hiragana) decides: godan when it is, or ends in, a listed kana godan verb (しゃべる, 見くびる), else Either
// (かえる: 帰る or 変える; 見つける, キレる). Meaningful for a lemma ending in る.
enum class RuVerbClass : uint8_t { Godan, Ichidan, Either };
RuVerbClass ruVerbClass(std::string_view lemma);

}  // namespace lexipoint::text
