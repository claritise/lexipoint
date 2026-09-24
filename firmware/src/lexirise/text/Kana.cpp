#if LEXIRISE

#include "Kana.h"

#include <Utf8.h>

#include <cstdint>
#include <cstring>

namespace lexipoint::text {
namespace {

struct Syllable {
  const char* romaji;
  const char* kana;
};

// Longest first within each length; lookup tries 3, then 2, then 1 letters.
constexpr Syllable kSyllables[] = {
    // three letters
    {"kya", "きゃ"},
    {"kyu", "きゅ"},
    {"kyo", "きょ"},
    {"gya", "ぎゃ"},
    {"gyu", "ぎゅ"},
    {"gyo", "ぎょ"},
    {"sha", "しゃ"},
    {"shu", "しゅ"},
    {"sho", "しょ"},
    {"she", "しぇ"},
    {"shi", "し"},
    {"sya", "しゃ"},
    {"syu", "しゅ"},
    {"syo", "しょ"},
    {"cha", "ちゃ"},
    {"chu", "ちゅ"},
    {"cho", "ちょ"},
    {"che", "ちぇ"},
    {"chi", "ち"},
    {"tya", "ちゃ"},
    {"tyu", "ちゅ"},
    {"tyo", "ちょ"},
    {"tsu", "つ"},
    {"nya", "にゃ"},
    {"nyu", "にゅ"},
    {"nyo", "にょ"},
    {"hya", "ひゃ"},
    {"hyu", "ひゅ"},
    {"hyo", "ひょ"},
    {"bya", "びゃ"},
    {"byu", "びゅ"},
    {"byo", "びょ"},
    {"pya", "ぴゃ"},
    {"pyu", "ぴゅ"},
    {"pyo", "ぴょ"},
    {"mya", "みゃ"},
    {"myu", "みゅ"},
    {"myo", "みょ"},
    {"rya", "りゃ"},
    {"ryu", "りゅ"},
    {"ryo", "りょ"},
    {"jya", "じゃ"},
    {"jyu", "じゅ"},
    {"jyo", "じょ"},
    {"zya", "じゃ"},
    {"zyu", "じゅ"},
    {"zyo", "じょ"},
    // two letters
    {"ka", "か"},
    {"ki", "き"},
    {"ku", "く"},
    {"ke", "け"},
    {"ko", "こ"},
    {"ga", "が"},
    {"gi", "ぎ"},
    {"gu", "ぐ"},
    {"ge", "げ"},
    {"go", "ご"},
    {"sa", "さ"},
    {"si", "し"},
    {"su", "す"},
    {"se", "せ"},
    {"so", "そ"},
    {"za", "ざ"},
    {"zi", "じ"},
    {"zu", "ず"},
    {"ze", "ぜ"},
    {"zo", "ぞ"},
    {"ja", "じゃ"},
    {"ji", "じ"},
    {"ju", "じゅ"},
    {"je", "じぇ"},
    {"jo", "じょ"},
    {"ta", "た"},
    {"ti", "ち"},
    {"tu", "つ"},
    {"te", "て"},
    {"to", "と"},
    {"da", "だ"},
    {"di", "ぢ"},
    {"du", "づ"},
    {"de", "で"},
    {"do", "ど"},
    {"na", "な"},
    {"ni", "に"},
    {"nu", "ぬ"},
    {"ne", "ね"},
    {"no", "の"},
    {"ha", "は"},
    {"hi", "ひ"},
    {"hu", "ふ"},
    {"fu", "ふ"},
    {"he", "へ"},
    {"ho", "ほ"},
    {"ba", "ば"},
    {"bi", "び"},
    {"bu", "ぶ"},
    {"be", "べ"},
    {"bo", "ぼ"},
    {"pa", "ぱ"},
    {"pi", "ぴ"},
    {"pu", "ぷ"},
    {"pe", "ぺ"},
    {"po", "ぽ"},
    {"ma", "ま"},
    {"mi", "み"},
    {"mu", "む"},
    {"me", "め"},
    {"mo", "も"},
    {"ya", "や"},
    {"yu", "ゆ"},
    {"yo", "よ"},
    {"ra", "ら"},
    {"ri", "り"},
    {"ru", "る"},
    {"re", "れ"},
    {"ro", "ろ"},
    {"wa", "わ"},
    {"wo", "を"},
    // one letter
    {"a", "あ"},
    {"i", "い"},
    {"u", "う"},
    {"e", "え"},
    {"o", "お"},
};

bool isVowel(const char c) { return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }

// Macron vowels (Hepburn, for the rare mixed word): the vowel doubled, ō → おう, ē → えい.
bool macron(const uint32_t cp, std::string& out) {
  switch (cp) {
    case 0x0101:  // ā
      out += "ああ";
      return true;
    case 0x012B:  // ī
      out += "いい";
      return true;
    case 0x016B:  // ū
      out += "うう";
      return true;
    case 0x0113:  // ē
      out += "えい";
      return true;
    case 0x014D:  // ō
      out += "おう";
      return true;
    default:
      return false;
  }
}

// ASCII letters lower-cased, macron vowels kept as their codepoints; anything else → nullopt.
std::optional<std::u32string> normalise(const std::string_view romaji) {
  std::u32string out;
  const std::string text(romaji);
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    if (cp >= 'A' && cp <= 'Z') {
      out += static_cast<char32_t>(cp - 'A' + 'a');
    } else if (cp == 0x0100 || cp == 0x0112 || cp == 0x012A || cp == 0x014C || cp == 0x016A) {
      out += static_cast<char32_t>(cp + 1);  // Ā Ē Ī Ō Ū → ā ē ī ō ū
    } else if ((cp >= 'a' && cp <= 'z') || cp == '\'') {
      out += static_cast<char32_t>(cp);
    } else if (std::string probe; macron(cp, probe)) {
      out += static_cast<char32_t>(cp);
    } else {
      return std::nullopt;
    }
  }
  return out;
}

struct MacronMatch {
  std::string kana;
  size_t length;
};

// At i: one to three ASCII letters and then a macron vowel, as one syllable (kō → こう, kyō → きょう).
template <typename Ascii>
std::optional<MacronMatch> macronSyllable(const std::u32string& s, const size_t i, const Ascii& ascii) {
  static constexpr struct {
    char32_t macron;
    char vowel;
    const char* tail;
  } kMacrons[] = {
      {0x0101, 'a', "あ"}, {0x012B, 'i', "い"}, {0x016B, 'u', "う"}, {0x0113, 'e', "い"}, {0x014D, 'o', "う"}};
  for (size_t letters = 1; letters <= 3; letters++) {
    const size_t at = i + letters;
    if (at >= s.size() || ascii(at) != '\0') continue;
    for (size_t k = i; k < at; k++) {
      if (ascii(k) == '\0') return std::nullopt;
    }
    for (const auto& m : kMacrons) {
      if (m.macron != s[at]) continue;
      std::string plain;
      for (size_t k = i; k < at; k++) plain += ascii(k);
      plain += m.vowel;
      for (const Syllable& syllable : kSyllables) {
        if (plain == syllable.romaji) return MacronMatch{std::string(syllable.kana) + m.tail, letters + 1};
      }
    }
    return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::string> romajiToHiragana(const std::string_view romaji) {
  const auto text = normalise(romaji);
  if (!text || text->empty()) return std::nullopt;
  const std::u32string& s = *text;
  const auto ascii = [&s](const size_t i) -> char {
    return i < s.size() && s[i] < 0x80 ? static_cast<char>(s[i]) : '\0';
  };
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    const char c = ascii(i);
    if (c == '\0') {  // a macron vowel on its own
      if (!macron(s[i], out)) return std::nullopt;
      i++;
      continue;
    }
    if (c == '\'') return std::nullopt;  // only valid after n (handled there)
    // ん: n' (before a vowel or y), or n before a consonant / at the end. "nn" + vowel is ん + な-row.
    if (c == 'n') {
      const char next = ascii(i + 1);
      if (next == '\'') {
        out += "ん";
        i += 2;
        continue;
      }
      const bool macronNext = i + 1 < s.size() && next == '\0';  // nō: の + う, not ん
      if (!isVowel(next) && next != 'y' && !macronNext) {
        out += "ん";
        i++;
        continue;
      }
    }
    // ん as m before b, m or p (traditional Hepburn: shimbun, tempura).
    if (c == 'm' && (ascii(i + 1) == 'b' || ascii(i + 1) == 'm' || ascii(i + 1) == 'p')) {
      out += "ん";
      i++;
      continue;
    }
    // っ: a doubled consonant (kk, tt, ss, pp …) or tch.
    if (!isVowel(c) && c != 'n' && (ascii(i + 1) == c || (c == 't' && ascii(i + 1) == 'c'))) {
      out += "っ";
      i++;
      continue;
    }
    bool matched = false;
    for (size_t len = 3; len >= 1 && !matched; len--) {
      if (i + len > s.size()) continue;
      for (const Syllable& syllable : kSyllables) {
        if (std::strlen(syllable.romaji) != len) continue;
        size_t k = 0;
        while (k < len && ascii(i + k) == syllable.romaji[k]) k++;
        if (k != len) continue;
        out += syllable.kana;
        i += len;
        matched = true;
        break;
      }
    }
    if (matched) continue;
    // A syllable ending in a macron vowel (kō, kyō, shō): its plain form, then the vowel's second half.
    if (const auto m = macronSyllable(s, i, ascii)) {
      out += m->kana;
      i += m->length;
      continue;
    }
    return std::nullopt;
  }
  return out;
}

bool isAllKatakana(const std::string_view text) {
  if (text.empty()) return false;
  const std::string s(text);
  const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    const bool katakana =
        (cp >= 0x30A1 && cp <= 0x30FA) || cp == 0x30FC || cp == 0x30FB || cp == 0x30FD || cp == 0x30FE;
    if (!katakana) return false;
  }
  return true;
}

std::optional<std::string> kanaReading(const std::string_view surface, const std::string_view romaji) {
  if (isAllKatakana(surface)) return std::string(surface);
  return romajiToHiragana(romaji);
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
