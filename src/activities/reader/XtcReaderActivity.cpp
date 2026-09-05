#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/XtcPixelPlanes.h"
#include "util/XtcStatusBarOverlayLayout.h"

bool XtcReaderActivity::loadBook() {
  auto loadedXtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint");
  if (!loadedXtc) {
    LOG_ERR("XTR", "Failed to allocate XTC object");
    return false;
  }
  if (!loadedXtc->load()) {
    LOG_ERR("XTR", "Failed to load XTC");
    return false;
  }
  xtc = std::move(loadedXtc);
  xtc->setupCacheDir();
  loadProgress();
  return true;
}

void XtcReaderActivity::openChapterSelection() {
  std::shared_ptr<Xtc> chapterBook;
  uint32_t pageToSelect = 0;
  {
    // getChapters() may lazily read through the same parser/file handle used
    // by renderPage(). Keep that file access serialized with the render task.
    RenderLock lock(*this);
    if (!xtc || !xtc->hasChapters() || xtc->getChapters().empty()) return;
    chapterBook = xtc;
    pageToSelect = pageState.requestedPage();
  }

  startActivityForResult(
      std::make_unique<XtcReaderChapterSelectionActivity>(renderer, mappedInput, chapterBook, pageToSelect),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          pageState.request(std::get<PageResult>(result.data).page);
          requestUpdate();
        }
      });
}

bool XtcReaderActivity::handleFormatInput() {
  if (!xtc) {
    return false;
  }

  // Enter chapter selection activity on Confirm release or touch menu gesture
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
    openChapterSelection();
    return true;
  }
  return false;
}

void XtcReaderActivity::applyInitialOrientation() { renderer.setOrientation(GfxRenderer::Orientation::Portrait); }

void XtcReaderActivity::renderBook() {
  pageRenderedThisFrame = false;
  if (!xtc) {
    return;
  }

  const uint32_t pageToRender = pageState.requestedPage();
  if (!renderPage(pageToRender)) return;
  pageState.markVisible(pageToRender);
  queueProgressSave(pageToRender);
  pageRenderedThisFrame = true;
}

XtcReaderActivity::StatusBarInfo XtcReaderActivity::getStatusBarInfo(const uint32_t pageToRender) const {
  const auto sb = SETTINGS.statusBarSpec();
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  const int bookPage = static_cast<int>(pageToRender) + 1;
  std::string title = sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE ? xtc->getTitle() : "";

  if (!xtc->hasChapters()) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  const auto& chapters = xtc->getChapters();
  const auto chapterIt =
      std::find_if(chapters.begin(), chapters.end(), [pageToRender](const xtc::ChapterInfo& chapter) {
        return pageToRender >= chapter.startPage && pageToRender <= chapter.endPage;
      });

  if (chapterIt == chapters.end() || chapterIt->endPage < chapterIt->startPage) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = chapterIt->name.empty() ? tr(STR_UNNAMED) : chapterIt->name;
  }

  return StatusBarInfo{static_cast<int>(pageToRender - chapterIt->startPage) + 1,
                       static_cast<int>(chapterIt->endPage - chapterIt->startPage) + 1, std::move(title)};
}

void XtcReaderActivity::renderStatusBarOverlay(GfxRenderer& renderer, const StatusBarOverlayPosition position,
                                               const uint32_t pageToRender) const {
  const auto sb = SETTINGS.statusBarSpec();
  const bool drawBottom = sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM &&
                          position == StatusBarOverlayPosition::Bottom;
  const bool drawTop = sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP &&
                       position == StatusBarOverlayPosition::Top;
  if (!drawBottom && !drawTop) {
    return;
  }

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  const auto layout =
      xtc_status_bar::calculateLayout(renderer.getScreenHeight(), orientedMarginTop, orientedMarginBottom,
                                      statusBarHeight, position == StatusBarOverlayPosition::Top);
  if (!layout.visible()) {
    return;
  }
  renderer.fillRect(0, layout.clearY, renderer.getScreenWidth(), layout.clearHeight, false);

  const int pageCount = static_cast<int>(xtc->getPageCount());
  const int displayPage = static_cast<int>(pageToRender) + 1;
  const float progress = pageCount > 0 ? (static_cast<float>(displayPage) * 100.0f) / pageCount : 0.0f;
  const auto pageInfo = getStatusBarInfo(pageToRender);
  GUI.drawStatusBar(renderer, progress, pageInfo.currentPage, pageInfo.pageCount, pageInfo.title, layout.paddingBottom);
}

