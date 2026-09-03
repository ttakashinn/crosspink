#include "DashboardTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "RecentBooksStore.h"
#include "activities/reader/BookStatsActivity.h"
#include "activities/reader/ReadingStatsClock.h"
#include "components/HomeCoverCachePolicy.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/icons/afternoon.h"
#include "components/icons/book24.h"
#include "components/icons/bookmark.h"
#include "components/icons/chart.h"
#include "components/icons/evening.h"
#include "components/icons/library.h"
#include "components/icons/morning.h"
#include "components/icons/night.h"
#include "components/icons/recent.h"
#include "components/icons/streak.h"
#include "components/icons/transfer.h"
#include "fontIds.h"
namespace {

constexpr int TOP_INSET = 20;
constexpr int COVER_STATS_GAP = 15;
constexpr int STATS_WIDTH_X4 = 105;
constexpr int STATS_WIDTH_X3 = 120;
constexpr int CONTENT_INSET_X4 = 20;
constexpr int CONTENT_INSET_X3 = 75;
constexpr int PAIR_INWARD_SHIFT_X3 = 15;
constexpr int COVER_CORNER_RADIUS = 8;
constexpr int TITLE_TOP_GAP = 28;
constexpr int TITLE_SUBTITLE_GAP = 8;
constexpr int FOOTER_BOTTOM_GAP = 54;
constexpr int FOOTER_ICON_SIZE = 24;
constexpr int FOOTER_ICON_TEXT_GAP = 14;
constexpr int MENU_PANEL_MAX_WIDTH_X4 = 400;
constexpr int MENU_PANEL_MAX_WIDTH_X3 = 420;
constexpr int MENU_PANEL_MARGIN = 32;
constexpr int MENU_PANEL_PADDING = 16;
constexpr int MENU_PANEL_RADIUS = 12;
constexpr int MENU_PANEL_BORDER = 2;
constexpr int MENU_TITLE_BOTTOM_GAP = 10;
constexpr int MENU_ROW_MIN_HEIGHT = 52;
constexpr int MENU_ROW_GAP = 5;
constexpr int MENU_ROW_RADIUS = 9;
constexpr int MENU_ICON_SIZE = 32;
constexpr int MENU_ICON_TEXT_GAP = 14;
constexpr int MENU_ROW_SIDE_PADDING = 14;
constexpr int ACTION_BUTTON_GAP = 8;
constexpr int ACTION_SIDE_MARGIN_X4 = 20;
constexpr int ACTION_SIDE_MARGIN_X3 = 38;
constexpr int ACTION_BUTTON_RADIUS = 10;
constexpr int ACTION_TEXT_Y_OFFSET = 7;

struct DashboardData {
  BookReadingStats book;
  GlobalReadingStats global;
  int progressPercent = 0;
  bool progressKnown = false;
};

bool wideScreen(const GfxRenderer& renderer) { return renderer.getScreenWidth() >= 560; }

int contentInset(const GfxRenderer& renderer) { return wideScreen(renderer) ? CONTENT_INSET_X3 : CONTENT_INSET_X4; }

int pairInwardShift(const GfxRenderer& renderer) { return wideScreen(renderer) ? PAIR_INWARD_SHIFT_X3 : 0; }

DashboardData makeData(const BookReadingStats* bookStats, const int progressPercent,
                       const GlobalReadingStats* globalStats) {
  DashboardData data;
  if (bookStats) data.book = *bookStats;
  if (globalStats) data.global = *globalStats;
  if (data.book.isCompleted) {
    data.progressPercent = 100;
    data.progressKnown = true;
  } else if (progressPercent >= 0 && progressPercent <= 100) {
    data.progressPercent = progressPercent;
    data.progressKnown = true;
  } else if (data.book.progressPermille <= 1000) {
    data.progressPercent = std::clamp(static_cast<int>((data.book.progressPermille + 5) / 10), 0, 100);
    data.progressKnown = true;
  }
  return data;
}

void drawRightAligned(const GfxRenderer& renderer, const int fontId, const int right, const int y,
                      const std::string& text, const EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                      const int maxWidth = 0) {
  const std::string visible = maxWidth > 0 ? renderer.truncatedText(fontId, text.c_str(), maxWidth, style) : text;
  const int width = renderer.getTextWidth(fontId, visible.c_str(), style);
  renderer.drawText(fontId, right - width, y, visible.c_str(), true, style);
}

void drawStatsRow(const GfxRenderer& renderer, const int right, const int y, const int maxWidth,
                  const std::string& value, const char* label) {
  drawRightAligned(renderer, UI_12_FONT_ID, right, y, value, EpdFontFamily::BOLD, maxWidth);
  drawRightAligned(renderer, SMALL_FONT_ID, right, y + renderer.getLineHeight(UI_12_FONT_ID) + 1, label,
                   EpdFontFamily::REGULAR, maxWidth);
}

std::string formatPercent(const DashboardData& data) {
  return data.progressKnown ? std::to_string(data.progressPercent) + "%" : "-";
}

std::string formatPace(const BookReadingStats& stats) {
  if (stats.totalReadingSeconds < 60) return "-";
  char value[16];
  snprintf(value, sizeof(value), "%.1f", ReadingStatsMath::pagesPerMinute(stats));
  return value;
}

std::string formatDays(const uint32_t days) {
  char value[32];
  snprintf(value, sizeof(value), tr(STR_STATS_DAYS_VALUE), static_cast<unsigned>(days));
  return value;
}

std::string formatCompactDate(const uint32_t dateKey) {
  uint32_t year;
  uint8_t month;
  uint8_t day;
  if (!ReadingStatsMath::splitDateKey(dateKey, year, month, day)) return "-";
  char value[9];
  snprintf(value, sizeof(value), "%02u/%02u/%02lu", static_cast<unsigned>(day), static_cast<unsigned>(month),
           static_cast<unsigned long>(year % 100U));
  return value;
}

std::string formatStreak(const GlobalReadingStats& stats, const uint32_t today) {
  const uint32_t days = ReadingStatsMath::displayedCurrentStreak(stats, today);
  if (days == 0) return tr(STR_STATS_NO_STREAK_SHORT);
  char value[48];
  snprintf(value, sizeof(value), tr(STR_STATS_STREAK_DAYS_VALUE), static_cast<unsigned>(days));
  return value;
}

const char* readerType(const GlobalReadingStats& stats) {
  const auto best = std::max_element(stats.timeOfDaySeconds.begin(), stats.timeOfDaySeconds.end());
  if (best == stats.timeOfDaySeconds.end() || *best == 0) return tr(STR_STATS_NEW_READER_SHORT);
  switch (std::distance(stats.timeOfDaySeconds.begin(), best)) {
    case 0:
      return tr(STR_STATS_MORNING_READER_SHORT);
    case 1:
      return tr(STR_STATS_AFTERNOON_READER_SHORT);
    case 2:
      return tr(STR_STATS_EVENING_READER_SHORT);
    default:
      return tr(STR_STATS_NIGHT_READER_SHORT);
  }
}

const uint8_t* readerTypeIcon(const GlobalReadingStats& stats) {
  const auto best = std::max_element(stats.timeOfDaySeconds.begin(), stats.timeOfDaySeconds.end());
  if (best == stats.timeOfDaySeconds.end() || *best == 0) return Book24Icon;
  switch (std::distance(stats.timeOfDaySeconds.begin(), best)) {
    case 0:
      return MorningReaderIcon;
    case 1:
      return AfternoonReaderIcon;
    case 2:
      return EveningReaderIcon;
    default:
      return NightReaderIcon;
  }
}

const uint8_t* menuIcon(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Statistics:
      return ChartIcon;
    case UIIcon::Bookmark:
      return BookmarkIcon;
    case UIIcon::Transfer:
      return TransferIcon;
    default:
      return nullptr;
  }
}

