#pragma once

// Japanese readings: Lexirise gives romaji only, the card shows kana (languages.md §3a, H10). Pure;
// tests: test/lexirise_kana.

#include <optional>
#include <string>
#include <string_view>

namespace lexipoint::text {

// The reading to show in kana for a word: its own surface form when that is all katakana (コーヒー:
// Lexirise's romaji has macrons there, kōhī), else the romaji converted to hiragana. nullopt when any of
// the romaji can't be converted: show the romaji then, never half-converted text.
std::optional<std::string> kanaReading(std::string_view surface, std::string_view romaji);

// Hepburn / wāpuro romaji → hiragana (longest match; n' and a final or pre-consonant n → ん; a doubled
// consonant → っ; macron vowels → the vowel doubled, ō → おう). nullopt on anything unconvertible.
std::optional<std::string> romajiToHiragana(std::string_view romaji);

// All katakana letters (and ー, ・): the word is its own reading.
bool isAllKatakana(std::string_view text);

// Katakana letters folded to hiragana (キレる → きれる; ヴ → ゔ, ヽ → ゝ); ー, ヷ-ヺ and everything else kept.
std::string katakanaToHiragana(std::string_view text);

}  // namespace lexipoint::text
