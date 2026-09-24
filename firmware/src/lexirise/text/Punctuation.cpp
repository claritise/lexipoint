#if LEXIRISE

#include "Punctuation.h"

#include <cstddef>

namespace lexipoint::text {
namespace {

template <size_t N>
bool oneOf(const uint32_t cp, const uint32_t (&set)[N]) {
  for (const uint32_t c : set) {
    if (c == cp) return true;
  }
  return false;
}

// Japanese
constexpr uint32_t kJaTerminators[] = {0x3002, 0xFF01, 0xFF1F, '!', '?', 0xFF0E};  // 。！？!?．
constexpr uint32_t kJaClosers[] = {0x300D, 0x300F, 0xFF09, 0x3011};                // 」』）】
constexpr uint32_t kJaOpeners[] = {0x300C, 0x300E, 0xFF08, 0x3010};                // 「『（【
// Simplified Chinese
constexpr uint32_t kZhTerminators[] = {0x3002, 0xFF01, 0xFF1F, '!', '?'};    // 。！？!?
constexpr uint32_t kZhClosers[] = {0x201D, 0x2019, 0xFF09, 0x3011, 0x300B};  // ”’）】》
constexpr uint32_t kZhOpeners[] = {0x201C, 0x2018, 0xFF08, 0x3010, 0x300A};  // “‘（【《
constexpr uint32_t kZhFallbackCut = 0xFF1B;                                  // ；
// Latin (English and anything else Lexirise doesn't handle)
constexpr uint32_t kLatinTerminators[] = {'.', '!', '?'};
constexpr uint32_t kLatinClosers[] = {'"', '\'', ')', 0x201D, 0x2019};  // " ' ) ” ’

constexpr uint32_t kEllipsis = 0x2026;  // …

}  // namespace

bool Punctuation::isTerminator(const uint32_t cp, const Script script) {
  switch (script) {
    case Script::Japanese:
      return oneOf(cp, kJaTerminators);
    case Script::Chinese:
      return oneOf(cp, kZhTerminators);
    case Script::Latin:
    default:
      return oneOf(cp, kLatinTerminators);
  }
}

bool Punctuation::isEllipsis(const uint32_t cp) { return cp == kEllipsis; }

bool Punctuation::isCloser(const uint32_t cp, const Script script) {
  switch (script) {
    case Script::Japanese:
      return oneOf(cp, kJaClosers);
    case Script::Chinese:
      return oneOf(cp, kZhClosers);
    case Script::Latin:
    default:
      return oneOf(cp, kLatinClosers);
  }
}

bool Punctuation::isOpener(const uint32_t cp, const Script script) {
  switch (script) {
    case Script::Japanese:
      return oneOf(cp, kJaOpeners);
    case Script::Chinese:
      return oneOf(cp, kZhOpeners);
    case Script::Latin:
    default:
      return false;  // Latin quotes are symmetric: no dialogue rule
  }
}

bool Punctuation::continuesQuote(const uint32_t* next, const size_t count, const Script script) {
  constexpr uint32_t kTo = 0x3068;        // と
  constexpr uint32_t kSmallTsu = 0x3063;  // っ
  constexpr uint32_t kTe = 0x3066;        // て
  if (script != Script::Japanese || count == 0) return false;
  return next[0] == kTo || (next[0] == kSmallTsu && (count == 1 || next[1] == kTe));
}

bool Punctuation::isFallbackCut(const uint32_t cp, const Script script) {
  return script == Script::Chinese && cp == kZhFallbackCut;
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
