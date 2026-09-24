#if LEXIRISE

#include "CardPainter.h"

#include <GfxRenderer.h>

#include "CardMetrics.h"
#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "ShapeGeometry.h"
#include "TextRuns.h"
#include "fontIds.h"

namespace lexipoint::card {

int CardFonts::id(const Font font) const {
  switch (font) {
    case Font::UiSmall:
      return uiSmall;
    case Font::Ui:
    case Font::UiBold:
      return ui;
    case Font::ReaderSmall:
      return readerSmall;
    case Font::ReaderMedium:
      return readerMedium;
    case Font::ReaderLarge:
      return readerLarge;
    case Font::Page:
      return page;
  }
  return ui;
}

CardFonts resolveCardFonts(GfxRenderer& renderer) {
  CardFonts f;
  f.uiSmall = SMALL_FONT_ID;
  f.ui = UI_10_FONT_ID;
  f.page = SETTINGS.getReaderFontId();
  const auto size = [&](const int pt) {
    const int id = sdFontSystem.familyFontIdAt(renderer, static_cast<uint8_t>(pt));
    return id != 0 ? id : f.page;  // no SD family (or no file at that size): the reader font
  };
  f.readerSmall = size(metrics::kReaderSmallPt);
  f.readerMedium = size(metrics::kReaderMediumPt);
  f.readerLarge = size(metrics::kReaderLargePt);
  return f;
}

namespace {

EpdFontFamily::Style styleOf(const Font font) {
  return font == Font::UiBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
}

}  // namespace

int RendererMetrics::lineHeight(const Font font) const { return renderer_.getLineHeight(fonts_.id(font)); }

int RendererMetrics::ascender(const Font font) const { return renderer_.getFontAscenderSize(fonts_.id(font)); }

int RendererMetrics::single(const Font font, const std::string& text) const {
  const int id = fonts_.id(font);
  if (renderer_.isSdCardFont(id)) renderer_.ensureSdCardFontReady(id, text.c_str());  // the advance table
  return renderer_.getTextAdvanceX(id, text.c_str(), styleOf(font));
}

int RendererMetrics::width(const Font font, const std::string& text) const {
  return runsWidth(font, text, [this](const Font f, const std::string& t) { return single(f, t); });
}

void paint(GfxRenderer& renderer, const DisplayList& list, const CardFonts& fonts, const RendererMetrics& metrics) {
  for (const Command& c : list.commands) {
    const Rect& r = c.rect;
    switch (c.kind) {
      case Command::Kind::Fill:
        renderer.fillRect(r.x, r.y, r.w, r.h, c.black);
        break;
      case Command::Kind::Frame:
        renderer.drawRect(r.x, r.y, r.w, r.h, c.thickness, c.black);
        break;
      case Command::Kind::RoundedFrame:
        renderer.drawRoundedRect(r.x, r.y, r.w, r.h, c.thickness, c.radius, c.black);
        break;
      case Command::Kind::Line:
        renderer.fillRect(r.x, r.y, r.w, c.thickness, c.black);
        break;
      case Command::Kind::Text:
        for (const PlacedRun& run :
             placeRuns(metrics, c.font, r.x, r.y, c.text,
                       [&](const Font f, const std::string& t) { return metrics.single(f, t); })) {
          renderer.drawText(fonts.id(run.font), run.x, run.y, run.text.c_str(), c.black, styleOf(run.font));
        }
        break;
      case Command::Kind::Shape: {
        const ShapeInk ink = shapeInk(c.shape, r);
        for (const auto& poly : ink.polygons) {
          std::vector<int> xs;
          std::vector<int> ys;
          for (const Point& p : poly) {
            xs.push_back(p.x);
            ys.push_back(p.y);
          }
          renderer.fillPolygon(xs.data(), ys.data(), static_cast<int>(poly.size()), c.black);
        }
        for (const auto& s : ink.strokes) {
          renderer.drawLine(s.from.x, s.from.y, s.to.x, s.to.y, ink.strokeWidth, c.black);
        }
        for (const Rect& d : ink.dots) renderer.fillRect(d.x, d.y, d.w, d.h, c.black);
        break;
      }
    }
  }
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
