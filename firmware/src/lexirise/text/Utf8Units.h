#pragma once

// Codepoints and UTF-16 units: Lexirise counts offsets in UTF-16 (lexirise-api-notes.md), the page in
// codepoints. One definition for the sentence builder, the card's highlight and its phase-0 character.

#include <Utf8.h>

#include <cstdint>
#include <string>

namespace lexipoint::text {

// A codepoint's length in UTF-16 units, Lexirise's offsets (a non-BMP character counts 2).
inline uint32_t utf16Units(const uint32_t codepoint) { return codepoint > 0xFFFF ? 2 : 1; }

// Codepoints [from, to) of `text` (as its codepoints are counted: invisible ones too), as UTF-8.
inline std::string utf8Codepoints(const std::string& text, const uint32_t from, const uint32_t to) {
  std::string out;
  uint32_t index = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (index < to) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (index >= from) utf8AppendCodepoint(cp, out);
    index++;
  }
  return out;
}

}  // namespace lexipoint::text
