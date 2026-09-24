#pragma once

// Pure coordinate helpers for the dev harness (no Arduino dependencies, host-testable).
//
// The harness injects touches in *logical* screen coordinates (what the UI draws in), while the
// input layer reports *normalised panel-native* coordinates (0..1 over the 800x480 panel), which
// GfxRenderer::tapToLogical() turns back into logical ones. These helpers are the exact inverse of
// tapToLogical(). test/lexirise_dev_coords round-trips them against a reference copy of it, and the
// device's LX:SELFTEST round-trips them against the real one.

#include <cstdint>

namespace lexipoint::dev {

// Same order and values as GfxRenderer::Orientation (checked by static_assert in DevHarness.cpp).
enum class Orientation : uint8_t {
  Portrait = 0,
  LandscapeClockwise = 1,
  PortraitInverted = 2,
  LandscapeCounterClockwise = 3,
};

// Logical (x, y) → panel-native pixel (phyX, phyY) on a panelWidth x panelHeight panel.
inline void logicalToPanel(const Orientation orientation, const int x, const int y, const int panelWidth,
                           const int panelHeight, int& phyX, int& phyY) {
  switch (orientation) {
    case Orientation::Portrait:
      phyX = y;
      phyY = panelHeight - 1 - x;
      break;
    case Orientation::PortraitInverted:
      phyX = panelWidth - 1 - y;
      phyY = x;
      break;
    case Orientation::LandscapeClockwise:
      phyX = panelWidth - 1 - x;
      phyY = panelHeight - 1 - y;
      break;
    case Orientation::LandscapeCounterClockwise:
    default:
      phyX = x;
      phyY = y;
      break;
  }
}

// Logical (x, y) → normalised panel-native coordinates, sampling the pixel centre so that
// tapToLogical()'s truncation lands back on the same pixel.
inline void logicalToNormalised(const Orientation orientation, const int x, const int y, const int panelWidth,
                                const int panelHeight, float& nx, float& ny) {
  int phyX = 0;
  int phyY = 0;
  logicalToPanel(orientation, x, y, panelWidth, panelHeight, phyX, phyY);
  nx = (static_cast<float>(phyX) + 0.5f) / static_cast<float>(panelWidth);
  ny = (static_cast<float>(phyY) + 0.5f) / static_cast<float>(panelHeight);
}

}  // namespace lexipoint::dev
