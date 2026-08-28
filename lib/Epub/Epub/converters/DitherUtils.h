#pragma once

#include <stdint.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Quantize to the panel's 4 native levels while using Bayer coverage only for
// the remainder between adjacent levels. This anchors exact 0/85/170/255 input
// to one stable output level instead of dithering native gray into neighboring
// levels and shifting its average luminance.
//
// Stateless: callers may decode in MCU blocks or image strips. Coordinates are
// destination coordinates, so the matrix is applied after scaling.
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  const uint8_t lower = gray / 85;
  if (lower >= 3) return 3;

  const uint8_t remainder = gray - lower * 85;
  const uint8_t threshold = bayer4x4[y & 3][x & 3];
  return static_cast<uint16_t>(remainder) * 16 > static_cast<uint16_t>(threshold) * 85 + 42 ? lower + 1 : lower;
}