void drawIconLabel(const GfxRenderer& renderer, const uint8_t* icon, const int iconX, const int centerY,
                   const char* label, const int maxTextWidth) {
  const std::string visible = renderer.truncatedText(UI_10_FONT_ID, label, std::max(1, maxTextWidth));
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawIcon(icon, iconX, centerY - FOOTER_ICON_SIZE / 2, FOOTER_ICON_SIZE);
  renderer.drawText(UI_10_FONT_ID, iconX + FOOTER_ICON_SIZE + FOOTER_ICON_TEXT_GAP, centerY - lineHeight / 2,
                    visible.c_str(), true, EpdFontFamily::BOLD);
}

void drawRightAlignedIconLabel(const GfxRenderer& renderer, const uint8_t* icon, const int right, const int centerY,
                               const char* label, const int maxTextWidth) {
  const std::string visible = renderer.truncatedText(UI_10_FONT_ID, label, std::max(1, maxTextWidth));
  const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, visible.c_str(), EpdFontFamily::BOLD);
  const int textX = right - labelWidth;
  const int iconX = textX - FOOTER_ICON_TEXT_GAP - FOOTER_ICON_SIZE;
  renderer.drawIcon(icon, iconX, centerY - FOOTER_ICON_SIZE / 2, FOOTER_ICON_SIZE);
  renderer.drawText(UI_10_FONT_ID, textX, centerY - renderer.getLineHeight(UI_10_FONT_ID) / 2, visible.c_str(), true,
                    EpdFontFamily::BOLD);
}

