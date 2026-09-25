#if LEXIRISE

#include "BenchPage.h"

#include "CardMetrics.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::card::bench {

namespace {

std::string firstCodepoints(const std::string& text, const int n) {
  return std::string(text::utf8FirstChars(text, static_cast<size_t>(n)));
}

}  // namespace

Scene layoutPage(const BenchBook& book, const int word, const bool low, const bool highlight,
                 const TextMetrics& metrics, const int highlightCodepoints) {
  Scene scene;
  std::vector<BenchLine> lines = low ? book.filler : std::vector<BenchLine>{};
  lines.insert(lines.end(), book.lines.begin(), book.lines.end());

  const int lh = metrics.lineHeight(Font::Page);
  const int box = card::metrics::kBenchPageLineBox;
  for (int li = 0; li < static_cast<int>(lines.size()); li++) {
    const int top = card::metrics::kBenchPagePadTop + li * box + (box - lh) / 2;
    int x = 0;
    StripLine strip;
    for (const BenchToken& t : lines[li]) {
      const int w = metrics.width(Font::Page, t.text);
      // The reference's lines are set in a narrower font: what runs past the right padding isn't drawn (the
      // panel would clip it with an error per pixel), though the strip still has it.
      const bool drawn =
          card::metrics::kBenchPagePadLeft + x + w <= card::metrics::kScreenWidth - card::metrics::kBenchPagePadRight;
      const bool active = t.word == word;
      if (active) {
        const int i = static_cast<int>(strip.tokens.size());
        if (strip.activeFirst < 0) strip.activeFirst = i;
        strip.activeLast = i;
        scene.wordOnPage = {card::metrics::kBenchPagePadLeft + x - card::metrics::kHighlightPadH, top,
                            w + 2 * card::metrics::kHighlightPadH, lh};
      }
      const bool whole = drawn && active && highlight && highlightCodepoints <= 0;
      if (whole) scene.page.fill(scene.wordOnPage);
      if (drawn) scene.page.text(Font::Page, card::metrics::kBenchPagePadLeft + x, top, t.text, !whole);
      if (drawn && active && highlight && highlightCodepoints > 0) {
        const std::string part = firstCodepoints(t.text, highlightCodepoints);
        scene.page.fill({card::metrics::kBenchPagePadLeft + x - card::metrics::kHighlightPadH, top,
                         metrics.width(Font::Page, part) + 2 * card::metrics::kHighlightPadH, lh});
        scene.page.text(Font::Page, card::metrics::kBenchPagePadLeft + x, top, part, false);
      }
      strip.tokens.push_back({t.text, x, w});
      x += w;
    }
    if (strip.activeFirst >= 0) {
      strip.lineNumber = li + 1;
      strip.lineCount = static_cast<int>(lines.size());
      scene.strip = std::move(strip);
    }
  }

  // The sentence: the book's context lines joined, as they stand (the paragraph's indent too).
  for (int li = book.contextFirst; li < book.contextEnd; li++) {
    for (const BenchToken& t : book.lines[li]) {
      if (t.word == word) {
        scene.sentence.markStart = scene.sentence.text.size();
        scene.sentence.markLength = t.text.size();
      }
      scene.sentence.text += t.text;
    }
  }
  return scene;
}

CardState benchState(const BenchBook& book, const int word, const Scene& scene) {
  CardState s;
  s.level = book.saved[word];
  s.wordOnPage = scene.wordOnPage;
  s.strip = scene.strip;
  s.contextSentence = scene.sentence;
  s.pageNumber = book.pageNumber;
  return s;
}

}  // namespace lexipoint::card::bench

#endif  // LEXIRISE