bool XtcReaderActivity::renderPage(const uint32_t pageToRender) {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  const size_t pageBufferSize = xtc::pageBitmapSize(bitDepth, pageWidth, pageHeight);

  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    LOG_ERR("XTR", "Failed to allocate page buffer (%lu bytes)", pageBufferSize);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_MEMORY_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return false;
  }

  size_t bytesRead = xtc->loadPage(pageToRender, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    LOG_ERR("XTR", "Failed to load page %lu: bufferSize=%lu bitDepth=%u error=%s", pageToRender, pageBufferSize,
            bitDepth, xtc::errorToString(xtc->getLastError()));
    free(pageBuffer);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return false;
  }

  renderer.clearScreen();

  const auto statusBarMode = SETTINGS.statusBarSpec().xtcMode;
  const bool statusBarAtTop = statusBarMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP;
  const bool statusBarVisible =
      statusBarAtTop || statusBarMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM;
  const auto statusBarPosition = statusBarAtTop ? StatusBarOverlayPosition::Top : StatusBarOverlayPosition::Bottom;

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  const auto statusBarLayout =
      xtc_status_bar::calculateLayout(renderer.getScreenHeight(), orientedMarginTop, orientedMarginBottom,
                                      UITheme::getInstance().getStatusBarHeight(), statusBarAtTop);
  const bool renderStatusBar = statusBarVisible && statusBarLayout.visible();

  if (bitDepth == 2) {
    const size_t planeSize = xtc::xthPlaneSize(pageWidth, pageHeight);
    const uint8_t* plane1 = pageBuffer;
    const uint8_t* plane2 = pageBuffer + planeSize;
    auto visitPixels = [&](auto&& visit) {
      xtc_pixels::forEach2BitPixel(plane1, plane2, pageWidth, pageHeight, std::forward<decltype(visit)>(visit));
    };

    visitPixels([&](const uint16_t x, const uint16_t y, const uint8_t value) {
      if (value >= 1) renderer.drawPixel(x, y, true);
    });
    if (renderStatusBar) renderStatusBarOverlay(renderer, statusBarPosition, pageToRender);

    if (pagesUntilFullRefresh <= 1) {
      // Periodic ghost cleanup: scrub via the normal path, then run the
      // settle flavor of the grayscale base pass (DTM planes are equal after
      // the display sync, so only the gentle reinforcement cells fire).
      // Combined-base panels (Paper Mono) instead defer the base so the gray
      // planes below join it in one waveform.
      if (renderer.combinesGrayscaleBase()) {
        renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
      } else {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        renderer.preconditionGrayscale();
      }
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }

    renderer.clearScreen(0x00);
    visitPixels([&](const uint16_t x, const uint16_t y, const uint8_t value) {
      if (value == 1) renderer.drawPixel(x, y, false);
    });
    // Zero selector bits preserve the already-rendered black/white overlay
    // instead of letting the source XTC grayscale pixels overwrite it.
    if (renderStatusBar) {
      renderer.fillRect(0, statusBarLayout.clearY, renderer.getScreenWidth(), statusBarLayout.clearHeight, true);
    }
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    visitPixels([&](const uint16_t x, const uint16_t y, const uint8_t value) {
      if (value == 1 || value == 2) renderer.drawPixel(x, y, false);
    });
    if (renderStatusBar) {
      renderer.fillRect(0, statusBarLayout.clearY, renderer.getScreenWidth(), statusBarLayout.clearHeight, true);
    }
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();

    renderer.clearScreen();
    visitPixels([&](const uint16_t x, const uint16_t y, const uint8_t value) {
      if (value >= 1) renderer.drawPixel(x, y, true);
    });
    if (renderStatusBar) renderStatusBarOverlay(renderer, statusBarPosition, pageToRender);

    renderer.cleanupGrayscaleWithFrameBuffer();

    free(pageBuffer);

    LOG_DBG("XTR", "Rendered page %lu/%lu (2-bit grayscale)", static_cast<unsigned long>(pageToRender + 1),
            static_cast<unsigned long>(xtc->getPageCount()));
    return true;
  } else {
    xtc_pixels::forEachBlack1BitPixel(pageBuffer, pageWidth, pageHeight,
                                      [&](const uint16_t x, const uint16_t y) { renderer.drawPixel(x, y, true); });
  }

  free(pageBuffer);

  if (renderStatusBar) renderStatusBarOverlay(renderer, statusBarPosition, pageToRender);

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", static_cast<unsigned long>(pageToRender + 1),
          static_cast<unsigned long>(xtc->getPageCount()), bitDepth);
  return true;
}

