#pragma once

#include "components/themes/BaseTheme.h"

namespace DashboardMetrics {
constexpr ThemeMetrics make() {
  ThemeMetrics value = BaseMetrics::values;
  // Dashboard uses the same clean, bright visual language outside Home:
  // left-anchored headers, breathable list rows and light selection pills.
  value.topPadding = 8;
  value.headerHeight = 52;
  value.verticalSpacing = 10;
  value.listRowHeight = 38;
  value.listWithSubtitleRowHeight = 58;
  value.listRowGap = 4;
  value.listRowRadius = 9;
  value.listInset = 14;
  value.listSidePadding = 14;
  value.listSelectionStyle = 1;  // light pill
  value.headerSidePadding = 20;
  value.headerUnderlineSize = 2;
  value.headerTitleAlign = 0;  // left
  // Preserve the readable large UI font while fitting all four Vietnamese
  // category names on X3/X4. Density belongs in navigation whitespace, not
  // in smaller text.
  value.tabSpacing = 2;
  value.tabBarHeight = 48;
  value.tabSpaceBetween = true;
  value.tabContentHorizontalPadding = 2;
  value.tabActiveUnderlineSize = 3;
  value.popupTopOffsetRatio = 0.14f;
  value.popupMarginX = 20;
  value.popupMarginY = 14;
  value.popupFrameThickness = 2;
  value.popupCornerRadius = 10;
  value.popupTextBold = true;
  value.popupTextInverted = true;  // black text on the Dashboard's white card
  value.popupProgressBarHeight = 5;
  value.popupProgressDrawOutline = true;
  value.popupProgressClampPercent = true;
  value.popupProgressFillInverted = true;
  value.popupProgressOutlineInverted = true;
  value.optionPopupItemSpacing = 7;
  value.optionPopupInnerPadding = 20;
  value.optionPopupSelectionVPadding = 9;
  value.controlRadius = 10;
  value.sheetRadius = 10;
  value.capsuleRadius = 255;
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

struct DashboardMenuLayout {
  Rect panel;
  Rect rows;
  int rowHeight = 0;
  int rowGap = 0;
};

class DashboardTheme final : public BaseTheme {
 public:
  static Rect coverRectForScreen(const GfxRenderer& renderer, Rect tile);
  static DashboardMenuLayout menuLayoutForScreen(const GfxRenderer& renderer, int itemCount);
  static Rect homeActionRectForScreen(const GfxRenderer& renderer, int actionIndex);
  void drawHomeMenu(GfxRenderer& renderer, const char* const* labels, const UIIcon* icons, int itemCount,
                    int selectedIndex) const;
  void drawHomeTouchActions(GfxRenderer& renderer, const char* action1, const char* action2, const char* action3,
                            const char* action4) const;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, const BookReadingStats* bookStats,
                           int progressPercent, const GlobalReadingStats* globalStats,
                           const char* currentChapterTitle) const override;
};
