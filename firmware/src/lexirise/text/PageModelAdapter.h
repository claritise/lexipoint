#pragma once

// CrossPoint's laid-out Page → the PageModel the sentence builder reads. One PageModel line per text
// line that DictionaryWordSelectActivity::extractWords() walks (a ::PageLine with a valid block), with
// every token, in order, so a word's (line, token) indexes it directly. Base text only: ruby lives in
// a separate list and never enters. Host-tested against real Page/TextBlock objects
// (test/lexirise_pagemodel).

#include <EpdFontFamily.h>

#include <functional>

#include "SentenceBuilder.h"

class Page;

namespace lexipoint::text {

// Width of a token in pixels, as the page was laid out (GfxRenderer::getTextAdvanceX on the device).
using MeasureText = std::function<int(const char* text, EpdFontFamily::Style style)>;

// `em`: the width of one full-width character in the page's font (for the paragraph heuristic).
PageModel buildPageModel(const Page& page, const MeasureText& measure, int em);

}  // namespace lexipoint::text
