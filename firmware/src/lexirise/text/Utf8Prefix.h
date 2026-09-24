#pragma once

// The longest prefix of a UTF-8 string within a byte budget that doesn't split a character. Pure.

#include <cstddef>
#include <string_view>

namespace lexipoint::text {

inline std::string_view utf8Prefix(const std::string_view text, const size_t maxBytes) {
  if (text.size() <= maxBytes) return text;
  size_t end = maxBytes;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end--;
  return text.substr(0, end);
}

}  // namespace lexipoint::text