bool drawCover(const GfxRenderer& renderer, const Rect& target, const RecentBook& recent) {
  // A missing-thumb placeholder can be cached just before Home generates the
  // real thumbnail. Clear the whole target so black placeholder text cannot
  // bleed through the white pixels of the newly decoded 1-bit bitmap.
  renderer.fillRoundedRect(target.x, target.y, target.width, target.height, COVER_CORNER_RADIUS, Color::White);
  bool drewCover = false;
  if (!recent.coverBmpPath.empty()) {
    const std::string coverPath = UITheme::getCoverThumbPath(recent.coverBmpPath, target.height);
    HalFile file;
    if (Storage.openFileForRead("DASH", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const float scale = std::min({1.0f, static_cast<float>(target.width) / bitmap.getWidth(),
                                      static_cast<float>(target.height) / bitmap.getHeight()});
        const int width = std::max(1, static_cast<int>(bitmap.getWidth() * scale));
        const int height = std::max(1, static_cast<int>(bitmap.getHeight() * scale));
        const int x = target.x + (target.width - width) / 2;
        const int y = target.y + (target.height - height) / 2;
        renderer.drawBitmap(bitmap, x, y, width, height);
        renderer.maskRoundedRectOutsideCorners(x, y, width, height, COVER_CORNER_RADIUS, Color::White);
        renderer.drawRoundedRect(x, y, width, height, 1, COVER_CORNER_RADIUS, true);
        drewCover = true;
      }
      file.close();
    }
  }
  if (!drewCover) {
    renderer.drawRoundedRect(target.x, target.y, target.width, target.height, 1, COVER_CORNER_RADIUS, true);
    UITheme::drawCenteredWrappedText(renderer,
                                     Rect{target.x + 12, target.y + 12, target.width - 24, target.height - 24},
                                     UI_12_FONT_ID, recent.title.c_str(), 6, true, EpdFontFamily::BOLD);
  }
  return drewCover;
}

