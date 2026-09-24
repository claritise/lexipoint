#if LEXIRISE

#include "TapContext.h"

namespace lexipoint::text {
namespace {

// The punctuation follows what the text is, whether or not Lexirise will be asked about it.
Script scriptFor(const std::optional<Language>& language) {
  if (!language) return Script::Latin;
  return *language == Language::Chinese ? Script::Chinese : Script::Japanese;
}

}  // namespace

TapContext describeTap(const PageModel& page, const TokenRef tap, const BookLanguage& book, const Settings& settings) {
  TapContext out;
  if (book.dependsOnSentence()) {
    const auto first = buildSentence(page, tap, Script::Japanese);
    if (!first) return out;
    out.language = book.decide(first->text, settings);
    out.script = scriptFor(out.language.detected);
    out.sentence = out.script == Script::Japanese ? first : buildSentence(page, tap, out.script);
  } else {
    out.language = book.decide({}, settings);
    out.script = scriptFor(out.language.detected);
    out.sentence = buildSentence(page, tap, out.script);
  }
  return out;
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
