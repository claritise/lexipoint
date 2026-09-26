#pragma once

// From a laid-out page and a tapped token to the sentence around it, plus the tap's offset in the
// server's unit (sentence-extraction.md). Pure: the page arrives as a PageModel (PageModelAdapter builds
// one from CrossPoint's Page). Tests: test/lexirise_sentence, and end to end through CrossPoint's real
// layout in test/lexirise_layout.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

// Where one character of a built sentence sits on the page: the laid-out token, and its index among that
// token's codepoints (invisible ones counted, as in the token's text). A space the join inserted has none.
struct SentenceChar {
  uint32_t start = 0;  // UTF-16 units in the sentence
  uint8_t units = 1;   // 2 for a non-BMP character
  TokenRef token;
  uint32_t codepoint = 0;
};

struct BuiltSentence {
  std::string text;  // UTF-8
  // Every character with a place on the page, in order: a Lexirise range [charStart, charEnd) finds its
  // page tokens here (the card's highlight, lookup-flow.md §6).
  std::vector<SentenceChar> chars;
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

// The sentence after `current` on the page (the card's side buttons step on into it, lookup-flow.md §6), built
// as if its first piece were tapped. nullopt at the page end, or when `current` has no place on the page.
std::optional<BuiltSentence> buildSentenceAfter(const PageModel& page, const BuiltSentence& current, Script script);
// The sentence that starts at `first` (a character another script's cut found), cut with `script`: the next
// sentence again once its own language is known, from the same place on the page.
std::optional<BuiltSentence> buildSentenceFrom(const PageModel& page, const SentenceChar& first, Script script);

// UTF-16 code units in a UTF-8 string (a non-BMP character counts 2), the unit of Lexirise offsets.
uint32_t utf16Length(std::string_view utf8);

}  // namespace lexipoint::text
