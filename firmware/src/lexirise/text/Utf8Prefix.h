#pragma once

// Prefixes and whole characters of a UTF-8 string, never splitting a character. Pure; tests:
// test/lexirise_kana/Utf8PrefixTest.cpp.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "CharClass.h"
#include "Utf8Units.h"

namespace lexipoint::text {

// A byte inside a character (10xxxxxx), not its first.
inline bool isContinuationByte(const char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// The longest prefix within a byte budget.
inline std::string_view utf8Prefix(const std::string_view text, const size_t maxBytes) {
  if (text.size() <= maxBytes) return text;
  size_t end = maxBytes;
  while (end > 0 && isContinuationByte(text[end])) end--;
  return text.substr(0, end);
}

// The first `count` characters (all of them when there are fewer).
inline std::string_view utf8FirstChars(const std::string_view text, size_t count) {
  size_t end = 0;
  while (end < text.size() && count > 0) {
    end++;
    while (end < text.size() && isContinuationByte(text[end])) end++;
    count--;
  }
  return text.substr(0, end);
}

// The last character (empty for an empty string), and the text without it.
inline std::string_view utf8LastChar(const std::string_view text) {
  size_t start = text.size();
  while (start > 0 && isContinuationByte(text[--start])) {
  }
  return text.substr(start);
}
inline std::string_view utf8WithoutLastChar(const std::string_view text) {
  return text.substr(0, text.size() - utf8LastChar(text).size());
}

// The first character's code point, by lib/Utf8's decoder: 0 for an empty string, U+FFFD for a broken start.
inline uint32_t utf8FirstCodepoint(const std::string_view text) {
  constexpr size_t kMaxCharBytes = 4;
  char first[kMaxCharBytes + 1] = {};  // NUL-terminated for the decoder, which reads up to one character
  text.copy(first, kMaxCharBytes);
  const auto* p = reinterpret_cast<const unsigned char*>(first);
  return utf8NextCodepoint(&p);
}

// The character starting `units` UTF-16 units into `text` (Lexirise's offsets); empty past the end or inside a
// character.
inline std::string_view utf8CharAtUtf16(std::string_view text, uint32_t units) {
  while (!text.empty() && units > 0) {
    const std::string_view first = utf8FirstChars(text, 1);
    const uint32_t width = utf16Units(utf8FirstCodepoint(first));
    if (width > units) return {};
    units -= width;
    text.remove_prefix(first.size());
  }
  return utf8FirstChars(text, 1);
}

// Without spaces or line breaks at either end (a paragraph's indent isn't part of its sentence).
inline std::string_view trimmedSpaces(std::string_view s) {
  while (!s.empty() && chars::isSpaceOrBreak(utf8FirstCodepoint(s))) s.remove_prefix(utf8FirstChars(s, 1).size());
  while (!s.empty() && chars::isSpaceOrBreak(utf8FirstCodepoint(utf8LastChar(s)))) {
    s.remove_suffix(utf8LastChar(s).size());
  }
  return s;
}

}  // namespace lexipoint::text
