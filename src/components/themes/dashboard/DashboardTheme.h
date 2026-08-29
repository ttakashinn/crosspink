#pragma once

#include "components/themes/BaseTheme.h"

namespace DashboardMetrics {
constexpr ThemeMetrics make() {
  ThemeMetrics value = BaseMetrics::values;
  value.homeTopPadding = 50;
  value.homeCoverHeight = 444;
  value.homeCoverTileHeight = 690;
  value.homeRecentBooksCount = 1;
  value.homeContinueReadingInMenu = false;
  value.homeMenuTopOffset = 4;
  value.menuSpacing = 4;
  return value;
}
inline constexpr ThemeMetrics values = make();
inline constexpr int coverImageWidth = 296;
inline constexpr int coverImageHeight = 444;
}  // namespace DashboardMetrics

class DashboardTheme final : public BaseTheme {
 public:
  static Rect coverRectForScreen(const GfxRenderer& renderer, Rect tile);
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, const BookReadingStats* bookStats,
                           int progressPercent, const GlobalReadingStats* globalStats,
                           const char* currentChapterTitle) const override;
};
