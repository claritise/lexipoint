#pragma once

// The device side of a tap. The page model is built when word select opens (extractWords, after the SD
// font has the page's glyphs), since measuring text on a tap would race the render task over the glyph
// cache; a tap is then pure work (text/TapContext.h). Reused by the Lexirise provider (P3).

#include <string>

#include "Fallback.h"
#include "lexirise/text/TapContext.h"

class GfxRenderer;
class Page;

namespace lexipoint::lookup {

// Text to add to the page's glyph warm-up before pageModelFor() measures: the em probe.
constexpr const char* kEmProbe = "\xE5\x9B\xBD";  // 国: one full-width character

// Measures the page (line ends, the em, furigana heights) with the font it was laid out in. Call where
// DictionaryWordSelectActivity measures its words: after ensureSdCardFontReady() on the page's text.
text::PageModel pageModelFor(GfxRenderer& renderer, int fontId, const Page& page);

// Everything about a tap on the page: the sentence, the tap's offset and the language (pure work).
text::TapContext describeTap(const text::PageModel& page, text::TokenRef tap, const text::BookLanguage& book);

// Whether a lookup in this book asks Lexirise now, or why not (Fallback.h: settings, key, a rejected key,
// a rate limit's back-off).
Gate lexiriseGate(const text::BookLanguage& book);
// Lexirise is set up for lookups in this book (on, a key, the language allowed): whether it may be asked
// *now* is lexiriseGate's (a rejected key, a rate limit). This one decides what doesn't change with
// time: word select opens without a StarDict dictionary, a long-press is a lookup, the page is snapshot.
bool lexiriseConfigured(const text::BookLanguage& book);
// True the first time it's asked each boot: "No Lexirise key" is said once, then StarDict answers quietly.
bool takeNoKeyNotice();
// takeNoKeyNotice() when the gate says there's no key (so another gate doesn't use it up).
inline bool takeNoKeyNoticeIf(const Gate gate) { return gate == Gate::NoKey && takeNoKeyNotice(); }
// The block the reader hasn't been told about, when this gate is that block (a lookup that doesn't ask
// for another reason, Lexirise off for the book, say, mustn't use it up).
api::AccessPolicy::Block takeUnannouncedIf(Gate gate);

// A debug line for a tap: language, source, offsets, truncation, sentence (debug builds only).
void logTap(const text::TapContext& context);

}  // namespace lexipoint::lookup
