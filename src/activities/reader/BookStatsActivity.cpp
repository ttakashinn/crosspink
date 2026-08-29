#include "BookStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "ReadingStatsClock.h"
#include "ReadingStatsLayout.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int CARD_GAP = 8;
constexpr int CARD_TITLE_HEIGHT = 32;

struct EditDateLayout {
  std::array<ReadingStatsRect, 6> fields{};
  ReadingStatsRect decrease;
  ReadingStatsRect increase;
  int titleY = 0;
  std::array<int, 2> labelY{};
};

EditDateLayout makeEditDateLayout(const GfxRenderer& renderer, const ThemeMetrics& metrics) {
  EditDateLayout layout;
  const int width = renderer.getScreenWidth();
  const int side = metrics.contentSidePadding;
  const int fieldGap = 8;
  const int fieldWidth = (width - side * 2 - fieldGap * 2) / 3;
  constexpr int fieldHeight = 54;
  layout.titleY = metrics.topPadding + metrics.headerHeight + 20;
  int y = layout.titleY + renderer.getLineHeight(UI_10_FONT_ID) + 32;
  for (int row = 0; row < 2; ++row) {
    layout.labelY[row] = y;
    y += renderer.getLineHeight(UI_10_FONT_ID) + 10;
    for (int column = 0; column < 3; ++column) {
      layout.fields[row * 3 + column] = {side + column * (fieldWidth + fieldGap), y, fieldWidth, fieldHeight};
    }
    y += 100;
  }
  constexpr int buttonGap = 14;
  constexpr int buttonHeight = 58;
  const int buttonTop = layout.fields[3].bottom() + 24;
  const int buttonWidth = (width - side * 2 - buttonGap) / 2;
  layout.decrease = {side, buttonTop, buttonWidth, buttonHeight};
  layout.increase = {side + buttonWidth + buttonGap, buttonTop, buttonWidth, buttonHeight};
  return layout;
}

void drawCentered(const GfxRenderer& renderer, const int fontId, const int x, const int width, const int y,
                  const std::string& text, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const std::string visible = renderer.truncatedText(fontId, text.c_str(), std::max(1, width - 8), style);
  const int textWidth = renderer.getTextWidth(fontId, visible.c_str(), style);
  renderer.drawText(fontId, x + (width - textWidth) / 2, y, visible.c_str(), true, style);
}

void drawMetricCell(const GfxRenderer& renderer, const ReadingStatsRect& rect, const std::string& value,
                    const std::string& label) {
  if (rect.width <= 0 || rect.height <= 0) return;
  const int valueLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int top = rect.y + std::max(2, (rect.height - valueLine - labelLine - 3) / 2);
  drawCentered(renderer, UI_12_FONT_ID, rect.x, rect.width, top, value, EpdFontFamily::BOLD);
  drawCentered(renderer, SMALL_FONT_ID, rect.x, rect.width, top + valueLine + 3, label);
}

