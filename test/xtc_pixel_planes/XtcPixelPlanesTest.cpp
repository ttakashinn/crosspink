#include <gtest/gtest.h>

#include <array>
#include <tuple>
#include <vector>

#include "Xtc/XtcTypes.h"
#include "XtcPixelPlanes.h"
#include "XtcStatusBarOverlayLayout.h"

TEST(XtcPixelPlanes, AccountsForPaddingInsideEveryTwoBitColumn) {
  // 2 columns x 9 rows require 2 bytes per column in each plane. Rounding the
  // total 18 bits would return 3 and make plane 2 overlap plane 1.
  EXPECT_EQ(xtc::xthPlaneSize(2, 9), 4U);
  EXPECT_EQ(xtc::pageBitmapSize(2, 2, 9), 8U);
  EXPECT_EQ(xtc::pageBitmapSize(2, 480, 800), 96000U);
  EXPECT_EQ(xtc::pageBitmapSize(1, 10, 2), 4U);
}

TEST(XtcPixelPlanes, DecodesRightToLeftColumnMajorPlanesInLogicalRowOrder) {
  // Storage offsets 0,1,2 correspond to logical x=2,1,0.
  const std::array<uint8_t, 3> plane1 = {0x40, 0x80, 0x00};
  const std::array<uint8_t, 3> plane2 = {0x00, 0x80, 0x40};
  std::vector<std::tuple<uint16_t, uint16_t, uint8_t>> nonWhite;

  xtc_pixels::forEach2BitPixel(plane1.data(), plane2.data(), 3, 8,
                               [&](const uint16_t x, const uint16_t y, const uint8_t value) {
                                 if (value != 0) nonWhite.emplace_back(x, y, value);
                               });

  const std::vector<std::tuple<uint16_t, uint16_t, uint8_t>> expected = {
      {1, 0, 3},
      {0, 1, 1},
      {2, 1, 2},
  };
  EXPECT_EQ(nonWhite, expected);
}

TEST(XtcPixelPlanes, VisitsOnlyBlackPixelsInAOneBitPartialByteRow) {
  const std::array<uint8_t, 4> pixels = {0b01111111, 0b10111111, 0b11111111, 0b10111111};
  std::vector<std::pair<uint16_t, uint16_t>> black;

  xtc_pixels::forEachBlack1BitPixel(pixels.data(), 10, 2,
                                    [&](const uint16_t x, const uint16_t y) { black.emplace_back(x, y); });

  const std::vector<std::pair<uint16_t, uint16_t>> expected = {{0, 0}, {9, 0}, {9, 1}};
  EXPECT_EQ(black, expected);
}

TEST(XtcStatusBarOverlayLayout, PlacesBottomOverlayInsideTheViewableBottomEdge) {
  const auto layout = xtc_status_bar::calculateLayout(800, 3, 7, 19, false);

  EXPECT_TRUE(layout.visible());
  EXPECT_EQ(770, layout.clearY);
  EXPECT_EQ(23, layout.clearHeight);
  EXPECT_EQ(0, layout.paddingBottom);
}

TEST(XtcStatusBarOverlayLayout, PlacesTopOverlayAndOffsetsStatusBarText) {
  const auto layout = xtc_status_bar::calculateLayout(800, 3, 7, 19, true);

  EXPECT_TRUE(layout.visible());
  EXPECT_EQ(3, layout.clearY);
  EXPECT_EQ(23, layout.clearHeight);
  EXPECT_EQ(767, layout.paddingBottom);
}

TEST(XtcStatusBarOverlayLayout, HidesOverlayWhenTextAndProgressLanesAreDisabled) {
  EXPECT_FALSE(xtc_status_bar::calculateLayout(800, 0, 0, 0, false).visible());
}
