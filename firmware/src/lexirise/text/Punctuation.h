#pragma once

// Per-language punctuation for sentence extraction (sentence-extraction.md §2, languages.md §2).
// Pure. Tests: test/lexirise_sentence.

#include <cstddef>
#include <cstdint>

namespace lexipoint::text {

// Which punctuation rules a page follows: the book's Lexirise language, or Latin for anything else.
enum class Script { Japanese, Chinese, Latin };

struct Punctuation {
  // Ends a sentence: 。！？!?． (ja), 。！？!? (zh), .!? (Latin).
  static bool isTerminator(uint32_t cp, Script script);
  // "…": ends a sentence only when a closer follows or the paragraph ends.
  static bool isEllipsis(uint32_t cp);
  // Kept with the sentence right after its terminator: 」』）】 (ja), ”’）】》 (zh), "')”’ (Latin).
  static bool isCloser(uint32_t cp, Script script);
  // Opens a quote or bracket: 「『（【 (ja), “‘（【《 (zh).
  static bool isOpener(uint32_t cp, Script script);
  // Quotation marks only (not brackets or titles): 」』 / 「『 (ja), ”’ / “‘ (zh). A closing quote followed
  // by an opening one (」「, ”“) is a sentence break even without a terminator: consecutive lines of
  // dialogue. 』（ or 》（ or 】【 is not.
  static bool isQuoteCloser(uint32_t cp, Script script);
  static bool isQuoteOpener(uint32_t cp, Script script);
  // "." and "．": full stops that also sit inside numbers and abbreviations (3.50, ３．５, Ｕ．Ｓ．).
  static bool isDot(uint32_t cp);
  // Chinese "；": a cut used only when a sentence would pass the codepoint cap.
  static bool isFallbackCut(uint32_t cp, Script script);
  // Japanese quotative と / って right after a closed quote (「行こう。」と言った) or a question or
  // exclamation mark (本当に!?と思った): the quote is part of the sentence, not a sentence of its own.
  // `next` is what follows. (After 。 a と starts a new sentence: 。とにかく.)
  static bool continuesQuote(const uint32_t* next, size_t count, Script script);
  // ？！?! (not 。): what an unbracketed quote can end with before its quotative.
  static bool isQuestionOrExclamation(uint32_t cp);
};

}  // namespace lexipoint::text
