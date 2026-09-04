#pragma once

namespace reader_viewport {

// The reading margin belongs between the laid-out page and the status bar.
// Keep it independent from the space occupied by the status bar itself so a
// visible bar does not silently replace the user's bottom reading margin.
constexpr int bottomInset(const int readingMargin, const int statusBarHeight, const int extraStatusHeight = 0) {
  return readingMargin + statusBarHeight + extraStatusHeight;
}

}  // namespace reader_viewport
