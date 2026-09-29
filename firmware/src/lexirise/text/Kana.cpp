#include "Kana.h"

#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "CharClass.h"

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
    {"dzu", "づ"},  // Lexirise's づ (気づく kidzuku): exact, where Hepburn's zu is ず or づ
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

// The macron vowels: codepoint, the plain vowel, and the kana that draws it out after a syllable.
constexpr struct {
  char32_t macron;
  char vowel;
  const char* tail;
} kMacrons[] = {
    {0x0101, 'a', "あ"}, {0x012B, 'i', "い"}, {0x016B, 'u', "う"}, {0x0113, 'e', "い"}, {0x014D, 'o', "う"}};

// At i: one to three ASCII letters and then a macron vowel, as one syllable (kō → こう, kyō → きょう).
template <typename Ascii>
std::optional<MacronMatch> macronSyllable(const std::u32string& s, const size_t i, const Ascii& ascii) {
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
      const auto syllable = std::find_if(std::begin(kSyllables), std::end(kSyllables),
                                         [&plain](const Syllable& candidate) { return plain == candidate.romaji; });
      if (syllable != std::end(kSyllables)) return MacronMatch{std::string(syllable->kana) + m.tail, letters + 1};
    }
    return std::nullopt;
  }
  return std::nullopt;
}

// Hiragana → romaji, two kana (a small ゃ ゅ ょ or a small vowel after) before one. ん, っ, ー and the iteration marks
// ゝ ゞ are handled apart. Tests: Kana.EveryKanaRow (keep its list in step).
constexpr detail::KanaSyllable kKanaSyllables[] = {
    // two kana
    {"きゃ", "kya"},
    {"きゅ", "kyu"},
    {"きょ", "kyo"},
    {"ぎゃ", "gya"},
    {"ぎゅ", "gyu"},
    {"ぎょ", "gyo"},
    {"しゃ", "sha"},
    {"しゅ", "shu"},
    {"しょ", "sho"},
    {"しぇ", "she"},
    {"じゃ", "ja"},
    {"じゅ", "ju"},
    {"じょ", "jo"},
    {"じぇ", "je"},
    {"ちゃ", "cha"},
    {"ちゅ", "chu"},
    {"ちょ", "cho"},
    {"ちぇ", "che"},
    {"ぢゃ", "ja"},
    {"ぢゅ", "ju"},
    {"ぢょ", "jo"},
    {"にゃ", "nya"},
    {"にゅ", "nyu"},
    {"にょ", "nyo"},
    {"ひゃ", "hya"},
    {"ひゅ", "hyu"},
    {"ひょ", "hyo"},
    {"びゃ", "bya"},
    {"びゅ", "byu"},
    {"びょ", "byo"},
    {"ぴゃ", "pya"},
    {"ぴゅ", "pyu"},
    {"ぴょ", "pyo"},
    {"みゃ", "mya"},
    {"みゅ", "myu"},
    {"みょ", "myo"},
    {"りゃ", "rya"},
    {"りゅ", "ryu"},
    {"りょ", "ryo"},
    {"ふぁ", "fa"},
    {"ふぃ", "fi"},
    {"ふぇ", "fe"},
    {"ふぉ", "fo"},
    {"ふゅ", "fyu"},
    {"てぃ", "ti"},
    {"てゅ", "tyu"},
    {"でぃ", "di"},
    {"でゅ", "dyu"},
    {"とぅ", "tu"},
    {"どぅ", "du"},
    {"うぃ", "wi"},
    {"うぇ", "we"},
    {"うぉ", "wo"},
    {"ゔぁ", "va"},
    {"ゔぃ", "vi"},
    {"ゔぇ", "ve"},
    {"ゔぉ", "vo"},
    {"ゔゅ", "vyu"},
    {"つぁ", "tsa"},
    {"つぃ", "tsi"},
    {"つぇ", "tse"},
    {"つぉ", "tso"},
    {"くぁ", "kwa"},
    {"くぃ", "kwi"},
    {"くぇ", "kwe"},
    {"くぉ", "kwo"},
    {"ぐぁ", "gwa"},
    {"すぃ", "si"},
    {"ずぃ", "zi"},
    {"いぇ", "ye"},
    // one kana
    {"あ", "a"},
    {"い", "i"},
    {"う", "u"},
    {"え", "e"},
    {"お", "o"},
    {"か", "ka"},
    {"き", "ki"},
    {"く", "ku"},
    {"け", "ke"},
    {"こ", "ko"},
    {"が", "ga"},
    {"ぎ", "gi"},
    {"ぐ", "gu"},
    {"げ", "ge"},
    {"ご", "go"},
    {"さ", "sa"},
    {"し", "shi"},
    {"す", "su"},
    {"せ", "se"},
    {"そ", "so"},
    {"ざ", "za"},
    {"じ", "ji"},
    {"ず", "zu"},
    {"ぜ", "ze"},
    {"ぞ", "zo"},
    {"た", "ta"},
    {"ち", "chi"},
    {"つ", "tsu"},
    {"て", "te"},
    {"と", "to"},
    {"だ", "da"},
    {"ぢ", "ji"},
    {"づ", "dzu"},
    {"で", "de"},
    {"ど", "do"},
    {"な", "na"},
    {"に", "ni"},
    {"ぬ", "nu"},
    {"ね", "ne"},
    {"の", "no"},
    {"は", "ha"},
    {"ひ", "hi"},
    {"ふ", "fu"},
    {"へ", "he"},
    {"ほ", "ho"},
    {"ば", "ba"},
    {"び", "bi"},
    {"ぶ", "bu"},
    {"べ", "be"},
    {"ぼ", "bo"},
    {"ぱ", "pa"},
    {"ぴ", "pi"},
    {"ぷ", "pu"},
    {"ぺ", "pe"},
    {"ぽ", "po"},
    {"ま", "ma"},
    {"み", "mi"},
    {"む", "mu"},
    {"め", "me"},
    {"も", "mo"},
    {"や", "ya"},
    {"ゆ", "yu"},
    {"よ", "yo"},
    {"ら", "ra"},
    {"り", "ri"},
    {"る", "ru"},
    {"れ", "re"},
    {"ろ", "ro"},
    {"わ", "wa"},
    {"ゐ", "wi"},
    {"ゑ", "we"},
    {"を", "wo"},
    {"ゔ", "vu"},
};

