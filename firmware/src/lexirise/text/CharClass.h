#pragma once

// Character classes the sentence builder and language detection use, in one place (the per-language
// punctuation sets are in Punctuation). Pure, header-only; tests: test/lexirise_sentence.

#include <cstdint>

namespace lexipoint::text::chars {

// Characters the layout carries that are never part of the text (sentence-extraction.md §2 rule 7):
// soft hyphen, zero-width space/joiners, word joiner, BOM.
inline bool isInvisible(const uint32_t cp) {
  return cp == 0x00AD || cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x2060 || cp == 0xFEFF;
}

// A Latin space the layout kept as a token of its own (&nbsp;, narrow no-break space).
inline bool isLatinSpace(const uint32_t cp) { return cp == ' ' || cp == 0x00A0 || cp == 0x202F; }

// The Japanese/Chinese full-width space (paragraph indent, and after ？！ between sentences).
constexpr uint32_t kIdeographicSpace = 0x3000;

inline bool isLatinHyphen(const uint32_t cp) { return cp == '-' || cp == 0x2010; }

// ASCII or full-width digits and letters: after a bare full stop they mean 3.50 / ３．５ / Ｕ．Ｓ．, not a
// new sentence.
inline bool isAlnum(const uint32_t cp) {
  return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
         (cp >= 0xFF10 && cp <= 0xFF19) || (cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A);
}

// Punctuation, symbols and spaces: a tap resolves to the first piece of its token with a character
// that isn't one of these.
inline bool isPunctuationLike(const uint32_t cp) {
  if (cp < 0x80) return !isAlnum(cp);
  return (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x3004) || (cp >= 0x3008 && cp <= 0x3011) ||
         (cp >= 0x3014 && cp <= 0x301F) || cp == 0x30FB || (cp >= 0xFF01 && cp <= 0xFF0F) ||
         (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65) ||
         cp == 0x00A0 || cp == 0x00AB || cp == 0x00BB;
}

// Latin punctuation that hugs the word before it (no space: "word," "word." "(word)") ...
inline bool attachesLeft(const uint32_t cp) {
  return cp == ',' || cp == '.' || cp == ';' || cp == ':' || cp == '!' || cp == '?' || cp == ')' || cp == ']' ||
         cp == 0x201D || cp == 0x2019;
}
// ... and the word after it ("(word" "“word").
inline bool attachesRight(const uint32_t cp) { return cp == '(' || cp == '[' || cp == 0x201C || cp == 0x2018; }

// Japanese kana letters and iteration marks, not ・ (U+30FB) or ー (U+30FC), which Chinese
// transliterated names use too (哈利・波特).
inline bool isKana(const uint32_t cp) {
  return (cp >= 0x3041 && cp <= 0x3096) || (cp >= 0x309D && cp <= 0x309F) || (cp >= 0x30A1 && cp <= 0x30FA) ||
         (cp >= 0x30FD && cp <= 0x30FF);
}

// Han ideographs (Unified, Extensions A-H, Compatibility).
inline bool isHan(const uint32_t cp) {
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x20000 && cp <= 0x323AF);
}

// The Japanese quotative particle と and the start of って.
constexpr uint32_t kQuotativeTo = 0x3068;  // と
constexpr uint32_t kSmallTsu = 0x3063;     // っ
constexpr uint32_t kQuotativeTe = 0x3066;  // て

}  // namespace lexipoint::text::chars
