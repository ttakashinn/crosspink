#pragma once

#include <cstddef>
#include <cstdint>

namespace xtc_pixels {

// XTH stores vertical bytes by column, with logical columns ordered from right
// to left. Calculate the row mask and first column offset once per scanline;
// the render passes then need no per-pixel division, modulo, or multiplication.
template <typename Visit>
void forEach2BitPixel(const uint8_t* plane1, const uint8_t* plane2, const uint16_t width, const uint16_t height,
                      Visit&& visit) {
  if (!plane1 || !plane2 || width == 0 || height == 0) return;
  const size_t columnBytes = (static_cast<size_t>(height) + 7U) / 8U;
  for (uint16_t y = 0; y < height; ++y) {
    size_t offset = (static_cast<size_t>(width) - 1U) * columnBytes + (y >> 3U);
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (y & 7U));
    for (uint16_t x = 0; x < width; ++x) {
      const uint8_t value =
          static_cast<uint8_t>(((plane1[offset] & mask) ? 2U : 0U) | ((plane2[offset] & mask) ? 1U : 0U));
      visit(x, y, value);
      if (x + 1U < width) offset -= columnBytes;
    }
  }
}

template <typename Visit>
void forEachBlack1BitPixel(const uint8_t* pixels, const uint16_t width, const uint16_t height, Visit&& visit) {
  if (!pixels || width == 0 || height == 0) return;
  const size_t rowBytes = (static_cast<size_t>(width) + 7U) / 8U;
  for (uint16_t y = 0; y < height; ++y) {
    const uint8_t* row = pixels + static_cast<size_t>(y) * rowBytes;
    uint16_t x = 0;
    for (size_t byteIndex = 0; byteIndex < rowBytes; ++byteIndex) {
      const uint8_t packed = row[byteIndex];
      for (uint8_t mask = 0x80U; mask != 0 && x < width; mask >>= 1U, ++x) {
        if ((packed & mask) == 0) visit(x, y);
      }
    }
  }
}

}  // namespace xtc_pixels
