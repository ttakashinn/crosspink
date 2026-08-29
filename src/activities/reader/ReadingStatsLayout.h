#pragma once

#include <algorithm>

struct ReadingStatsRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  int right() const { return x + width; }
  int bottom() const { return y + height; }
};

struct ReadingStatsScreenLayout {
  ReadingStatsRect currentBook;
  ReadingStatsRect thisDevice;
  int columns = 3;
};

inline ReadingStatsScreenLayout makeReadingStatsScreenLayout(const int screenWidth, const int screenHeight,
                                                             const int contentTop, const int bottomReserve) {
  const int side = screenWidth >= 528 ? 24 : 18;
  const int gap = 16;
  const int bottom = std::max(contentTop, screenHeight - bottomReserve - 12);
  const int available = std::max(0, bottom - contentTop - gap);
  int currentHeight = std::clamp(available * 52 / 100, 300, 350);
  if (currentHeight > available) currentHeight = available;
  const int deviceHeight = std::max(0, available - currentHeight);
  return {{side, contentTop, std::max(0, screenWidth - side * 2), currentHeight},
          {side, contentTop + currentHeight + gap, std::max(0, screenWidth - side * 2), deviceHeight},
          3};
}

struct ReadingDashboardLayout {
  ReadingStatsRect cover;
  ReadingStatsRect summary;
  ReadingStatsRect footer;
};

inline ReadingDashboardLayout makeReadingDashboardLayout(const int x, const int y, const int width, const int height) {
  const int inset = width >= 528 ? 24 : 18;
  const int gap = 14;
  const int coverHeight = std::max(1, std::min(300, height - 92));
  const int coverWidth = coverHeight * 62 / 100;
  const int top = y + 12;
  const int right = x + width - inset;
  const int summaryLeft = x + inset + coverWidth + gap;
  const int footerTop = top + coverHeight + 10;
  return {{x + inset, top, coverWidth, coverHeight},
          {summaryLeft, top, std::max(0, right - summaryLeft), coverHeight},
          {x + inset, footerTop, std::max(0, right - (x + inset)), std::max(0, y + height - footerTop - 8)}};
}
