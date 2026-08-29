#pragma once

#include <algorithm>

struct HomeMenuViewport {
  int first = 0;
  int count = 0;
  int selected = 0;
};

inline HomeMenuViewport calculateHomeMenuViewport(const int total, const int selected, const int height,
                                                  const int rowHeight, const int rowSpacing, const int topInset) {
  if (total <= 0) return {};
  const int step = std::max(1, rowHeight + rowSpacing);
  const int usableHeight = std::max(rowHeight, height - std::max(0, topInset));
  const int pageItems = std::clamp((usableHeight + rowSpacing) / step, 1, total);
  if (selected < 0) return {0, pageItems, -1};
  const int safeSelected = std::clamp(selected, 0, total - 1);
  // Scroll only as far as needed to keep the selection visible. A fixed page
  // jump can leave the final page with a single orphaned row when one new Home
  // action is added; a sliding window preserves the surrounding context.
  const int first = std::clamp(safeSelected - pageItems + 1, 0, total - pageItems);
  return {first, std::min(pageItems, total - first), safeSelected - first};
}
