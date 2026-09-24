#pragma once

// The device side of a tap. The page model is built when word select opens (extractWords, after the SD
// font has the page's glyphs), since measuring text on a tap would race the render task over the glyph
// cache; a tap is then pure work (text/TapContext.h). Reused by the Lexirise provider (P3).

#include <string>

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

// Lexirise is on, has a key, and this book's language may go to it: lookups can go to it (word select
// opens without a StarDict dictionary).
bool lexiriseUsable(const text::BookLanguage& book);

// A debug line for a tap: language, source, offsets, truncation, sentence (debug builds only).
void logTap(const text::TapContext& context);

}  // namespace lexipoint::lookup
