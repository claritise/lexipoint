#pragma once

// Everything a lookup needs to know about a tap: the sentence around it (with the tap's offset) and the
// language to send. Pure; the provider (P3) and the P2 debug log use it. Tests: test/lexirise_sentence.

#include <optional>

#include "BookLanguage.h"
#include "SentenceBuilder.h"

namespace lexipoint::text {

struct TapContext {
  std::optional<BuiltSentence> sentence;  // nullopt: nothing to look up at the tap
  LanguageDecision language;              // .language nullopt: Lexirise isn't used for it
  Script script = Script::Latin;          // the punctuation the sentence was cut with (from .detected)
};

// The punctuation follows the language, and without metadata the language follows the sentence: a
// first cut with Japanese rules (the CJK superset) decides the language, then the sentence is cut
// again with the right rules if that changed anything.
TapContext describeTap(const PageModel& page, TokenRef tap, const BookLanguage& book, const Settings& settings);

}  // namespace lexipoint::text
