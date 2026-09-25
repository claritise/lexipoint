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

TapContext describeNextSentence(const PageModel& page, const TapContext& current, const BookLanguage& book,
                                const Settings& settings) {
  TapContext out;
  if (!current.sentence) return out;
  const auto next = buildSentenceAfter(page, *current.sentence, current.script);
  if (!next) return out;
  out.language = book.decide(book.dependsOnSentence() ? next->text : std::string(), settings);
  out.script = scriptFor(out.language.detected);
  // Cut again with its own rules when its language changed them (as describeTap does after a first cut), from
  // the same first character: the other script's pieces may start elsewhere.
  out.sentence = out.script == current.script ? next : buildSentenceFrom(page, next->chars.front(), out.script);
  return out;
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
