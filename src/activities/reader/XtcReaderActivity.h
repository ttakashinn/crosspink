#pragma once

#include <Xtc.h>

#include <memory>
#include <string>

#include "ReaderActivity.h"
#include "ReaderProgressSaveDebouncer.h"
#include "XtcPageState.h"

class XtcReaderActivity final : public ReaderActivity {
  std::shared_ptr<Xtc> xtc;
  XtcPageState pageState;
  ReaderProgressSaveDebouncer progressSaveDebouncer;
  bool pageRenderedThisFrame = false;

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  bool renderPage(uint32_t pageToRender);
  void openChapterSelection();
  void renderStatusBarOverlay(GfxRenderer& renderer, StatusBarOverlayPosition position, uint32_t pageToRender) const;
  StatusBarInfo getStatusBarInfo(uint32_t pageToRender) const;
  bool saveProgress(uint32_t page);
  void queueProgressSave(uint32_t pageToRender);
  void flushReaderState() override;
  void requestProgressSaveIfDue() override;
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return xtc ? xtc->getTitle() : ""; }
  std::string getBookAuthor() const override { return xtc ? xtc->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return xtc ? xtc->getThumbBmpPath() : ""; }
  bool handleFormatInput() override;
  void renderBook() override;
  bool renderedReadingPageThisFrame() const override { return pageRenderedThisFrame; }
  std::pair<int32_t, int32_t> readerTelemetryPosition() const override {
    return {static_cast<int32_t>(pageState.visiblePage()), -1};
  }
  void applyInitialOrientation() override;

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("XtcReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~XtcReaderActivity() override = default;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
