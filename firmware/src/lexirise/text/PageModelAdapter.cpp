#if LEXIRISE

#include "PageModelAdapter.h"

#include <Epub/Page.h>

#include <cstring>

#include "ParagraphBreaks.h"

namespace lexipoint::text {
namespace {

constexpr const char* kIdeographicSpace = "\xE3\x80\x80";  // U+3000

}  // namespace

PageModel buildPageModel(const Page& page, const MeasureText& measure, const int em) {
  PageModel model;
  std::vector<LineShape> shapes;
  for (const auto& element : page.elements) {
    // The same filter as DictionaryWordSelectActivity::extractWords(): WordBox line indexes rely on it.
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const ::PageLine*>(element.get());
    const TextBlock* block = line->getBlock();
    if (!block || !block->valid()) continue;

    TextLine out;
    LineShape shape;
    shape.top = line->yPos;
    shape.blockInset = block->getBlockStyle().leftInset();
    shape.alignment = static_cast<int>(block->getBlockStyle().alignment);
    const uint16_t count = block->wordCount();
    out.tokens.reserve(count);
    for (uint16_t i = 0; i < count; i++) out.tokens.emplace_back(block->wordText(i));
    if (count > 0) {
      shape.left = line->xPos + block->wordXpos(0);
      const uint16_t last = count - 1;
      shape.right = line->xPos + block->wordXpos(last) + measure(block->wordText(last), block->wordStyle(last));
      shape.startsWithIdeographicSpace = std::strncmp(block->wordText(0), kIdeographicSpace, 3) == 0;
    } else {
      shape.left = shape.right = line->xPos;
    }
    model.lines.push_back(std::move(out));
    shapes.push_back(shape);
  }
  const std::vector<bool> starts = paragraphStarts(shapes, em);
  for (size_t i = 0; i < model.lines.size(); i++) model.lines[i].startsParagraph = starts[i];
  return model;
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
