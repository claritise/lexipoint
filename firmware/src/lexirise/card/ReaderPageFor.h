#pragma once

// The device side of ReaderPage: the reader's laid-out page as word select measured it, for the live
// card's highlight and strips (ReaderScene.h). Lines are counted with the page model's filter
// (text::buildPageModel), so a sentence's token references find them. Call after the page's glyphs are
// ready (DictionaryWordSelectActivity::extractWords), never from the render task.

#include "ReaderScene.h"

class GfxRenderer;
class Page;

namespace lexipoint::card {

ReaderPage readerPageFor(GfxRenderer& renderer, int fontId, const Page& page, int marginLeft, int marginTop);

}  // namespace lexipoint::card
