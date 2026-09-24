#if LEXIRISE

#include "ReaderPageFor.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>

#include "lexirise/text/PageModelAdapter.h"

namespace lexipoint::card {

ReaderPage readerPageFor(GfxRenderer& renderer, const int fontId, const Page& page, const int marginLeft,
                         const int marginTop) {
  ReaderPage out;
  const int ascender = renderer.getFontAscenderSize(fontId);
  text::forEachTextLine(page, [&](const PageLine& line, const TextBlock& block) {
    ReaderLine row;
    row.y = line.yPos + marginTop + block.getRubyShift(ascender);  // where word select draws the line
    for (uint16_t i = 0; i < block.wordCount(); i++) {
      const char* text = block.wordText(i);
      const EpdFontFamily::Style style = block.wordStyle(i);
      row.tokens.push_back({text, line.xPos + block.wordXpos(i) + marginLeft,
                            renderer.getTextAdvanceX(fontId, text, style), static_cast<uint8_t>(style)});
    }
    out.lines.push_back(std::move(row));
  });
  return out;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
