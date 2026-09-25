#pragma once

// Prefixes of a UTF-8 string that never split a character. Pure.

#include <cstddef>
#include <string_view>

namespace lexipoint::text {

// The longest prefix within a byte budget.
inline std::string_view utf8Prefix(const std::string_view text, const size_t maxBytes) {
  if (text.size() <= maxBytes) return text;
  size_t end = maxBytes;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end--;
  return text.substr(0, end);
}

// The first `count` characters (all of them when there are fewer).
inline std::string_view utf8FirstChars(const std::string_view text, size_t count) {
  size_t end = 0;
  while (end < text.size() && count > 0) {
    end++;
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end++;
    count--;
  }
  return text.substr(0, end);
}

}  // namespace lexipoint::text
