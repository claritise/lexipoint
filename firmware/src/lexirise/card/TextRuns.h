#pragma once

// CrossPoint's UI fonts have no CJK glyphs, and the card's UI text can hold some (a book title in
// "Met before · ノルウェイの森"). A UI-font string is drawn as runs: its CJK characters in the reader
// family at the nearest size, the rest in the UI font. Measuring and drawing both go through this, so
// they agree. Pure; tests: test/lexirise_card.

#include <Utf8.h>

#include <string>
#include <vector>

#include "DisplayList.h"

namespace lexipoint::card {

struct TextRun {
  Font font;
  std::string text;
};

// The reader-family font that stands in for a UI font's missing CJK glyphs (same nominal size).
inline Font cjkFallback(const Font font) {
  switch (font) {
    case Font::UiSmall:
      return Font::ReaderSmall;
    case Font::Ui:
    case Font::UiBold:
      return Font::ReaderMedium;
    default:
      return font;
  }
}

inline bool isUiFont(const Font font) { return font == Font::UiSmall || font == Font::Ui || font == Font::UiBold; }

inline std::vector<TextRun> textRuns(const Font font, const std::string& text) {
  if (!isUiFont(font)) return {{font, text}};
  std::vector<TextRun> runs;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    const Font f = utf8IsCjkCodepoint(cp) ? cjkFallback(font) : font;
    if (runs.empty() || runs.back().font != f) runs.push_back({f, {}});
    utf8AppendCodepoint(cp, runs.back().text);
  }
  return runs;
}

// A text command's runs, placed: each at its x, and its top so all share the command font's baseline.
struct PlacedRun {
  Font font;
  std::string text;
  int x;
  int y;
};
// `measure(font, text)` is the single-font advance width (not TextMetrics::width, which sums runs).
template <typename MeasureFn>
std::vector<PlacedRun> placeRuns(const TextMetrics& metrics, const Font font, const int x, const int y,
                                 const std::string& text, MeasureFn&& measure) {
  std::vector<PlacedRun> out;
  int cx = x;
  const int baseline = y + metrics.ascender(font);
  for (TextRun& run : textRuns(font, text)) {
    const int w = measure(run.font, run.text);
    out.push_back({run.font, std::move(run.text), cx, baseline - metrics.ascender(run.font)});
    cx += w;
  }
  return out;
}

// TextMetrics::width over runs, from a single-font measure.
template <typename MeasureFn>
int runsWidth(const Font font, const std::string& text, MeasureFn&& measure) {
  int w = 0;
  for (const TextRun& run : textRuns(font, text)) w += measure(run.font, run.text);
  return w;
}

}  // namespace lexipoint::card
