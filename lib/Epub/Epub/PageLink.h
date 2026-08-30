#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "FootnoteEntry.h"

// One laid-out internal EPUB link. Coordinates are relative to the page
// origin; the reader adds the oriented content margins when hit-testing.
struct PageLink {
  char href[FOOTNOTE_HREF_LEN]{};
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;

  bool contains(const int pageX, const int pageY, const int slop, const int minWidth) const {
    const int horizontalSlop = std::max(slop, (minWidth - width) / 2);
    return pageX >= x - horizontalSlop && pageX < x + width + horizontalSlop && pageY >= y - slop &&
           pageY < y + height + slop;
  }

  uint64_t touchScore(const int pageX, const int pageY, const int slop, const int minWidth) const {
    if (!contains(pageX, pageY, slop, minWidth)) return std::numeric_limits<uint64_t>::max();
    const bool insideActual = pageX >= x && pageX < x + width && pageY >= y && pageY < y + height;
    const int64_t dx = static_cast<int64_t>(pageX) * 2 - (static_cast<int64_t>(x) * 2 + width);
    const int64_t dy = static_cast<int64_t>(pageY) * 2 - (static_cast<int64_t>(y) * 2 + height);
    const uint64_t distance = static_cast<uint64_t>(dx * dx + dy * dy);
    return distance + (insideActual ? 0U : (uint64_t{1} << 62));
  }
};
