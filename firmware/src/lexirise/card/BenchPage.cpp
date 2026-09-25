#if LEXIRISE

#include "BenchPage.h"

#include <Utf8.h>

#include <algorithm>

#include "CardMetrics.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::card::bench {

namespace {

std::string firstCodepoints(const std::string& text, const int n) {
  return std::string(text::utf8FirstChars(text, static_cast<size_t>(n)));
}

}  // namespace

std::vector<PlacedRow> wrapLines(const std::vector<BenchLine>& lines, const int width, const TextMetrics& metrics) {
  std::vector<PlacedRow> rows;
  for (const BenchLine& line : lines) {
    rows.emplace_back();
    int x = 0;
    const auto place = [&](std::string text, const int word, const uint32_t firstCp, const int w) {
      if (x > 0 && x + w > width) {
        rows.emplace_back();
        x = 0;
      }
      rows.back().push_back({std::move(text), word, firstCp, x, w});
      x += w;
    };
    for (const BenchToken& t : line) {
      const int w = metrics.width(Font::Page, t.text);
      if (w <= width) {
        place(t.text, t.word, 0, w);
        continue;
      }
      // Wider than a row: codepoint by codepoint, as much as fits on each.
      std::string part;
      uint32_t partFirst = 0;
      uint32_t cp = 0;
      std::string first;
      const auto* q = reinterpret_cast<const unsigned char*>(t.text.c_str());
      utf8AppendCodepoint(utf8NextCodepoint(&q), first);
      if (x > 0 && x + metrics.width(Font::Page, first) > width) {  // not even its first character fits here
        rows.emplace_back();
        x = 0;
      }
      int room = width - x;  // what the part being built has: the rest of this row, then whole rows
      const auto* p = reinterpret_cast<const unsigned char*>(t.text.c_str());
      while (const uint32_t c = utf8NextCodepoint(&p)) {
        std::string next = part;
        utf8AppendCodepoint(c, next);
        if (!part.empty() && metrics.width(Font::Page, next) > room) {
          place(part, t.word, partFirst, metrics.width(Font::Page, part));
          part.clear();
          partFirst = cp;
          utf8AppendCodepoint(c, part);
          room = width;
        } else {
          part = std::move(next);
        }
        cp++;
      }
      if (!part.empty()) place(part, t.word, partFirst, metrics.width(Font::Page, part));
    }
  }
  return rows;
}

Scene layoutPage(const BenchBook& book, const int word, const bool low, const bool highlight,
                 const TextMetrics& metrics, const int highlightCodepoints) {
  Scene scene;
  std::vector<BenchLine> lines = low ? book.filler : std::vector<BenchLine>{};
  lines.insert(lines.end(), book.lines.begin(), book.lines.end());
  const std::vector<PlacedRow> rows = wrapLines(
      lines, card::metrics::kScreenWidth - card::metrics::kBenchPagePadLeft - card::metrics::kBenchPagePadRight,
      metrics);

  const int lh = metrics.lineHeight(Font::Page);
  const int box = card::metrics::kBenchPageLineBox;
  bool firstPiece = true;
  for (int li = 0; li < static_cast<int>(rows.size()); li++) {
    const int top = card::metrics::kBenchPagePadTop + li * box + (box - lh) / 2;
    // A row wrapping pushed below the panel isn't drawn (it would be clipped pixel by pixel); it still counts
    // as a line of the page.
    const bool onPanel = top + lh <= card::metrics::kScreenHeight;
    StripLine strip;
    for (const PlacedToken& t : rows[li]) {
      const int x = card::metrics::kBenchPagePadLeft + t.x;
      const bool active = t.word == word;
      if (active) {
        const int i = static_cast<int>(strip.tokens.size());
        if (strip.activeFirst < 0) strip.activeFirst = i;
        strip.activeLast = i;
        const Rect piece{x - card::metrics::kHighlightPadH, top, t.width + 2 * card::metrics::kHighlightPadH, lh};
        scene.wordPieces.push_back(piece);
        if (firstPiece) {
          scene.wordOnPage = piece;
          firstPiece = false;
        } else {  // a word over more than one row: the box covers every piece (as ReaderScene)
          const int right = std::max(scene.wordOnPage.right(), piece.right());
          const int bottom = std::max(scene.wordOnPage.bottom(), piece.bottom());
          scene.wordOnPage = {std::min(scene.wordOnPage.x, piece.x), std::min(scene.wordOnPage.y, piece.y),
                              right - std::min(scene.wordOnPage.x, piece.x),
                              bottom - std::min(scene.wordOnPage.y, piece.y)};
        }
      }
      // Phase 0 inverts only the word's first `highlightCodepoints`: this part's share of them.
      const int partHighlight =
          highlightCodepoints > 0 ? std::max(0, highlightCodepoints - static_cast<int>(t.firstCp)) : 0;
      const bool whole = active && highlight && highlightCodepoints <= 0;
      if (onPanel && whole) scene.page.fill(scene.wordPieces.back());
      if (onPanel) scene.page.text(Font::Page, x, top, t.text, !whole);
      if (onPanel && active && highlight && partHighlight > 0) {
        const std::string part = firstCodepoints(t.text, partHighlight);
        scene.page.fill({x - card::metrics::kHighlightPadH, top,
                         metrics.width(Font::Page, part) + 2 * card::metrics::kHighlightPadH, lh});
        scene.page.text(Font::Page, x, top, part, false);
      }
      strip.tokens.push_back({t.text, t.x, t.width});
    }
    if (strip.activeFirst >= 0 && scene.strip.activeFirst < 0) {
      strip.lineNumber = li + 1;
      strip.lineCount = static_cast<int>(rows.size());
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
