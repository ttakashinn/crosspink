#pragma once

#include <cstdint>
#include <string>

#include "ReadingStats.h"
#include "activities/Activity.h"

class BookStatsActivity final : public Activity {
 public:
  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, std::string sourcePath,
                    std::string cachePath, const BookReadingStats& stats, const GlobalReadingStats& globalStats,
                    int progressPercent)
      : Activity("BookStats", renderer, mappedInput),
        title(std::move(title)),
        sourcePath(std::move(sourcePath)),
        cachePath(std::move(cachePath)),
        stats(stats),
        globalStats(globalStats),
        progressPercent(progressPercent) {}

  void loop() override;
  void render(RenderLock&&) override;
  void onEnter() override;
  void onExit() override;
  bool handleHomeGesture() override;

  static std::string formatDuration(uint32_t seconds);
  static std::string formatDate(uint32_t dateKey);

 private:
  enum class Page : uint8_t { BOOK, DEVICE, EDIT_DATES };

  std::string title;
  std::string sourcePath;
  std::string cachePath;
  BookReadingStats stats;
  GlobalReadingStats globalStats;
  int progressPercent = 0;
  Page page = Page::BOOK;
  int selectedDateField = 0;
  bool editsChanged = false;
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;

  bool hasEditableBook() const { return !sourcePath.empty() && !cachePath.empty(); }
  void showNextPage();
  void showPreviousPage();
  void adjustSelectedDate(int delta);
  void clearSelectedDate();
  void saveEdits();
};
