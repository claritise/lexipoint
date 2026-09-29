#pragma once

// Japanese readings: the card shows kana, and romaji after a tap on its reading line (languages.md §3a, H10).
// Lexirise's transliteration comes in romaji or in kana (docs/reference/lexirise-api-notes.md, "Japanese reading"):
// either becomes both. Pure; tests: test/lexirise_kana.

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace lexipoint::text {

// The reading to show in kana for a word: its own surface form when that is all katakana (コーヒー:
// Lexirise's romaji has macrons there, kōhī), else the romaji converted to hiragana. nullopt when any of
// the romaji can't be converted: show the romaji then, never half-converted text.
std::optional<std::string> kanaReading(std::string_view surface, std::string_view romaji);

// A Japanese word's two readings, from Lexirise's transliteration: romaji is converted to kana (kanaReading; left as
// it is when it can't be), kana is kept (a katakana word's own surface form) and converted to romaji (kanaToRomaji;
// left as it is when it can't be). None given: a katakana word reads as itself; anything else stays empty.
struct JapaneseReading {
  std::string kana;
  std::string romaji;
};
JapaneseReading japaneseReading(std::string_view surface, std::string_view transliteration);

// Kana → romaji in Lexirise's own style: Hepburn (shi, chi, tsu, fu, ji), long vowels spelled out in hiragana
// (とうきょう → toukyou), ー as a macron (コーヒー → kōhī), ん before a vowel or y → n', っ → the next consonant
// doubled (tch before ch), a final っ not written (あっ → a), a small vowel after a syllable drawn out (ねぇ → nee),
// づ → dzu, ゝ ゞ the kana before again. Katakana is read as hiragana. nullopt on anything else (a leading small kana,
// a っ before a vowel, a ー with no vowel before it, a kanji): never half-converted text.
std::optional<std::string> kanaToRomaji(std::string_view kana);

namespace detail {
struct KanaSyllable {
  const char* kana;  // hiragana: one kana, or two (a small ゃ ゅ ょ or vowel after)
  const char* romaji;
};
std::span<const KanaSyllable> kanaSyllables();  // kanaToRomaji's table, for its test
}  // namespace detail

// Hepburn / wāpuro romaji, and Lexirise's dzu (づ), → hiragana (longest match; n' and a final or pre-consonant n → ん;
// a doubled consonant → っ; macron vowels → the vowel doubled, ō → おう). nullopt on anything unconvertible.
std::optional<std::string> romajiToHiragana(std::string_view romaji);

// All katakana letters (and ー, ・): the word is its own reading.
bool isAllKatakana(std::string_view text);

// Katakana letters folded to hiragana (キレる → きれる; ヴ → ゔ, ヽ → ゝ); ー, ヷ-ヺ and everything else kept.
std::string katakanaToHiragana(std::string_view text);

}  // namespace lexipoint::text