void drawBookText(const GfxRenderer& renderer, const Rect& cover, const RecentBook& recent,
                  const char* currentChapterTitle) {
  const int inset = contentInset(renderer);
  const int width = renderer.getScreenWidth() - inset * 2;
  int y = cover.y + cover.height + TITLE_TOP_GAP;
  const auto lines = renderer.wrappedText(UI_12_FONT_ID, recent.title.c_str(), width, 2, EpdFontFamily::BOLD);
  for (const auto& line : lines) {
    renderer.drawText(UI_12_FONT_ID, cover.x, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  const char* subtitle =
      currentChapterTitle != nullptr && currentChapterTitle[0] != '\0' ? currentChapterTitle : recent.author.c_str();
  if (subtitle != nullptr && subtitle[0] != '\0') {
    y += TITLE_SUBTITLE_GAP;
    const auto subtitleLines = renderer.wrappedText(UI_12_FONT_ID, subtitle, width, 2);
    for (const auto& line : subtitleLines) {
      renderer.drawText(UI_12_FONT_ID, cover.x, y, line.c_str());
      y += renderer.getLineHeight(UI_12_FONT_ID);
    }
  }
}

void drawStatsColumn(const GfxRenderer& renderer, const Rect& cover, const DashboardData& data) {
  const bool rtc = ReadingStatsClock::hasPersistentWallClock();
  const int rows = rtc ? 7 : 6;
  const int right = renderer.getScreenWidth() - contentInset(renderer) - pairInwardShift(renderer);
  const int maxWidth = std::max(1, right - (cover.x + cover.width) - COVER_STATS_GAP);
  const int valueHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int blockHeight = valueHeight + labelHeight + 1;
  const int gap = rows > 1 ? std::max(0, (cover.height - blockHeight * rows) / (rows - 1)) : 0;
  const uint32_t today = ReadingStatsClock::currentLocalDateKey();
  const uint32_t remaining =
      data.progressKnown ? ReadingStatsMath::estimatedSecondsLeft(data.book, data.progressPercent) : 0;

  std::array<std::string, 7> values;
  std::array<std::string, 7> ownedLabels;
  std::array<const char*, 7> labels{};
  int count = 0;
  values[count] = BookStatsActivity::formatDuration(data.book.totalReadingSeconds);
  labels[count++] = tr(STR_STATS_READING_TIME_SHORT);
  values[count] = remaining == 0 ? "-" : BookStatsActivity::formatDuration(remaining);
  labels[count++] = tr(STR_STATS_TIME_LEFT_SHORT);
  values[count] = formatPercent(data);
  labels[count++] = tr(STR_STATS_PROGRESS);
  if (rtc) {
    values[count] =
        data.book.firstReadDateKey == 0
            ? "-"
            : BookStatsActivity::formatDuration(ReadingStatsMath::averageCalendarDailySeconds(data.book, today));
    labels[count++] = tr(STR_STATS_DAILY_AVERAGE_SHORT);
  }
  values[count] = formatPace(data.book);
  labels[count++] = tr(STR_STATS_PAGES_PER_MINUTE_SHORT);
  if (!rtc) {
    values[count] = std::to_string(data.book.sessionCount);
    labels[count++] = tr(STR_STATS_SESSIONS_SHORT);
    values[count] = BookStatsActivity::formatDuration(ReadingStatsMath::averageSessionSeconds(data.book));
    labels[count++] = tr(STR_STATS_AVERAGE_SESSION_SHORT);
  } else {
    const uint32_t endDate =
        data.book.isCompleted && data.book.completedDateKey != 0 ? data.book.completedDateKey : today;
    values[count] = data.book.firstReadDateKey == 0
                        ? "-"
                        : formatDays(ReadingStatsMath::calendarDaysElapsed(data.book.firstReadDateKey, endDate));
    ownedLabels[count] = std::string(tr(STR_STATS_FROM_SHORT)) + " " + formatCompactDate(data.book.firstReadDateKey);
    labels[count] = ownedLabels[count].c_str();
    ++count;
    const uint32_t finish = data.book.isCompleted
                                ? data.book.completedDateKey
                                : ReadingStatsMath::estimatedFinishDateKey(data.book, data.progressPercent, today);
    values[count] = formatCompactDate(finish);
    labels[count++] = data.book.isCompleted ? tr(STR_STATS_FINISHED) : tr(STR_STATS_ESTIMATED_FINISH_SHORT);
  }

  for (int i = 0; i < count; ++i) {
    drawStatsRow(renderer, right, cover.y + i * (blockHeight + gap), maxWidth, values[i], labels[i]);
  }
}

void drawFooter(const GfxRenderer& renderer, const Rect& cover, const DashboardData& data) {
  const int inset = contentInset(renderer);
  // Dashboard Home always has a visible action row: physical-button hints on
  // X3/X4 and actual touch targets on X4 Pro.
  const int buttonHintReserve = DashboardMetrics::values.buttonHintsHeight;
  const int centerY = renderer.getScreenHeight() - buttonHintReserve - FOOTER_BOTTOM_GAP;
  const int half = renderer.getScreenWidth() / 2;
  if (ReadingStatsClock::hasPersistentWallClock()) {
    const std::string streak = formatStreak(data.global, ReadingStatsClock::currentLocalDateKey());
    const int maxTextWidth = half - inset - FOOTER_ICON_SIZE - FOOTER_ICON_TEXT_GAP - 8;
    drawIconLabel(renderer, StreakIcon, cover.x, centerY, streak.c_str(), maxTextWidth);
    drawRightAlignedIconLabel(renderer, readerTypeIcon(data.global),
                              renderer.getScreenWidth() - inset - pairInwardShift(renderer), centerY,
                              readerType(data.global), maxTextWidth);
    return;
  }

  renderer.drawText(UI_12_FONT_ID, cover.x, centerY - renderer.getLineHeight(UI_12_FONT_ID),
                    BookStatsActivity::formatDuration(data.global.totalReadingSeconds).c_str(), true,
                    EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, cover.x, centerY + 2, tr(STR_STATS_TOTAL_TIME));
  const std::string books = std::to_string(data.global.completedBooks);
  drawRightAligned(renderer, UI_12_FONT_ID, renderer.getScreenWidth() - inset,
                   centerY - renderer.getLineHeight(UI_12_FONT_ID), books, EpdFontFamily::BOLD);
  drawRightAligned(renderer, SMALL_FONT_ID, renderer.getScreenWidth() - inset, centerY + 2,
                   tr(STR_STATS_BOOKS_COMPLETED));
}

void drawHomeActionRow(GfxRenderer& renderer, const char* const* labels) {
  for (int i = 0; i < 4; ++i) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const Rect button = DashboardTheme::homeActionRectForScreen(renderer, i);
    renderer.fillRoundedRect(button.x, button.y, button.width, button.height, ACTION_BUTTON_RADIUS, true, true, false,
                             false, Color::White);
    renderer.drawRoundedRect(button.x, button.y, button.width, button.height, 1, ACTION_BUTTON_RADIUS, true, true,
                             false, false, true);
    // drawRoundedRect intentionally omits the unrounded lower corners; extend
    // the side rails to the panel edge while keeping the bottom border open.
    renderer.drawLine(button.x, button.y + ACTION_BUTTON_RADIUS, button.x, renderer.getScreenHeight() - 1, true);
    renderer.drawLine(button.x + button.width - 1, button.y + ACTION_BUTTON_RADIUS, button.x + button.width - 1,
                      renderer.getScreenHeight() - 1, true);
    BaseTheme::drawHintLabel(renderer, UI_10_FONT_ID, labels[i], button.x, button.width, button.y, button.height,
                             ACTION_TEXT_Y_OFFSET);
  }
}

}  // namespace

void DashboardTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                     const char* btn4) const {
  if (gpio.hasTouch()) return;

  const GfxRenderer::Orientation originalOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const char* labels[] = {btn1, btn2, btn3, btn4};
  drawHomeActionRow(renderer, labels);

  renderer.setOrientation(originalOrientation);
}

void DashboardTheme::drawHomeTouchActions(GfxRenderer& renderer, const char* action1, const char* action2,
                                          const char* action3, const char* action4) const {
  const GfxRenderer::Orientation originalOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const char* labels[] = {action1, action2, action3, action4};
  drawHomeActionRow(renderer, labels);
  renderer.setOrientation(originalOrientation);
}

Rect DashboardTheme::coverRectForScreen(const GfxRenderer& renderer, const Rect tile) {
  const int inset = contentInset(renderer);
  const int statsWidth = wideScreen(renderer) ? STATS_WIDTH_X3 : STATS_WIDTH_X4;
  const int maximumWidth = renderer.getScreenWidth() - inset * 2 - statsWidth - COVER_STATS_GAP;
  const int width = std::max(1, std::min(DashboardMetrics::coverImageWidth, maximumWidth));
  const int height = std::max(1, std::min(DashboardMetrics::coverImageHeight, width * 3 / 2));
  return Rect{inset + pairInwardShift(renderer), tile.y + TOP_INSET, width, height};
}

