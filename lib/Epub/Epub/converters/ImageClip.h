#pragma once

#include <algorithm>
#include <cstdint>

struct ImageClipBounds {
  int colStart = 0;
  int colEnd = 0;
  int rowStart = 0;
  int rowEnd = 0;

  [[nodiscard]] bool empty() const { return colStart >= colEnd || rowStart >= rowEnd; }
};

// Return the image-local half-open rectangle that is visible on screen. Use
// 64-bit intermediates so malformed coordinates cannot overflow while clipping.
inline ImageClipBounds calculateImageClip(const int imageX, const int imageY, const int imageWidth,
                                          const int imageHeight, const int screenWidth, const int screenHeight) {
  if (imageWidth <= 0 || imageHeight <= 0 || screenWidth <= 0 || screenHeight <= 0) return {};

  const int64_t x = imageX;
  const int64_t y = imageY;
  const int64_t width = imageWidth;
  const int64_t height = imageHeight;
  const int64_t screenW = screenWidth;
  const int64_t screenH = screenHeight;

  const int64_t visibleLeft = std::max<int64_t>(x, 0);
  const int64_t visibleTop = std::max<int64_t>(y, 0);
  const int64_t visibleRight = std::min<int64_t>(x + width, screenW);
  const int64_t visibleBottom = std::min<int64_t>(y + height, screenH);
  if (visibleLeft >= visibleRight || visibleTop >= visibleBottom) return {};

  ImageClipBounds bounds;
  bounds.colStart = static_cast<int>(visibleLeft - x);
  bounds.colEnd = static_cast<int>(visibleRight - x);
  bounds.rowStart = static_cast<int>(visibleTop - y);
  bounds.rowEnd = static_cast<int>(visibleBottom - y);
  return bounds;
}
