#if LEXIRISE

#include "PageModelAdapter.h"

#include <Epub/Page.h>
#include <Utf8.h>

#include "CharClass.h"
#include "ParagraphBreaks.h"
#include "Punctuation.h"

namespace lexipoint::text {
namespace {

uint32_t firstCodepoint(const char* text) {
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  return utf8NextCodepoint(&p);
}

uint32_t lastCodepoint(const char* text) {
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  uint32_t last = 0;
  while (const uint32_t cp = utf8NextCodepoint(&p)) last = cp;
  return last;
}

}  // namespace

PageModel buildPageModel(const Page& page, const MeasureText& measure, const int em, const int ascender) {
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
    shape.rubyShift = block->getRubyShift(ascender);
    const uint16_t count = block->wordCount();
    out.tokens.reserve(count);
    for (uint16_t i = 0; i < count; i++) out.tokens.emplace_back(block->wordText(i));
    if (count > 0) {
      shape.left = line->xPos + block->wordXpos(0);
      const uint16_t last = count - 1;
      shape.right = line->xPos + block->wordXpos(last) + measure(block->wordText(last), block->wordStyle(last));
      shape.startsWithIdeographicSpace = firstCodepoint(block->wordText(0)) == chars::kIdeographicSpace;
      const uint32_t end = lastCodepoint(block->wordText(last));
      shape.endsWithPause = Punctuation::isQuestionOrExclamation(end) || Punctuation::isEllipsis(end);
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