template <size_t N>
void drawMetricCard(const GfxRenderer& renderer, const ReadingStatsRect& rect, const std::string& heading,
                    const std::array<std::string, N>& labels, const std::array<std::string, N>& values,
                    const int columns) {
  if (rect.width <= 0 || rect.height <= CARD_TITLE_HEIGHT || N == 0) return;
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  renderer.drawLine(rect.x, rect.y + CARD_TITLE_HEIGHT, rect.right(), rect.y + CARD_TITLE_HEIGHT);
  drawCentered(renderer, UI_10_FONT_ID, rect.x, rect.width,
               rect.y + (CARD_TITLE_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2, heading, EpdFontFamily::BOLD);

  const int rows = (static_cast<int>(N) + columns - 1) / columns;
  const int bodyHeight = rect.height - CARD_TITLE_HEIGHT;
  for (int row = 0; row < rows; ++row) {
    const int first = row * columns;
    const int count = std::min(columns, static_cast<int>(N) - first);
    const int top = rect.y + CARD_TITLE_HEIGHT + bodyHeight * row / rows;
    const int bottom = rect.y + CARD_TITLE_HEIGHT + bodyHeight * (row + 1) / rows;
    for (int column = 0; column < count; ++column) {
      const int left = rect.x + rect.width * column / count;
      const int right = rect.x + rect.width * (column + 1) / count;
      drawMetricCell(renderer, {left, top, right - left, bottom - top}, values[first + column], labels[first + column]);
    }
  }
}

template <size_t N>
void drawBarCard(const GfxRenderer& renderer, const ReadingStatsRect& rect, const char* heading,
                 const std::array<uint32_t, N>& values, const std::array<const char*, N>& labels) {
  if (rect.width <= 0 || rect.height <= CARD_TITLE_HEIGHT || N == 0) return;
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  renderer.drawLine(rect.x, rect.y + CARD_TITLE_HEIGHT, rect.right(), rect.y + CARD_TITLE_HEIGHT);
  drawCentered(renderer, UI_10_FONT_ID, rect.x, rect.width,
               rect.y + (CARD_TITLE_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2, heading, EpdFontFamily::BOLD);

  const uint32_t maximum = *std::max_element(values.begin(), values.end());
  const int labelWidth = N > 4 ? 54 : 76;
  const int left = rect.x + 10;
  const int barX = left + labelWidth;
  const int barWidth = std::max(1, rect.right() - barX - 14);
  const int bodyTop = rect.y + CARD_TITLE_HEIGHT + 5;
  const int bodyHeight = std::max(1, rect.bottom() - bodyTop - 5);
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  for (size_t i = 0; i < N; ++i) {
    const int rowTop = bodyTop + bodyHeight * static_cast<int>(i) / static_cast<int>(N);
    const int rowBottom = bodyTop + bodyHeight * static_cast<int>(i + 1) / static_cast<int>(N);
    const int rowHeight = std::max(1, rowBottom - rowTop);
    renderer.drawText(SMALL_FONT_ID, left, rowTop + std::max(0, (rowHeight - lineHeight) / 2), labels[i]);
    if (maximum > 0 && values[i] > 0) {
      const int fill = std::max(2, static_cast<int>(static_cast<uint64_t>(barWidth) * values[i] / maximum));
      const int barHeight = std::clamp(rowHeight - 8, 5, 16);
      renderer.fillRect(barX, rowTop + (rowHeight - barHeight) / 2, fill, barHeight, true);
    }
  }
}

std::string formatFloat(const float value) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

std::string formatDays(const uint32_t days) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), tr(STR_STATS_DAYS_VALUE), static_cast<unsigned>(days));
  return buffer;
}

std::string formatProgress(const int progressPercent) {
  return progressPercent >= 0 && progressPercent <= 100 ? std::to_string(progressPercent) + "%" : "-";
}

std::string dateLabel(const char* label, const uint32_t dateKey) {
  return std::string(label) + " " + BookStatsActivity::formatDate(dateKey);
}

uint32_t finishDisplayDate(const BookReadingStats& stats, const int progressPercent, const uint32_t today) {
  return stats.isCompleted ? stats.completedDateKey
                           : ReadingStatsMath::estimatedFinishDateKey(stats, progressPercent, today);
}

void splitOrDefaultDate(uint32_t dateKey, uint32_t& year, uint8_t& month, uint8_t& day) {
  if (!ReadingStatsMath::splitDateKey(dateKey, year, month, day)) {
    year = 2000;
    month = 1;
    day = 1;
  }
}

}  // namespace

std::string BookStatsActivity::formatDuration(const uint32_t seconds) {
  if (seconds < 60) return tr(STR_STATS_LESS_THAN_MINUTE);
  const uint32_t minutes = seconds / 60 + (seconds % 60 >= 30 ? 1 : 0);
  if (minutes < 60) return std::to_string(minutes) + " " + tr(STR_STATS_MINUTES_SHORT);
  const uint32_t hours = minutes / 60;
  const uint32_t remainder = minutes % 60;
  return remainder == 0 ? std::to_string(hours) + " " + tr(STR_STATS_HOURS_SHORT)
                        : std::to_string(hours) + " " + tr(STR_STATS_HOURS_SHORT) + " " + std::to_string(remainder) +
                              " " + tr(STR_STATS_MINUTES_SHORT);
}

