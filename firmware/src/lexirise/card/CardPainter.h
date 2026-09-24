#pragma once

// The device side of the card: its fonts, measuring with GfxRenderer, and painting a DisplayList. The
// layout (CardLayout) is pure; this is the only card code that touches the renderer. Everything here
// runs under RenderLock (inside render()): loading an SD size and measuring change shared font state.

#include "DisplayList.h"

class GfxRenderer;

namespace lexipoint::card {

// The renderer's font ids for the card's fonts. A missing reader size falls back to the reader font.
struct CardFonts {
  int uiSmall = 0;
  int ui = 0;
  int readerSmall = 0;
  int readerMedium = 0;
  int readerLarge = 0;
  int page = 0;
  int id(Font font) const;
};

// Resolves (and, for the reader family's extra sizes, loads) the card's fonts. The extra sizes stay loaded
// after the card closes (the next card reuses them); they go when the reader changes family.
CardFonts resolveCardFonts(GfxRenderer& renderer);

class RendererMetrics final : public TextMetrics {
 public:
  RendererMetrics(GfxRenderer& renderer, const CardFonts& fonts) : renderer_(renderer), fonts_(fonts) {}
  int lineHeight(Font font) const override;
  int ascender(Font font) const override;
  int width(Font font, const std::string& text) const override;
  int single(Font font, const std::string& text) const;  // one font, no runs

 private:
  GfxRenderer& renderer_;
  const CardFonts& fonts_;
};

// Draws the commands in order (the page first, then the card over it).
void paint(GfxRenderer& renderer, const DisplayList& list, const CardFonts& fonts, const RendererMetrics& metrics);

}  // namespace lexipoint::card
