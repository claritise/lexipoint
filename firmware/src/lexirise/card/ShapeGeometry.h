#pragma once

// The ink of the glyphs no device font has (✕ ▼ ▲ ⋯ ›), inside the layout's glyph box. The proportions
// follow the reference's glyphs (a font's ▼ is ~0.65 em wide, ✕ ~0.6 em); the box is §1.1's glyph size.
// Shared by the device painter and the host preview, so both draw the same pixels. Pure; tests:
// test/lexirise_card.

#include <algorithm>
#include <vector>

#include "CardMetrics.h"
#include "DisplayList.h"

namespace lexipoint::card {

struct Point {
  int x = 0;
  int y = 0;
};

struct ShapeInk {
  std::vector<std::vector<Point>> polygons;  // filled
  struct Stroke {
    Point from;
    Point to;
  };
  std::vector<Stroke> strokes;  // lines of strokeWidth
  int strokeWidth = metrics::shape::kStrokeWidth;
  std::vector<Rect> dots;  // filled squares (⋯)
};

inline ShapeInk shapeInk(const Shape kind, const Rect& box) {
  const auto pct = [](const int v, const int p) { return metrics::percentOf(v, p); };
  const int cx = box.x + box.w / 2;
  const int cy = box.y + box.h / 2;
  ShapeInk ink;
  ink.strokeWidth = metrics::shape::kStrokeWidth;
  switch (kind) {
    case Shape::TriangleDown:
    case Shape::TriangleUp: {
      const int w = pct(box.w, metrics::shape::kTriangleWidthPct);
      const int h = pct(box.w, metrics::shape::kTriangleHeightPct);
      const int left = cx - w / 2;
      const int top = cy - h / 2;
      if (kind == Shape::TriangleDown) {
        ink.polygons.push_back({{left, top}, {left + w - 1, top}, {left + (w - 1) / 2, top + h - 1}});
      } else {
        ink.polygons.push_back({{left, top + h - 1}, {left + w - 1, top + h - 1}, {left + (w - 1) / 2, top}});
      }
      break;
    }
    case Shape::Cross: {
      const int s = pct(box.w, metrics::shape::kCrossPct);
      const int l = cx - s / 2;
      const int t = cy - s / 2;
      ink.strokes.push_back({{l, t}, {l + s - 1, t + s - 1}});
      ink.strokes.push_back({{l + s - 1, t}, {l, t + s - 1}});
      break;
    }
    case Shape::Ellipsis: {
      const int d = std::max(metrics::shape::kDotMin, pct(box.w, metrics::shape::kDotPct));
      const int span = pct(box.w, metrics::shape::kDotSpanPct);
      const int left = cx - span / 2;
      for (int i = 0; i < 3; i++) {
        const int x = left + (span - d) * i / 2;
        ink.dots.push_back({x, cy - d / 2, d, d});
      }
      break;
    }
    case Shape::Chevron: {
      const int w = pct(box.w, metrics::shape::kChevronWidthPct);
      const int h = pct(box.h, metrics::shape::kChevronHeightPct);
      const int l = cx - w / 2;
      const int t = cy - h / 2;
      ink.strokes.push_back({{l, t}, {l + w - 1, cy}});
      ink.strokes.push_back({{l + w - 1, cy}, {l, t + h - 1}});
      break;
    }
  }
  return ink;
}

}  // namespace lexipoint::card