std::string BookStatsActivity::formatDate(const uint32_t dateKey) {
  uint32_t year;
  uint8_t month;
  uint8_t day;
  if (!ReadingStatsMath::splitDateKey(dateKey, year, month, day)) return "-";
  char value[11];
  snprintf(value, sizeof(value), "%02u/%02u/%04lu", static_cast<unsigned>(day), static_cast<unsigned>(month),
           static_cast<unsigned long>(year));
  return value;
}

bool BookStatsActivity::handleHomeGesture() {
  saveEdits();
  finish();
  return true;
}

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  requestUpdate();
}

void BookStatsActivity::onExit() {
  saveEdits();
  renderer.setOrientation(previousOrientation);
  Activity::onExit();
}

void BookStatsActivity::showNextPage() {
  if (page == Page::BOOK) {
    page = Page::DEVICE;
    requestUpdate();
  }
}

void BookStatsActivity::showPreviousPage() {
  if (page == Page::DEVICE) {
    page = Page::BOOK;
    requestUpdate();
  }
}

void BookStatsActivity::clearSelectedDate() {
  const bool finishDate = selectedDateField >= 3;
  if (finishDate) {
    if (stats.isCompleted && globalStats.completedBooks > 0) --globalStats.completedBooks;
    stats.completedDateKey = 0;
    stats.completedDateManual = false;
    stats.isCompleted = false;
  } else {
    stats.firstReadDateKey = 0;
    stats.firstReadDateManual = false;
  }
  editsChanged = true;
  requestUpdate();
}

void BookStatsActivity::adjustSelectedDate(const int delta) {
  const bool finishDate = selectedDateField >= 3;
  const int field = selectedDateField % 3;
  uint32_t& dateKey = finishDate ? stats.completedDateKey : stats.firstReadDateKey;
  uint32_t year;
  uint8_t month;
  uint8_t day;

  if (dateKey == 0) {
    dateKey = ReadingStatsClock::currentLocalDateKey();
    if (dateKey == 0) dateKey = finishDate && stats.firstReadDateKey != 0 ? stats.firstReadDateKey : 20000101;
  }
  splitOrDefaultDate(dateKey, year, month, day);

  // Keep the edit order identical to the displayed Vietnamese date format:
  // day / month / year. CrossInk uses month / day / year here, which makes the
  // control error-prone when the rest of the VNS UI formats dates as DD/MM/YYYY.
  const bool clearAtBoundary =
      (field == 0 && ((day == 1 && delta < 0) || (day == ReadingStatsMath::daysInMonth(year, month) && delta > 0))) ||
      (field == 1 && ((month == 1 && delta < 0) || (month == 12 && delta > 0))) ||
      (field == 2 && ((year == 2000 && delta < 0) || (year == 2099 && delta > 0)));
  if (clearAtBoundary) {
    clearSelectedDate();
    return;
  }

  if (field == 0) {
    int value = static_cast<int>(day) + delta;
    const int days = ReadingStatsMath::daysInMonth(year, month);
    if (value < 1) value = days;
    if (value > days) value = 1;
    day = static_cast<uint8_t>(value);
  } else if (field == 1) {
    int value = static_cast<int>(month) + delta;
    if (value < 1) value = 12;
    if (value > 12) value = 1;
    month = static_cast<uint8_t>(value);
    day = std::min(day, ReadingStatsMath::daysInMonth(year, month));
  } else {
    int value = static_cast<int>(year) + delta;
    if (value < 2000) value = 2099;
    if (value > 2099) value = 2000;
    year = static_cast<uint32_t>(value);
    day = std::min(day, ReadingStatsMath::daysInMonth(year, month));
  }
  dateKey = ReadingStatsMath::makeDateKey(year, month, day);

  if (finishDate) {
    if (!stats.isCompleted) {
      globalStats.completedBooks = ReadingStatsMath::saturatedAdd<uint32_t>(globalStats.completedBooks, 1U);
    }
    stats.isCompleted = true;
    stats.completedDateManual = true;
    if (stats.firstReadDateKey != 0 && stats.completedDateKey < stats.firstReadDateKey) {
      stats.firstReadDateKey = stats.completedDateKey;
    }
  } else {
    stats.firstReadDateManual = true;
    if (stats.completedDateKey != 0 && stats.firstReadDateKey > stats.completedDateKey) {
      stats.completedDateKey = stats.firstReadDateKey;
    }
  }
  editsChanged = true;
  requestUpdate();
}