using chars::kKanaBytes;

// The syllable at `at` in `text` (hiragana), two kana before one; nullptr when none starts there.
const char* kanaSyllableAt(const std::string_view text, const size_t at, size_t& length) {
  for (const size_t kana : {size_t{2}, size_t{1}}) {
    if (at + kana * kKanaBytes > text.size()) continue;
    const std::string_view here = text.substr(at, kana * kKanaBytes);
    const auto* syllable = std::find_if(std::begin(kKanaSyllables), std::end(kKanaSyllables),
                                        [here](const auto& candidate) { return here == candidate.kana; });
    if (syllable != std::end(kKanaSyllables)) {
      length = kana * kKanaBytes;
      return syllable->romaji;
    }
  }
  return nullptr;
}

// Kana letters, iteration marks and ー.
bool isKanaCodepoint(const uint32_t cp) { return chars::isKana(cp) || cp == chars::kProlongedSoundMark; }

// All kana (hiragana, katakana, ー): a transliteration given in kana rather than romaji.
bool isAllKana(const std::string_view text) {
  if (text.empty()) return false;
  const std::string s(text);
  const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    if (!isKanaCodepoint(cp)) return false;
  }
  return true;
}

// The voiced and unvoiced kana of a pair (か が … と ど, は ば … ほ ぼ); 0 when `cp` has none.
uint32_t voicedOf(const uint32_t cp) {
  const bool kaToChi = cp >= U'か' && cp <= U'ち' && (cp - U'か') % 2 == 0;
  const bool tsuToTo = cp == U'つ' || cp == U'て' || cp == U'と';
  const bool haRow = cp >= U'は' && cp <= U'ほ' && (cp - U'は') % 3 == 0;
  return kaToChi || tsuToTo || haRow ? cp + 1 : 0;
}
uint32_t unvoicedOf(const uint32_t cp) { return cp > 0 && voicedOf(cp - 1) == cp ? cp - 1 : cp; }

// ゝ (the kana before, unvoiced) or ゞ (voiced) → that kana; nullopt when there's none to repeat.
std::optional<std::string> repeated(const std::string_view last, const bool voiced) {
  if (last.size() != kKanaBytes) return std::nullopt;  // one kana only (not きゃ)
  const std::string one(last);
  const auto* p = reinterpret_cast<const unsigned char*>(one.c_str());
  const uint32_t base = unvoicedOf(utf8NextCodepoint(&p));
  const uint32_t cp = voiced ? voicedOf(base) : base;
  if (cp == 0) return std::nullopt;
  std::string out;
  utf8AppendCodepoint(cp, out);
  return out;
}

}  // namespace

namespace detail {
std::span<const KanaSyllable> kanaSyllables() { return kKanaSyllables; }
}  // namespace detail