bool XtcReaderActivity::pageTurn(bool isForward) { return xtc && pageState.turn(isForward, xtc->getPageCount()); }

bool XtcReaderActivity::skipPages(int amount) { return xtc && pageState.skip(amount, xtc->getPageCount()); }

bool XtcReaderActivity::isAtEndOfBook() const { return xtc && pageState.atEnd(xtc->getPageCount()); }

void XtcReaderActivity::onReturnFromEndOfBook() { pageState.returnFromEnd(xtc ? xtc->getPageCount() : 0); }

bool XtcReaderActivity::saveProgress(const uint32_t page) {
  if (!xtc) return false;
  uint8_t data[4];
  data[0] = page & 0xFF;
  data[1] = (page >> 8) & 0xFF;
  data[2] = (page >> 16) & 0xFF;
  data[3] = (page >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(xtc->getCachePath(), data, sizeof(data))) {
    LOG_ERR("XTC", "Failed to save progress: page %lu", static_cast<unsigned long>(page));
    progressSaveDebouncer.markAttemptFailed(millis());
    return false;
  }
  progressSaveDebouncer.seedPersisted(page, xtc->getPageCount(), millis());
  return true;
}

void XtcReaderActivity::queueProgressSave(const uint32_t pageToRender) {
  if (xtc && progressSaveDebouncer.observe(pageToRender, xtc->getPageCount(), millis())) saveProgress(pageToRender);
}

void XtcReaderActivity::flushReaderState() {
  if (xtc && progressSaveDebouncer.hasPending()) saveProgress(progressSaveDebouncer.lastObservedPosition());
}

void XtcReaderActivity::requestProgressSaveIfDue() {
  RenderLock lock(*this);
  if (progressSaveDebouncer.due(millis())) flushReaderState();
}

void XtcReaderActivity::loadProgress() {
  if (!xtc) return;
  uint32_t page = 0;
  HalFile f;
  if (Storage.openFileForRead("XTC", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      page = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8U) |
             (static_cast<uint32_t>(data[2]) << 16U) | (static_cast<uint32_t>(data[3]) << 24U);
      if (xtc->getPageCount() == 0) {
        page = 0;
      } else if (page >= xtc->getPageCount()) {
        page = xtc->getPageCount() - 1;
      }
      LOG_DBG("XTC", "Loaded progress: page %lu/%lu", static_cast<unsigned long>(page + 1),
              static_cast<unsigned long>(xtc->getPageCount()));
    }
  }
  pageState.initialize(page);
  progressSaveDebouncer.seedPersisted(page, xtc->getPageCount(), millis());
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  const uint32_t visiblePage = pageState.visiblePage();
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    const uint32_t clampedPage = (pageCount > 0 && visiblePage >= pageCount) ? pageCount - 1 : visiblePage;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = static_cast<int>(visiblePage) + 1;
  }
  return info;
}
