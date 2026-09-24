#pragma once

// The device side of a tap: CrossPoint's Page, measured with the renderer, turned into a TapContext
// (the sentence and the language, text/TapContext.h). Used by the word-select hook (P2's debug log)
// and the Lexirise provider (P3).

#include "lexirise/text/TapContext.h"

class GfxRenderer;
class Page;

namespace lexipoint::lookup {

// `fontId`: the font the page was laid out with (DictionaryWordSelectActivity::fontId).
text::TapContext describePageTap(GfxRenderer& renderer, int fontId, const Page& page, text::TokenRef tap,
                                 const text::BookLanguage& book);

// One debug line for the P2 gate: language, source, offsets, truncation, sentence.
void logTapContext(const text::TapContext& context);

}  // namespace lexipoint::lookup