std::optional<std::string> kanaToRomaji(const std::string_view kana) {
  if (!isAllKana(kana)) return std::nullopt;
  const std::string text = katakanaToHiragana(kana);
  std::string out;
  std::string_view last;  // the syllable before, for ゝ ゞ
  size_t i = 0;
  while (i < text.size()) {
    const std::string_view here = std::string_view(text).substr(i, kKanaBytes);
    size_t length = 0;
    if (here == "っ") {  // the next syllable's consonant, doubled (tch before ch); at the end, not written
      if (out.empty()) return std::nullopt;  // nothing before it: a fragment
      const char* next = kanaSyllableAt(text, i + kKanaBytes, length);
      if (!next && i + kKanaBytes == text.size()) break;
      if (!next || isVowel(next[0])) return std::nullopt;
      out += next[0] == 'c' ? 't' : next[0];
      i += kKanaBytes;
      continue;
    }
    if (here == "ん") {  // n' before a vowel or y, so it reads back as ん
      out += 'n';
      const char* next = kanaSyllableAt(text, i + kKanaBytes, length);
      if (next && (isVowel(next[0]) || next[0] == 'y')) out += '\'';
      last = {};
      i += kKanaBytes;
      continue;
    }
    if (here == "ー") {  // the vowel before it, with a macron
      const auto m = out.empty()
                         ? std::end(kMacrons)
                         : std::find_if(std::begin(kMacrons), std::end(kMacrons),
                                        [&out](const auto& candidate) { return candidate.vowel == out.back(); });
      if (m == std::end(kMacrons)) return std::nullopt;
      out.pop_back();
      utf8AppendCodepoint(m->macron, out);
      last = {};
      i += kKanaBytes;
      continue;
    }
    if (here == "ゝ" || here == "ゞ") {  // the kana before, again (unvoiced / voiced): こゝろ, いすゞ
      const std::optional<std::string> again = repeated(last, here == "ゞ");
      if (!again) return std::nullopt;
      out += kanaSyllableAt(*again, 0, length);
      i += kKanaBytes;
      continue;
    }
    static constexpr struct {
      const char* kana;
      char vowel;
    } kSmallVowels[] = {{"ぁ", 'a'}, {"ぃ", 'i'}, {"ぅ", 'u'}, {"ぇ", 'e'}, {"ぉ", 'o'}};
    const auto small = std::find_if(std::begin(kSmallVowels), std::end(kSmallVowels),
                                    [here](const auto& candidate) { return here == candidate.kana; });
    if (small != std::end(kSmallVowels)) {  // a small vowel not part of a syllable: drawn out (ねぇ → nee)
      if (out.empty() || text.substr(i - kKanaBytes, kKanaBytes) == "ん") return std::nullopt;
      out += small->vowel;
      last = {};
      i += kKanaBytes;
      continue;
    }
    const char* romaji = kanaSyllableAt(text, i, length);
    if (!romaji) return std::nullopt;
    out += romaji;
    last = std::string_view(text).substr(i, length);
    i += length;
  }
  if (out.empty()) return std::nullopt;
  return out;
}

JapaneseReading japaneseReading(const std::string_view surface, const std::string_view transliteration) {
  if (transliteration.empty() && isAllKatakana(surface)) {  // no reading given: a katakana word is its own
    return {std::string(surface), kanaToRomaji(surface).value_or(std::string(surface))};
  }
  if (isAllKana(transliteration)) {
    const std::string kana(isAllKatakana(surface) ? surface : transliteration);
    return {kana, kanaToRomaji(transliteration).value_or(std::string(transliteration))};
  }
  const std::string romaji(transliteration);
  return {kanaReading(surface, transliteration).value_or(romaji), romaji};
}

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

std::string katakanaToHiragana(const std::string_view text) {
  constexpr uint32_t kFirst = 0x30A1;  // ァ
  constexpr uint32_t kLast = chars::kSmallKatakanaKe;
  constexpr uint32_t kIterationMark = 0x30FD;     // ヽ (and ヾ after it)
  constexpr uint32_t kKatakanaToHiragana = 0x60;  // ァ U+30A1 → ぁ U+3041
  const std::string s(text);
  std::string out;
  out.reserve(s.size());
  const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
  while (uint32_t cp = utf8NextCodepoint(&p)) {
    if ((cp >= kFirst && cp <= kLast) || cp == kIterationMark || cp == kIterationMark + 1) cp -= kKatakanaToHiragana;
    utf8AppendCodepoint(cp, out);
  }
  return out;
}

std::optional<std::string> kanaReading(const std::string_view surface, const std::string_view romaji) {
  if (isAllKatakana(surface)) return std::string(surface);
  return romajiToHiragana(romaji);
}

}  // namespace lexipoint::text
