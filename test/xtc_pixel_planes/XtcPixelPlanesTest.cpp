#include <gtest/gtest.h>

#include <array>
#include <tuple>
#include <vector>

#include "XtcPixelPlanes.h"

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