void BookStatsActivity::saveEdits() {
  if (!editsChanged || !hasEditableBook()) return;

  BookReadingStats persisted;
  const auto bookLoad = ReadingStatsStore::loadBook(sourcePath, cachePath, persisted);
  if (bookLoad == ReadingStatsStore::LoadStatus::NEWER_VERSION || bookLoad == ReadingStatsStore::LoadStatus::IO_ERROR) {
    LOG_ERR("RSTAT", "Cannot save edited dates over unreadable/newer per-book stats");
    return;
  }
  if (bookLoad == ReadingStatsStore::LoadStatus::INVALID) persisted = {};

  const bool wasCompleted = persisted.isCompleted;
  persisted.firstReadDateKey = stats.firstReadDateKey;
  persisted.firstReadDateManual = stats.firstReadDateManual;
  persisted.completedDateKey = stats.completedDateKey;
  persisted.completedDateManual = stats.completedDateManual;
  persisted.isCompleted = stats.isCompleted;

  GlobalReadingStats persistedGlobal;
  const auto globalLoad = ReadingStatsStore::loadGlobal(persistedGlobal);
  const bool globalWritable = globalLoad != ReadingStatsStore::LoadStatus::NEWER_VERSION &&
                              globalLoad != ReadingStatsStore::LoadStatus::IO_ERROR;
  if (globalLoad == ReadingStatsStore::LoadStatus::INVALID) persistedGlobal = {};
  if (globalWritable && wasCompleted != persisted.isCompleted) {
    if (persisted.isCompleted) {
      persistedGlobal.completedBooks = ReadingStatsMath::saturatedAdd<uint32_t>(persistedGlobal.completedBooks, 1U);
    } else if (persistedGlobal.completedBooks > 0) {
      --persistedGlobal.completedBooks;
    }
  }

  if (ReadingStatsStore::saveBook(sourcePath, cachePath, persisted) != ReadingStatsStore::SaveStatus::SAVED) {
    LOG_ERR("RSTAT", "Could not save edited reading dates");
    return;
  }
  if (globalWritable && wasCompleted != persisted.isCompleted &&
      ReadingStatsStore::saveGlobal(persistedGlobal) != ReadingStatsStore::SaveStatus::SAVED) {
    LOG_ERR("RSTAT", "Book completion changed but global reading stats could not be saved");
    return;
  }
  editsChanged = false;
}

