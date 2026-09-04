#pragma once

#include <algorithm>

namespace xtc_status_bar {

inline constexpr int OVERLAY_PADDING_PX = 4;

struct Layout {
  int clearY = 0;
  int clearHeight = 0;
  int paddingBottom = 0;

  constexpr bool visible() const { return clearHeight > 0; }
};

inline Layout calculateLayout(const int screenHeight, const int orientedMarginTop, const int orientedMarginBottom,
                              const int statusBarHeight, const bool top) {
  if (screenHeight <= 0 || statusBarHeight <= 0) return {};

  Layout layout;
  if (top) {
    layout.clearY = std::max(0, orientedMarginTop);
    layout.clearHeight = std::min(statusBarHeight + OVERLAY_PADDING_PX, screenHeight - layout.clearY);
    layout.paddingBottom =
        std::max(0, screenHeight - statusBarHeight - orientedMarginBottom - orientedMarginTop - OVERLAY_PADDING_PX);
  } else {
    layout.clearY = std::max(0, screenHeight - orientedMarginBottom - statusBarHeight - OVERLAY_PADDING_PX);
    layout.clearHeight = std::max(0, screenHeight - orientedMarginBottom - layout.clearY);
  }
  return layout;
}

}  // namespace xtc_status_bar
