#if LEXIRISE

#include "ReaderScene.h"

#include <Utf8.h>

#include <algorithm>

#include "CardMetrics.h"
#include "lexirise/text/Utf8Units.h"

namespace lexipoint::card {

namespace {

// The active range's part of one token: codepoints [first, last] of it.
struct Piece {
  text::TokenRef token;
  uint32_t first = 0;
  uint32_t last = 0;
};

std::vector<Piece> piecesOf(const text::BuiltSentence& sentence, const uint32_t start, const uint32_t end) {
  std::vector<Piece> pieces;
  for (const text::SentenceChar& c : sentence.chars) {
    if (c.start < start || c.start >= end) continue;
    if (!pieces.empty() && pieces.back().token.line == c.token.line && pieces.back().token.token == c.token.token &&
        pieces.back().last + 1 == c.codepoint) {
      pieces.back().last = c.codepoint;
    } else {
      pieces.push_back({c.token, c.codepoint, c.codepoint});
    }
  }
  return pieces;
}

// The UTF-8 byte offset of a UTF-16 offset in `text`.
size_t byteAt(const std::string& text, const uint32_t units) {
  uint32_t at = 0;
  const auto* begin = reinterpret_cast<const unsigned char*>(text.c_str());
  const auto* p = begin;
  while (at < units) {
    const auto* before = p;
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) return static_cast<size_t>(before - begin);
    at += text::utf16Units(cp);
  }
  return static_cast<size_t>(p - begin);
}

}  // namespace

PageScene readerScene(const ReaderPage& page, const text::BuiltSentence& sentence, const uint32_t start,
                      const uint32_t end, const bool highlight, const TextMetrics& metrics) {
  PageScene scene;
  const size_t markStart = byteAt(sentence.text, start);
  scene.sentence = {sentence.text, markStart, byteAt(sentence.text, end) - markStart};

  const int lh = metrics.lineHeight(Font::Page);
  bool first = true;
  for (const Piece& piece : piecesOf(sentence, start, end)) {
    if (piece.token.line >= page.lines.size()) continue;
    const ReaderLine& line = page.lines[piece.token.line];
    if (piece.token.token >= line.tokens.size()) continue;
    const PageToken& token = line.tokens[piece.token.token];
    // Measured and drawn in the word's own style (a bold heading stays bold, and its box fits).
    const std::string part = text::utf8Codepoints(token.text, piece.first, piece.last + 1);
    const int x = token.x + metrics.pageWidth(text::utf8Codepoints(token.text, 0, piece.first), token.style);
    const Rect box{x - metrics::kHighlightPadH, line.y,
                   metrics.pageWidth(part, token.style) + 2 * metrics::kHighlightPadH, lh};
    if (highlight) {
      scene.page.fill(box);
      scene.page.text(Font::Page, x, line.y, part, false, token.style);
    }
    if (first) {
      scene.wordOnPage = box;
      // The strip: the word's (first) line, its tokens from the line's start.
      const int origin = line.tokens.empty() ? 0 : line.tokens.front().x;
      for (const PageToken& t : line.tokens) scene.strip.tokens.push_back({t.text, t.x - origin, t.width});
      scene.strip.activeFirst = scene.strip.activeLast = static_cast<int>(piece.token.token);
      scene.strip.lineNumber = static_cast<int>(piece.token.line) + 1;
      scene.strip.lineCount = static_cast<int>(page.lines.size());
      first = false;
    } else {
      // A word over more than one token or line: the box covers every piece (D17 asks if any is covered).
      const int right = std::max(scene.wordOnPage.right(), box.right());
      const int bottom = std::max(scene.wordOnPage.bottom(), box.bottom());
      scene.wordOnPage.x = std::min(scene.wordOnPage.x, box.x);
      scene.wordOnPage.y = std::min(scene.wordOnPage.y, box.y);
      scene.wordOnPage.w = right - scene.wordOnPage.x;
      scene.wordOnPage.h = bottom - scene.wordOnPage.y;
      if (static_cast<int>(piece.token.line) + 1 == scene.strip.lineNumber) {
        scene.strip.activeLast = static_cast<int>(piece.token.token);
      }
    }
  }
  return scene;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