Rect DashboardTheme::homeActionRectForScreen(const GfxRenderer& renderer, const int actionIndex) {
  if (actionIndex < 0 || actionIndex >= 4) return Rect{};
  const int screenWidth = renderer.getScreenWidth();
  const int sideMargin = screenWidth >= 528 ? ACTION_SIDE_MARGIN_X3 : ACTION_SIDE_MARGIN_X4;
  const int available = screenWidth - sideMargin * 2 - ACTION_BUTTON_GAP * 3;
  const int buttonWidth = std::max(1, available / 4);
  const int usedWidth = buttonWidth * 4 + ACTION_BUTTON_GAP * 3;
  const int startX = (screenWidth - usedWidth) / 2;
  return Rect{startX + actionIndex * (buttonWidth + ACTION_BUTTON_GAP),
              renderer.getScreenHeight() - DashboardMetrics::values.buttonHintsHeight, buttonWidth,
              DashboardMetrics::values.buttonHintsHeight};
}

DashboardMenuLayout DashboardTheme::menuLayoutForScreen(const GfxRenderer& renderer, const int itemCount) {
  const auto spec = uiScaleSpec();
  const int rowHeight = std::max(MENU_ROW_MIN_HEIGHT, renderer.getLineHeight(spec.bodyFontId) + 20);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int rowsHeight = std::max(0, itemCount) * rowHeight + std::max(0, itemCount - 1) * MENU_ROW_GAP;
  const int panelHeight = MENU_PANEL_PADDING + titleHeight + MENU_TITLE_BOTTOM_GAP + rowsHeight + MENU_PANEL_PADDING;
  const int maxWidth = wideScreen(renderer) ? MENU_PANEL_MAX_WIDTH_X3 : MENU_PANEL_MAX_WIDTH_X4;
  const int panelWidth = std::max(1, std::min(maxWidth, renderer.getScreenWidth() - MENU_PANEL_MARGIN * 2));
  const int safeTop = DashboardMetrics::values.homeTopPadding + MENU_PANEL_MARGIN;
  const int buttonHintReserve = DashboardMetrics::values.buttonHintsHeight;
  const int safeBottom = renderer.getScreenHeight() - buttonHintReserve - MENU_PANEL_MARGIN;
  const int panelY = safeTop + std::max(0, safeBottom - safeTop - panelHeight) / 2;
  const Rect panel{(renderer.getScreenWidth() - panelWidth) / 2, panelY, panelWidth, panelHeight};
  const Rect rows{panel.x + MENU_PANEL_PADDING, panel.y + MENU_PANEL_PADDING + titleHeight + MENU_TITLE_BOTTOM_GAP,
                  panel.width - MENU_PANEL_PADDING * 2, rowsHeight};
  return DashboardMenuLayout{panel, rows, rowHeight, MENU_ROW_GAP};
}

