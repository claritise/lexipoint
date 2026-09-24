#pragma once

// From a laid-out page and a tapped token to the sentence around it, plus the tap's offset in the
// server's unit (sentence-extraction.md). Pure: the page arrives as a PageModel (PageModelAdapter builds
// one from CrossPoint's Page). Tests: test/lexirise_sentence, and end to end through CrossPoint's real
// layout in test/lexirise_pagemodel.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Punctuation.h"

namespace lexipoint::text {

// One laid-out line: its tokens in reading order (base text only: never ruby) and whether it starts
// a paragraph (a paragraph end is a sentence end, which is what separates dialogue lines).
struct TextLine {
  std::vector<std::string> tokens;
  bool startsParagraph = false;
};

// The text lines of one page, in reading order. Images and rules are simply not in it.
struct PageModel {
  std::vector<TextLine> lines;
};

struct TokenRef {
  size_t line = 0;
  size_t token = 0;
};

struct BuiltSentence {
  std::string text;  // UTF-8
  // The tapped text's start in `text`, in UTF-16 code units (Lexirise's charStart), and its length. A
  // laid-out token can hold more than one sentence (Chinese “好。”“走 is one token): the tap is its first
  // piece with a letter in it.
  uint32_t tapOffset = 0;
  uint32_t tapLength = 0;
  bool truncatedLeft = false;   // cut by the page top or the cap, not by a sentence start
  bool truncatedRight = false;  // cut by the page bottom or the cap, not by a sentence end
};

// Builds the sentence around `tap`. nullopt if the reference is out of range or the tapped token has
// no text once zero-width characters are dropped.
std::optional<BuiltSentence> buildSentence(const PageModel& page, TokenRef tap, Script script);

// UTF-16 code units in a UTF-8 string (a non-BMP character counts 2), the unit of Lexirise offsets.
uint32_t utf16Length(const std::string& utf8);

}  // namespace lexipoint::text