void BookStatsActivity::loop() {
  if (page == Page::EDIT_DATES) {
    if (mappedInput.hasTouch()) {
      const auto& metrics = UITheme::getInstance().getMetrics();
      if (mappedInput.wasTapInRect(0, metrics.topPadding, std::min(110, renderer.getScreenWidth()),
                                   metrics.headerHeight)) {
        saveEdits();
        page = Page::BOOK;
        requestUpdate();
        return;
      }
      const auto layout = makeEditDateLayout(renderer, metrics);
      for (int index = 0; index < static_cast<int>(layout.fields.size()); ++index) {
        const auto& field = layout.fields[index];
        if (mappedInput.wasTapInRect(field.x, field.y, field.width, field.height)) {
          selectedDateField = index;
          requestUpdate();
          return;
        }
      }
      if (mappedInput.wasTapInRect(layout.decrease.x, layout.decrease.y, layout.decrease.width,
                                   layout.decrease.height)) {
        adjustSelectedDate(-1);
        return;
      }
      if (mappedInput.wasTapInRect(layout.increase.x, layout.increase.y, layout.increase.width,
                                   layout.increase.height)) {
        adjustSelectedDate(1);
        return;
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      saveEdits();
      page = Page::BOOK;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      selectedDateField = (selectedDateField + 1) % 6;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      adjustSelectedDate(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      adjustSelectedDate(1);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    if (mappedInput.wasTapInRect(0, metrics.topPadding, std::min(110, renderer.getScreenWidth()),
                                 metrics.headerHeight)) {
      finish();
      return;
    }
  }
  if (!ReadingStatsClock::hasPersistentWallClock()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Right) {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (page == Page::BOOK && hasEditableBook()) {
      page = Page::EDIT_DATES;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (mappedInput.hasTouch() && page == Page::BOOK && hasEditableBook()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + 6;
    const int side = metrics.contentSidePadding;
    constexpr int summaryHeight = 212;
    const int thirdRowTop = contentTop + CARD_TITLE_HEIGHT + (summaryHeight - CARD_TITLE_HEIGHT) * 2 / 3;
    if (mappedInput.wasTapInRect(side, thirdRowTop, (renderer.getScreenWidth() - side * 2) / 2,
                                 contentTop + summaryHeight - thirdRowTop)) {
      page = Page::EDIT_DATES;
      requestUpdate();
      return;
    }
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left || mappedInput.wasPressed(MappedInputManager::Button::Down) ||
      mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    showNextPage();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right || mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (page == Page::BOOK && swipe == MappedInputManager::SwipeDir::Right)
      finish();
    else
      showPreviousPage();
  }
}

void BookStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  if (page == Page::EDIT_DATES) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_STATS_EDIT_DATES));
    const auto layout = makeEditDateLayout(renderer, metrics);
    const int side = metrics.contentSidePadding;
    drawCentered(renderer, UI_10_FONT_ID, side, width - side * 2, layout.titleY, title, EpdFontFamily::BOLD);
    const std::array<uint32_t, 2> dates = {stats.firstReadDateKey, stats.completedDateKey};
    const std::array<const char*, 2> labels = {tr(STR_STATS_START_DATE), tr(STR_STATS_FINISH_DATE)};
    for (int row = 0; row < 2; ++row) {
      renderer.drawText(UI_10_FONT_ID, side, layout.labelY[row], labels[row], true, EpdFontFamily::BOLD);
      uint32_t year;
      uint8_t month;
      uint8_t day;
      const bool valid = ReadingStatsMath::splitDateKey(dates[row], year, month, day);
      const std::array<unsigned, 3> values = {valid ? static_cast<unsigned>(day) : 0U,
                                              valid ? static_cast<unsigned>(month) : 0U,
                                              valid ? static_cast<unsigned>(year) : 0U};
      const std::array<const char*, 3> fieldLabels = {tr(STR_STATS_DAY_FIELD), tr(STR_STATS_MONTH_FIELD),
                                                      tr(STR_STATS_YEAR_FIELD)};
      for (int column = 0; column < 3; ++column) {
        const auto& field = layout.fields[row * 3 + column];
        renderer.drawRect(field.x, field.y, field.width, field.height);
        if (selectedDateField == row * 3 + column)
          renderer.drawRect(field.x + 2, field.y + 2, field.width - 4, field.height - 4);
        char value[8];
        if (!valid)
          snprintf(value, sizeof(value), "--");
        else if (column == 2)
          snprintf(value, sizeof(value), "%04u", values[column]);
        else
          snprintf(value, sizeof(value), "%02u", values[column]);
        drawCentered(renderer, SMALL_FONT_ID, field.x, field.width, field.y + 4, fieldLabels[column]);
        drawCentered(renderer, UI_12_FONT_ID, field.x, field.width,
                     field.bottom() - renderer.getLineHeight(UI_12_FONT_ID) - 4, value, EpdFontFamily::BOLD);
      }
    }
    if (mappedInput.hasTouch()) {
      renderer.drawRect(layout.decrease.x, layout.decrease.y, layout.decrease.width, layout.decrease.height);
      renderer.drawRect(layout.increase.x, layout.increase.y, layout.increase.width, layout.increase.height);
      drawCentered(renderer, UI_12_FONT_ID, layout.decrease.x, layout.decrease.width,
                   layout.decrease.y + (layout.decrease.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2, "-",
                   EpdFontFamily::BOLD);
      drawCentered(renderer, UI_12_FONT_ID, layout.increase.x, layout.increase.width,
                   layout.increase.y + (layout.increase.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2, "+",
                   EpdFontFamily::BOLD);
    }
    const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
    GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
    renderer.displayBuffer();
    return;
  }

  const bool rtc = ReadingStatsClock::hasPersistentWallClock();
  const char* header = rtc && page == Page::DEVICE ? tr(STR_STATS_THIS_DEVICE) : tr(STR_READING_STATS);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, header);
  const int hintReserve = mappedInput.hasTouch() ? 0 : metrics.buttonHintsHeight;
  const int contentTop = metrics.topPadding + metrics.headerHeight + 6;
  const int contentBottom = height - hintReserve - 10;
  const uint32_t today = ReadingStatsClock::currentLocalDateKey();
  const uint32_t estimate = ReadingStatsMath::estimatedSecondsLeft(stats, progressPercent);

  if (!rtc) {
    const auto layout = makeReadingStatsScreenLayout(width, height, contentTop, hintReserve);
    const std::array<std::string, 6> bookLabels = {tr(STR_STATS_SESSIONS_SHORT),  tr(STR_STATS_READING_TIME_SHORT),
                                                   tr(STR_STATS_PROGRESS),        tr(STR_STATS_AVERAGE_SESSION_SHORT),
                                                   tr(STR_STATS_TIME_LEFT_SHORT), tr(STR_STATS_PAGES_PER_MINUTE_SHORT)};
    const std::array<std::string, 6> bookValues = {std::to_string(stats.sessionCount),
                                                   formatDuration(stats.totalReadingSeconds),
                                                   formatProgress(progressPercent),
                                                   formatDuration(ReadingStatsMath::averageSessionSeconds(stats)),
                                                   estimate == 0 ? "-" : formatDuration(estimate),
                                                   formatFloat(ReadingStatsMath::pagesPerMinute(stats))};
    const std::array<std::string, 5> globalLabels = {
        tr(STR_STATS_SESSIONS_SHORT), tr(STR_STATS_TOTAL_TIME_SHORT), tr(STR_STATS_PAGES_PER_MINUTE_SHORT),
        tr(STR_STATS_AVERAGE_SESSION_SHORT), tr(STR_STATS_BOOKS_COMPLETED_SHORT)};
    const std::array<std::string, 5> globalValues = {
        std::to_string(globalStats.sessionCount), formatDuration(globalStats.totalReadingSeconds),
        formatFloat(ReadingStatsMath::pagesPerMinute(globalStats)),
        formatDuration(ReadingStatsMath::averageSessionSeconds(globalStats)),
        std::to_string(globalStats.completedBooks)};
    drawMetricCard(renderer, layout.currentBook, title, bookLabels, bookValues, 3);
    drawMetricCard(renderer, layout.thisDevice, tr(STR_STATS_THIS_DEVICE), globalLabels, globalValues, 3);
  } else {
    const int side = metrics.contentSidePadding;
    const int cardWidth = width - side * 2;
    // Three metric rows need enough room for Vietnamese labels and their
    // diacritics. The earlier 182 px card let the final label raster cross the
    // card border on X3 even though the shorter English text appeared valid.
    const int summaryHeight = page == Page::BOOK ? 212 : 154;
    const int remaining = std::max(0, contentBottom - contentTop - summaryHeight - CARD_GAP * 2);
    const int timeHeight = std::max(120, remaining * 4 / 11);
    const int dayHeight = std::max(0, remaining - timeHeight);
    const ReadingStatsRect summary{side, contentTop, cardWidth, summaryHeight};
    const ReadingStatsRect timeCard{side, summary.bottom() + CARD_GAP, cardWidth, timeHeight};
    const ReadingStatsRect dayCard{side, timeCard.bottom() + CARD_GAP, cardWidth, dayHeight};
    const std::array<const char*, 4> timeLabels = {tr(STR_STATS_MORNING), tr(STR_STATS_AFTERNOON),
                                                   tr(STR_STATS_EVENING), tr(STR_STATS_NIGHT)};
    const std::array<const char*, 7> dayLabels = {tr(STR_STATS_MON), tr(STR_STATS_TUE), tr(STR_STATS_WED),
                                                  tr(STR_STATS_THU), tr(STR_STATS_FRI), tr(STR_STATS_SAT),
                                                  tr(STR_STATS_SUN)};

    if (page == Page::BOOK) {
      const uint32_t endDate = stats.isCompleted && stats.completedDateKey != 0 ? stats.completedDateKey : today;
      const uint32_t days = ReadingStatsMath::calendarDaysElapsed(stats.firstReadDateKey, endDate);
      const std::array<std::string, 8> labels = {
          tr(STR_STATS_SESSIONS_SHORT),
          tr(STR_STATS_READING_TIME_SHORT),
          tr(STR_STATS_PROGRESS),
          tr(STR_STATS_AVERAGE_SESSION_SHORT),
          tr(STR_STATS_TIME_LEFT_SHORT),
          tr(STR_STATS_PAGES_PER_MINUTE_SHORT),
          dateLabel(tr(STR_STATS_STARTED), stats.firstReadDateKey),
          stats.isCompleted ? tr(STR_STATS_FINISHED) : tr(STR_STATS_ESTIMATED_FINISH)};
      const std::array<std::string, 8> values = {std::to_string(stats.sessionCount),
                                                 formatDuration(stats.totalReadingSeconds),
                                                 formatProgress(progressPercent),
                                                 formatDuration(ReadingStatsMath::averageSessionSeconds(stats)),
                                                 estimate == 0 ? "-" : formatDuration(estimate),
                                                 formatFloat(ReadingStatsMath::pagesPerMinute(stats)),
                                                 stats.firstReadDateKey == 0 ? "-" : formatDays(days),
                                                 formatDate(finishDisplayDate(stats, progressPercent, today))};
      drawMetricCard(renderer, summary, title, labels, values, 3);
      drawBarCard(renderer, timeCard, tr(STR_STATS_TIME_OF_DAY), stats.timeOfDaySeconds, timeLabels);
      drawBarCard(renderer, dayCard, tr(STR_STATS_DAY_OF_WEEK), stats.dayOfWeekSeconds, dayLabels);
    } else {
      const uint32_t streak = ReadingStatsMath::displayedCurrentStreak(globalStats, today);
      const std::array<std::string, 6> labels = {
          tr(STR_STATS_SESSIONS_SHORT),        tr(STR_STATS_TOTAL_TIME_SHORT), tr(STR_STATS_PAGES_PER_MINUTE_SHORT),
          tr(STR_STATS_AVERAGE_SESSION_SHORT), tr(STR_STATS_STREAK_SHORT),     tr(STR_STATS_BOOKS_COMPLETED_SHORT)};
      const std::array<std::string, 6> values = {std::to_string(globalStats.sessionCount),
                                                 formatDuration(globalStats.totalReadingSeconds),
                                                 formatFloat(ReadingStatsMath::pagesPerMinute(globalStats)),
                                                 formatDuration(ReadingStatsMath::averageSessionSeconds(globalStats)),
                                                 streak == 0 ? "-" : formatDays(streak),
                                                 std::to_string(globalStats.completedBooks)};
      drawMetricCard(renderer, summary, tr(STR_STATS_ALL_TIME), labels, values, 3);
      drawBarCard(renderer, timeCard, tr(STR_STATS_TIME_OF_DAY), globalStats.timeOfDaySeconds, timeLabels);
      drawBarCard(renderer, dayCard, tr(STR_STATS_DAY_OF_WEEK), globalStats.dayOfWeekSeconds, dayLabels);
    }
  }

  const auto hints = !rtc ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                          : mappedInput.mapLabels(
                                tr(STR_BACK), page == Page::BOOK && hasEditableBook() ? tr(STR_STATS_EDIT_DATES) : "",
                                tr(STR_STATS_PREVIOUS_PAGE), tr(STR_STATS_NEXT_PAGE));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