void DashboardTheme::drawHomeMenu(GfxRenderer& renderer, const char* const* labels, const UIIcon* icons,
                                  const int itemCount, const int selectedIndex) const {
  const DashboardMenuLayout layout = menuLayoutForScreen(renderer, itemCount);
  renderer.fillRoundedRect(layout.panel.x, layout.panel.y, layout.panel.width, layout.panel.height, MENU_PANEL_RADIUS,
                           Color::White);
  renderer.drawRoundedRect(layout.panel.x, layout.panel.y, layout.panel.width, layout.panel.height, MENU_PANEL_BORDER,
                           MENU_PANEL_RADIUS, true);

  const int titleY = layout.panel.y + MENU_PANEL_PADDING;
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, tr(STR_DASHBOARD_MENU), true, EpdFontFamily::BOLD);

  const auto spec = uiScaleSpec();
  const int lineHeight = renderer.getLineHeight(spec.bodyFontId);
  for (int i = 0; i < itemCount; ++i) {
    const int rowY = layout.rows.y + i * (layout.rowHeight + layout.rowGap);
    const bool selected = i == selectedIndex;
    if (selected) {
      renderer.fillRoundedRect(layout.rows.x, rowY, layout.rows.width, layout.rowHeight, MENU_ROW_RADIUS,
                               Color::LightGray);
      renderer.fillRoundedRect(layout.rows.x, rowY + 8, 4, layout.rowHeight - 16, 2, Color::Black);
    }

    const uint8_t* icon = menuIcon(icons[i]);
    const int iconX = layout.rows.x + MENU_ROW_SIDE_PADDING;
    if (icon != nullptr) renderer.drawIcon(icon, iconX, rowY + (layout.rowHeight - MENU_ICON_SIZE) / 2, MENU_ICON_SIZE);
    const int textX = iconX + MENU_ICON_SIZE + MENU_ICON_TEXT_GAP;
    const int textWidth = layout.rows.x + layout.rows.width - MENU_ROW_SIDE_PADDING - textX;
    const std::string visible = renderer.truncatedText(spec.bodyFontId, labels[i], std::max(1, textWidth),
                                                       selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(spec.bodyFontId, textX, rowY + (layout.rowHeight - lineHeight) / 2, visible.c_str(), true,
                      selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}

void DashboardTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                         const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                         bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                         std::function<bool()> storeCoverBuffer, const BookReadingStats* bookStats,
                                         const int progressPercent, const GlobalReadingStats* globalStats,
                                         const char* currentChapterTitle) const {
  (void)selectorIndex;
  const Rect cover = coverRectForScreen(renderer, rect);
  if (recentBooks.empty()) {
    renderer.drawRoundedRect(cover.x, cover.y, cover.width, cover.height, 1, COVER_CORNER_RADIUS, true);
    renderer.drawCenteredText(UI_12_FONT_ID, cover.y + cover.height / 2 - 20, tr(STR_NO_OPEN_BOOK), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, cover.y + cover.height / 2 + 16, tr(STR_START_READING));
    coverRendered = false;
    coverBufferStored = false;
    return;
  }

  const RecentBook& recent = recentBooks[0];
  if (!bufferRestored || !coverRendered) {
    const bool renderedCover = drawCover(renderer, cover, recent);
    if (shouldCacheHomeCover(!recent.coverBmpPath.empty(), renderedCover)) {
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;
    } else {
      coverBufferStored = false;
      coverRendered = false;
    }
  }
  const DashboardData data = makeData(bookStats, progressPercent, globalStats);
  drawStatsColumn(renderer, cover, data);
  drawBookText(renderer, cover, recent, currentChapterTitle);
  drawFooter(renderer, cover, data);
}
