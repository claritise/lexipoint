// Round-trips the dev harness's logical→panel conversion (src/lexirise/dev/DevCoords.h) against
// a reference copy of GfxRenderer::tapToLogical(), for every pixel in every orientation.
// The device-side LX:SELFTEST repeats this against the real renderer.

#include <gtest/gtest.h>

#include "src/lexirise/dev/DevCoords.h"

using lexipoint::dev::logicalToNormalised;
using lexipoint::dev::logicalToPanel;
using lexipoint::dev::Orientation;

namespace {

constexpr int kPanelWidth = 800;   // X4 Pro panel-native width
constexpr int kPanelHeight = 480;  // X4 Pro panel-native height

// Reference copy of GfxRenderer::tapToLogical() as of CrossPoint 1.6.5rc
// (lib/GfxRenderer/GfxRenderer.cpp). If that mapping changes, this copy and DevCoords.h
// must change together; LX:SELFTEST on the device catches a drift this test can't see.
void referenceTapToLogical(const Orientation orientation, const float nx, const float ny, int& outX, int& outY) {
  int phyX = static_cast<int>(nx * kPanelWidth);
  int phyY = static_cast<int>(ny * kPanelHeight);
  if (phyX < 0) phyX = 0;
  if (phyX > kPanelWidth - 1) phyX = kPanelWidth - 1;
  if (phyY < 0) phyY = 0;
  if (phyY > kPanelHeight - 1) phyY = kPanelHeight - 1;
  switch (orientation) {
    case Orientation::Portrait:
      outX = kPanelHeight - 1 - phyY;
      outY = phyX;
      break;
    case Orientation::PortraitInverted:
      outX = phyY;
      outY = kPanelWidth - 1 - phyX;
      break;
    case Orientation::LandscapeClockwise:
      outX = kPanelWidth - 1 - phyX;
      outY = kPanelHeight - 1 - phyY;
      break;
    case Orientation::LandscapeCounterClockwise:
    default:
      outX = phyX;
      outY = phyY;
      break;
  }
}

struct Case {
  Orientation orientation;
  int logicalWidth;
  int logicalHeight;
};

class DevCoordsRoundTrip : public ::testing::TestWithParam<Case> {};

TEST_P(DevCoordsRoundTrip, EveryPixelLandsWhereAimed) {
  const Case c = GetParam();
  int misses = 0;
  for (int y = 0; y < c.logicalHeight; y++) {
    for (int x = 0; x < c.logicalWidth; x++) {
      float nx = 0, ny = 0;
      logicalToNormalised(c.orientation, x, y, kPanelWidth, kPanelHeight, nx, ny);
      ASSERT_GT(nx, 0.0f);
      ASSERT_LT(nx, 1.0f);
      ASSERT_GT(ny, 0.0f);
      ASSERT_LT(ny, 1.0f);
      int rx = -1, ry = -1;
      referenceTapToLogical(c.orientation, nx, ny, rx, ry);
      if (rx != x || ry != y) misses++;
    }
  }
  EXPECT_EQ(misses, 0);
}

INSTANTIATE_TEST_SUITE_P(AllOrientations, DevCoordsRoundTrip,
                         ::testing::Values(Case{Orientation::Portrait, kPanelHeight, kPanelWidth},
                                           Case{Orientation::PortraitInverted, kPanelHeight, kPanelWidth},
                                           Case{Orientation::LandscapeClockwise, kPanelWidth, kPanelHeight},
                                           Case{Orientation::LandscapeCounterClockwise, kPanelWidth, kPanelHeight}));

TEST(DevCoords, PortraitCornersMapToExpectedPanelPixels) {
  int px = 0, py = 0;
  logicalToPanel(Orientation::Portrait, 0, 0, kPanelWidth, kPanelHeight, px, py);
  EXPECT_EQ(px, 0);
  EXPECT_EQ(py, kPanelHeight - 1);  // logical top-left is the panel's bottom-left
  logicalToPanel(Orientation::Portrait, kPanelHeight - 1, kPanelWidth - 1, kPanelWidth, kPanelHeight, px, py);
  EXPECT_EQ(px, kPanelWidth - 1);
  EXPECT_EQ(py, 0);
}

}  // namespace
