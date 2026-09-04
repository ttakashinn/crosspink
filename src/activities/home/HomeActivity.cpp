#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HomeMenuViewport.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/home/SavedItemsActivity.h"
#include "activities/reader/BookStatsActivity.h"
#include "activities/reader/ReadingStatsStore.h"
#include "clippings/ClippingStore.h"
#include "components/UITheme.h"
#include "components/themes/dashboard/DashboardTheme.h"
#include "fontIds.h"
#include "saved_items/SavedItemsCatalog.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkFile.h"

namespace {

enum class DashboardMenuAction { RECENTS, OPDS, READING_STATS, MY_CLIPPINGS, FILE_TRANSFER };

DashboardMenuAction dashboardActionAt(int index, const bool hasOpds, const bool hasStats) {
  if (index-- == 0) return DashboardMenuAction::RECENTS;
  if (hasOpds && index-- == 0) return DashboardMenuAction::OPDS;
  if (hasStats && index-- == 0) return DashboardMenuAction::READING_STATS;
  if (index-- == 0) return DashboardMenuAction::MY_CLIPPINGS;
  return DashboardMenuAction::FILE_TRANSFER;
}

bool hasAnyBookStats(const BookReadingStats& stats) {
  return stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 || stats.isCompleted ||
         stats.progressPermille != BookReadingStats::UNKNOWN_PROGRESS_PERMILLE || stats.firstReadDateKey != 0 ||
         stats.completedDateKey != 0;
}

bool hasAnyGlobalStats(const GlobalReadingStats& stats) {
  return stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 ||
         stats.completedBooks > 0 || stats.longestReadingStreak > 0;
}

enum class CoverThumbnailState : uint8_t { MISSING, READY, NO_COVER_SENTINEL, INVALID, IO_ERROR };

CoverThumbnailState inspectCoverThumbnail(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("HOME", path, file)) {
    return Storage.exists(path.c_str()) ? CoverThumbnailState::IO_ERROR : CoverThumbnailState::MISSING;
  }

  if (file.fileSize64() == 0) {
    file.close();
    return CoverThumbnailState::NO_COVER_SENTINEL;
  }

  Bitmap bitmap(file);
  const BmpReaderError result = bitmap.parseHeaders();
#if defined(SIMULATOR)
  // The pinned simulator HalFile does not expose the device-only SdFat error
  // code. Header failures still remain INVALID in render-lab builds; device
  // firmware keeps distinguishing transient SD I/O from corrupt thumbnails.
  const uint8_t fileError = 0;
#else
  const uint8_t fileError = file.getError();
#endif
  file.close();
  if (result == BmpReaderError::Ok) return CoverThumbnailState::READY;
  if (fileError != 0) return CoverThumbnailState::IO_ERROR;

  LOG_ERR("HOME", "Invalid cached cover thumbnail %s: %s", path.c_str(), Bitmap::errorToString(result));
  return CoverThumbnailState::INVALID;
}

}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Recents, My Clippings, File transfer, Settings
  if (hasReadingStats) count++;
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

bool HomeActivity::usesDashboardHome() const {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::DASHBOARD;
}

int HomeActivity::getDashboardMenuItemCount() const { return 3 + (hasOpdsServers ? 1 : 0) + (hasReadingStats ? 1 : 0); }

void HomeActivity::ensureSavedItemsCatalog() {
  std::vector<SavedItemsCatalog::Entry> entries;
  auto status = SavedItemsCatalog::load(entries);
  if (status == SavedItemsCatalog::LoadStatus::MISSING) {
    // vns.1 stored bookmark/clipping payloads per book but had no global
    // catalog. Backfill the bounded recent-books set once; books outside it
    // are indexed automatically the next time they are opened.
    for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
      if (RecentBooksStore::isMissing(book) || !FsHelpers::hasEpubExtension(book.path)) continue;
      std::vector<BookmarkEntry> bookmarks;
      BookmarkFile::load(book.path, bookmarks);
      std::vector<ClippingCodec::Record> clippings;
      std::string cachePath;
      if (getBookCachePath(book.path, cachePath)) {
        const auto clippingStatus = ClippingStore::load(book.path, cachePath, clippings);
        if (clippingStatus == ClippingStore::LoadStatus::NEWER_VERSION ||
            clippingStatus == ClippingStore::LoadStatus::IO_ERROR ||
            clippingStatus == ClippingStore::LoadStatus::INVALID) {
          clippings.clear();
        }
      }
      if (!bookmarks.empty() || !clippings.empty()) {
        SavedItemsCatalog::syncBook(book.path, book.title, book.author, bookmarks.size(), clippings.size());
      }
    }
    status = SavedItemsCatalog::load(entries);
    if (status == SavedItemsCatalog::LoadStatus::MISSING &&
        SavedItemsCatalog::save({}) == SavedItemsCatalog::SaveStatus::SAVED) {
      status = SavedItemsCatalog::LoadStatus::LOADED;
    }
  }

  // My Clippings is intentionally always visible. An empty catalog renders a
  // useful empty state instead of hiding the feature until the user discovers
  // clipping from inside a book first.
}

void HomeActivity::loadReadingStats() {
  currentBookStats = {};
  globalReadingStats = {};
  currentBookProgressPercent = -1;
  currentBookChapterTitle.clear();

  const auto globalStatus = ReadingStatsStore::loadGlobal(globalReadingStats);
  if (globalStatus != ReadingStatsStore::LoadStatus::LOADED) globalReadingStats = {};

  if (!recentBooks.empty()) {
    std::string cachePath;
    if (getBookCachePath(recentBooks[0].path, cachePath)) {
      const auto bookStatus = ReadingStatsStore::loadBook(recentBooks[0].path, cachePath, currentBookStats);
      if (bookStatus != ReadingStatsStore::LoadStatus::LOADED) currentBookStats = {};
    }
    if (currentBookStats.isCompleted) {
      currentBookProgressPercent = 100;
    } else if (currentBookStats.progressPermille <= 1000) {
      currentBookProgressPercent = std::clamp(static_cast<int>((currentBookStats.progressPermille + 5U) / 10U), 0, 100);
    }

    // The Dashboard uses the current chapter as its subtitle, falling back to
    // the author. Inspect an existing metadata cache only: Home must never
    // trigger a potentially expensive re-index just to paint this line.
    if (usesDashboardHome() && FsHelpers::hasEpubExtension(recentBooks[0].path)) {
      Epub epub(recentBooks[0].path, "/.crosspoint");
      if (epub.load(false, true)) {
        uint16_t spineIndex = 0;
        uint16_t pageNumber = 0;
        uint16_t pageCount = 0;
        bool hasProgress = false;
        HalFile progressFile;
        if (Storage.openFileForRead("HOME", epub.getCachePath() + "/progress.bin", progressFile)) {
          uint8_t bytes[10] = {};
          const int read = progressFile.read(bytes, sizeof(bytes));
          if (read == 4 || read == 6 || read == 10) {
            spineIndex = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
            pageNumber = static_cast<uint16_t>(bytes[2]) | (static_cast<uint16_t>(bytes[3]) << 8);
            if (read >= 6) pageCount = static_cast<uint16_t>(bytes[4]) | (static_cast<uint16_t>(bytes[5]) << 8);
            hasProgress = spineIndex < epub.getSpineItemsCount() && pageNumber != UINT16_MAX;
          }
          progressFile.close();
        }
        // progress.bin is the durable last reading position and predates the
        // vns.2 statistics store. Prefer it when valid so upgraded installs
        // immediately get an accurate Dashboard instead of an unknown/0%
        // value until the next qualifying statistics session is committed.
        if (!currentBookStats.isCompleted && hasProgress) {
          const float chapterProgress =
              pageCount > 0 ? std::clamp(static_cast<float>(pageNumber) / pageCount, 0.0f, 1.0f) : 0.0f;
          currentBookProgressPercent =
              std::clamp(static_cast<int>(epub.calculateProgress(spineIndex, chapterProgress) * 100.0f + 0.5f), 0, 100);
        }
        const int tocIndex = epub.getTocIndexForSpineIndex(spineIndex);
        if (tocIndex >= 0) currentBookChapterTitle = epub.getTocItem(tocIndex).title;
      }
    }
  }
  hasReadingStats = hasAnyBookStats(currentBookStats) || hasAnyGlobalStats(globalReadingStats);
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  bool redrawCovers = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    const bool isEpub = FsHelpers::hasEpubExtension(book.path);
    const bool isXtc = FsHelpers::hasXtcExtension(book.path);
    if (!isEpub && !isXtc) {
      progress++;
      continue;
    }

    const auto persistCoverPattern = [&book](const std::string& pattern) {
      if (book.coverBmpPath == pattern) return;
      book.coverBmpPath = pattern;
      RECENT_BOOKS.updateBook(book.path, book.title, book.author, pattern);
    };

    const auto processBook = [&](auto& publication, auto&& loadPublication) {
      const std::string expectedPattern = book.coverBmpPath.empty() ? publication.getThumbBmpPath() : book.coverBmpPath;
      const std::string thumbnailPath = UITheme::getCoverThumbPath(expectedPattern, coverHeight);
      CoverThumbnailState state = inspectCoverThumbnail(thumbnailPath);

      if (state == CoverThumbnailState::READY) {
        // Repairs recent.json entries cleared by older firmware after a
        // transient generation failure, without regenerating the image.
        if (book.coverBmpPath.empty()) {
          persistCoverPattern(expectedPattern);
          redrawCovers = true;
        } else if (!coverRendered) {
          // The theme could have hit a transient open failure before this
          // validation pass. Give it one bounded redraw attempt.
          redrawCovers = true;
        }
        return;
      }

      if (state == CoverThumbnailState::NO_COVER_SENTINEL) {
        // EPUB writes a zero-byte sentinel for a book with no supported cover.
        // Keep that terminal result, but do not expose it to the theme as BMP.
        persistCoverPattern("");
        return;
      }

      if (state == CoverThumbnailState::IO_ERROR) {
        LOG_DBG("HOME", "Deferring thumbnail after transient SD read failure: %s", thumbnailPath.c_str());
        redrawCovers = true;
        return;
      }

      if (state == CoverThumbnailState::INVALID && !Storage.remove(thumbnailPath.c_str())) {
        LOG_ERR("HOME", "Cannot remove invalid cover thumbnail: %s", thumbnailPath.c_str());
        redrawCovers = true;
        return;
      }

      // Load only when a thumbnail really has to be rebuilt. EPUB skips CSS;
      // Home must remain a cheap metadata/cache operation on the C3 boards.
      if (!loadPublication()) {
        LOG_DBG("HOME", "Deferring thumbnail until book metadata is available: %s", book.path.c_str());
        return;
      }

      if (!showingLoading) {
        showingLoading = true;
        popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      }
      GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
      const bool generated = publication.generateThumbBmp(coverHeight);
      state = inspectCoverThumbnail(thumbnailPath);

      if (generated && state == CoverThumbnailState::READY) {
        persistCoverPattern(expectedPattern);
        redrawCovers = true;
        return;
      }

      if (state == CoverThumbnailState::NO_COVER_SENTINEL) {
        // This is a terminal "no supported cover" result, not an I/O failure.
        persistCoverPattern("");
        return;
      }

      // Do not erase the durable cover pattern after one OOM/SD/decode
      // failure. A later Home visit can retry, and a valid existing pattern is
      // required for the theme to know that a placeholder is non-terminal.
      LOG_ERR("HOME", "Cover thumbnail generation deferred for %s", book.path.c_str());
      if (state == CoverThumbnailState::INVALID) Storage.remove(thumbnailPath.c_str());
      if (state == CoverThumbnailState::IO_ERROR) redrawCovers = true;
    };

    if (isEpub) {
      Epub epub(book.path, "/.crosspoint");
      processBook(epub, [&epub] { return epub.load(false, true); });
    } else {
      Xtc xtc(book.path, "/.crosspoint");
      processBook(xtc, [&xtc] { return xtc.load(); });
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
  if (redrawCovers) {
    freeCoverBuffer();
    coverRendered = false;
    requestUpdate();
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  loadReadingStats();

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE
                      ? 0
                      : base + menuItemToIndex(initialMenuItem, hasOpdsServers, hasReadingStats);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loopDashboardHome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int menuCount = getDashboardMenuItemCount();

  auto activateMenu = [this] {
    switch (dashboardActionAt(dashboardMenuIndex, hasOpdsServers, hasReadingStats)) {
      case DashboardMenuAction::RECENTS:
        onRecentsOpen();
        break;
      case DashboardMenuAction::OPDS:
        onOpdsBrowserOpen();
        break;
      case DashboardMenuAction::READING_STATS:
        onReadingStatsOpen();
        break;
      case DashboardMenuAction::MY_CLIPPINGS:
        onMyClippingsOpen();
        break;
      case DashboardMenuAction::FILE_TRANSFER:
        onFileTransferOpen();
        break;
    }
  };
  auto closeMenu = [this] {
    dashboardMenuOpen = false;
    // The menu contains a dense black selected row. A half refresh when it is
    // dismissed costs slightly more time than FAST but avoids leaving a dark
    // ghost over the white Dashboard reading surface.
    renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    requestUpdate();
  };

  if (gpio.hasTouch()) {
    int touchedAction = -1;
    for (int i = 0; i < 4; ++i) {
      const Rect button = DashboardTheme::homeActionRectForScreen(renderer, i);
      if (mappedInput.wasTapInRect(button.x, button.y, button.width, button.height)) {
        touchedAction = i;
        break;
      }
    }
    if (touchedAction >= 0) {
      if (dashboardMenuOpen) {
        switch (touchedAction) {
          case 0:
            closeMenu();
            break;
          case 1:
            activateMenu();
            break;
          case 2:
            dashboardMenuIndex = ButtonNavigator::previousIndex(dashboardMenuIndex, menuCount);
            requestUpdate();
            break;
          case 3:
            dashboardMenuIndex = ButtonNavigator::nextIndex(dashboardMenuIndex, menuCount);
            requestUpdate();
            break;
        }
      } else {
        switch (touchedAction) {
          case 0:
            dashboardMenuOpen = true;
            dashboardMenuIndex = 0;
            requestUpdate();
            break;
          case 1:
            onFileBrowserOpen();
            break;
          case 2:
            onSettingsOpen();
            break;
          case 3:
            if (!recentBooks.empty()) onSelectBook(recentBooks[0].path);
            break;
        }
      }
      return;
    }
  }

  if (dashboardMenuOpen) {
    buttonNavigator.onNext([this, menuCount] {
      dashboardMenuIndex = ButtonNavigator::nextIndex(dashboardMenuIndex, menuCount);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, menuCount] {
      dashboardMenuIndex = ButtonNavigator::previousIndex(dashboardMenuIndex, menuCount);
      requestUpdate();
    });
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up) {
      dashboardMenuIndex = ButtonNavigator::nextIndex(dashboardMenuIndex, menuCount);
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      dashboardMenuIndex = ButtonNavigator::previousIndex(dashboardMenuIndex, menuCount);
      requestUpdate();
      return;
    }

    const DashboardMenuLayout layout = DashboardTheme::menuLayoutForScreen(renderer, menuCount);
    int row = -1;
    const auto touch = mappedInput.rowTouch(row, layout.rows.y, layout.rowHeight + layout.rowGap, menuCount,
                                            layout.rows.x, layout.rows.x + layout.rows.width, layout.rowHeight);
    if (touch != MappedInputManager::RowTouch::None) {
      dashboardMenuIndex = row;
      if (touch == MappedInputManager::RowTouch::Tap)
        activateMenu();
      else
        requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeMenu();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activateMenu();
    return;
  }

  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      dashboardMenuOpen = true;
      dashboardMenuIndex = 0;
      requestUpdate();
      return;
    case MappedInputManager::SwipeDir::Right:
      onFileBrowserOpen();
      return;
    case MappedInputManager::SwipeDir::Down:
      onSettingsOpen();
      return;
    case MappedInputManager::SwipeDir::Left:
      if (!recentBooks.empty()) onSelectBook(recentBooks[0].path);
      return;
    case MappedInputManager::SwipeDir::None:
      break;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    dashboardMenuOpen = true;
    dashboardMenuIndex = 0;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onFileBrowserOpen();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    onSettingsOpen();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    if (!recentBooks.empty()) onSelectBook(recentBooks[0].path);
    return;
  }

  if (!recentBooks.empty()) {
    const Rect tile{0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight};
    const Rect cover = DashboardTheme::coverRectForScreen(renderer, tile);
    if (mappedInput.wasTapInRect(cover.x, cover.y, cover.width, cover.height)) onSelectBook(recentBooks[0].path);
  }
}

void HomeActivity::loop() {
  if (usesDashboardHome()) {
    loopDashboardHome();
    return;
  }
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasOpdsServers, hasReadingStats)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::READING_STATS:
        onReadingStatsOpen();
        break;
      case HomeMenuItem::MY_CLIPPINGS:
        onMyClippingsOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
  const int recentCount = std::min(static_cast<int>(recentBooks.size()), coverColumnCount);
  const int coverColumnWidth = (renderer.getScreenWidth() - 2 * metrics.contentSidePadding) / coverColumnCount;
  int touchedBook = -1;
  const auto coverTouch = mappedInput.colTouch(touchedBook, metrics.contentSidePadding, coverColumnWidth, recentCount,
                                               metrics.homeTopPadding,
                                               metrics.homeTopPadding + metrics.homeCoverTileHeight, coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedBook) {
        selectorIndex = touchedBook;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedBook;
      activateSelection();
    }
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
  int menuRow = -1;
  // Row height from the theme, not the metrics table: RoundedRaff draws
  // font-derived rows and the touch grid must match the visuals exactly.
  const int menuRowHeight = GUI.getMenuRowHeight(renderer);
  const int menuTopInset = GUI.getMenuTopInset();
  const int menuBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int menuHeight = std::max(menuRowHeight, menuBottom - menuTop);
  const HomeMenuViewport viewport = calculateHomeMenuViewport(renderedMenuCount, renderedMenuSelection, menuHeight,
                                                              menuRowHeight, metrics.menuSpacing, menuTopInset);
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop + menuTopInset, menuRowHeight + metrics.menuSpacing,
                                              viewport.count, 0, INT32_MAX, menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int logicalMenuIndex = viewport.first + menuRow;
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? logicalMenuIndex : logicalMenuIndex + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool dashboardHome = usesDashboardHome();

  renderer.clearScreen();
  if (dashboardHome && dashboardMenuOpen) {
    const auto& dashboard = static_cast<const DashboardTheme&>(GUI);

    std::array<const char*, 5> menuItems{};
    std::array<UIIcon, 5> menuIcons{};
    int menuItemCount = 0;
    menuItems[menuItemCount] = tr(STR_MENU_RECENT_BOOKS);
    menuIcons[menuItemCount++] = Recent;
    if (hasOpdsServers) {
      menuItems[menuItemCount] = tr(STR_OPDS_BROWSER);
      menuIcons[menuItemCount++] = Library;
    }
    if (hasReadingStats) {
      menuItems[menuItemCount] = tr(STR_READING_STATS);
      menuIcons[menuItemCount++] = Statistics;
    }
    menuItems[menuItemCount] = tr(STR_MY_CLIPPINGS);
    menuIcons[menuItemCount++] = Bookmark;
    menuItems[menuItemCount] = tr(STR_FILE_TRANSFER);
    menuIcons[menuItemCount++] = Transfer;
    dashboard.drawHomeMenu(renderer, menuItems.data(), menuIcons.data(), menuItemCount, dashboardMenuIndex);

    if (gpio.hasTouch()) {
      dashboard.drawHomeTouchActions(renderer, tr(STR_CLOSE), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    } else {
      const auto labels = mappedInput.mapLabels(tr(STR_CLOSE), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
    renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH
                                                                   : HalDisplay::FAST_REFRESH);
    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    }
    return;
  }

  if (dashboardHome) {
    const Rect cover = DashboardTheme::coverRectForScreen(
        renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight});
    coverRectX = cover.x;
    coverRectY = cover.y;
    coverRectW = cover.width;
    coverRectH = cover.height;
  } else {
    coverRectX = 0;
    coverRectY = metrics.homeTopPadding;
    coverRectW = pageWidth;
    coverRectH = metrics.homeCoverTileHeight;
  }
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band (and a
  // centered title, e.g. RoundedRaff's book title) sinks into the tile.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this),
                          hasAnyBookStats(currentBookStats) ? &currentBookStats : nullptr, currentBookProgressPercent,
                          &globalReadingStats, currentBookChapterTitle.c_str());

  if (dashboardHome) {
    if (gpio.hasTouch()) {
      static_cast<const DashboardTheme&>(GUI).drawHomeTouchActions(renderer, tr(STR_DASHBOARD_MENU),
                                                                   tr(STR_DASHBOARD_BROWSE), tr(STR_SETTINGS_TITLE),
                                                                   recentBooks.empty() ? "" : tr(STR_DASHBOARD_READ));
    } else {
      const auto labels =
          mappedInput.mapLabels(tr(STR_DASHBOARD_MENU), tr(STR_DASHBOARD_BROWSE), tr(STR_SETTINGS_TITLE),
                                recentBooks.empty() ? "" : tr(STR_DASHBOARD_READ));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
    renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH
                                                                   : HalDisplay::FAST_REFRESH);
    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    } else if (!recentsLoaded && !recentsLoading) {
      recentsLoading = true;
      loadRecentCovers(metrics.homeCoverHeight);
    }
    return;
  }

  // Fixed upper bound: avoid two vector allocations and repeated insert()
  // shifts on every Home repaint on the constrained C3 heap.
  std::array<const char*, 8> menuItems{};
  std::array<UIIcon, 8> menuIcons{};
  int menuItemCount = 0;
  const auto appendMenuItem = [&menuItems, &menuIcons, &menuItemCount](const char* label, const UIIcon icon) {
    if (menuItemCount >= static_cast<int>(menuItems.size())) return;
    menuItems[menuItemCount] = label;
    menuIcons[menuItemCount] = icon;
    ++menuItemCount;
  };
  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) appendMenuItem(tr(STR_CONTINUE_READING), Book);
  appendMenuItem(tr(STR_BROWSE_FILES), Folder);
  appendMenuItem(tr(STR_MENU_RECENT_BOOKS), Recent);
  if (hasOpdsServers) appendMenuItem(tr(STR_OPDS_BROWSER), Library);
  if (hasReadingStats) appendMenuItem(tr(STR_READING_STATS), Statistics);
  appendMenuItem(tr(STR_MY_CLIPPINGS), Bookmark);
  appendMenuItem(tr(STR_FILE_TRANSFER), Transfer);
  appendMenuItem(tr(STR_SETTINGS_TITLE), Settings);

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int menuBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const Rect menuRect{0, menuTop, pageWidth, std::max(GUI.getMenuRowHeight(renderer), menuBottom - menuTop)};
  const int logicalSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - static_cast<int>(recentBooks.size());
  const HomeMenuViewport viewport =
      calculateHomeMenuViewport(menuItemCount, logicalSelection, menuRect.height, GUI.getMenuRowHeight(renderer),
                                metrics.menuSpacing, GUI.getMenuTopInset());
  GUI.drawButtonMenu(
      renderer, menuRect, viewport.count, viewport.selected,
      [&menuItems, &viewport](const int index) { return std::string(menuItems[viewport.first + index]); },
      [&menuIcons, &viewport](const int index) { return menuIcons[viewport.first + index]; });

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onReadingStatsOpen() {
  std::string title = tr(STR_READING_STATS);
  std::string sourcePath;
  std::string cachePath;
  if (!recentBooks.empty()) {
    const RecentBook& recent = recentBooks[0];
    title = recent.title;
    sourcePath = recent.path;
    getBookCachePath(sourcePath, cachePath);
  }

  auto activity =
      makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInput, std::move(title), sourcePath, cachePath,
                                           currentBookStats, globalReadingStats, currentBookProgressPercent);
  if (!activity) {
    LOG_ERR("HOME", "OOM: reading statistics activity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    dashboardMenuOpen = false;
    loadReadingStats();
    requestUpdate();
  });
}

void HomeActivity::onMyClippingsOpen() {
  // The global catalog was introduced after per-book bookmark and clipping
  // payloads. Migrate it only when this screen is first used instead of
  // scanning storage on every Home entry.
  ensureSavedItemsCatalog();
  auto activity = makeUniqueNoThrow<SavedItemsActivity>(renderer, mappedInput, true);
  if (!activity) {
    LOG_ERR("HOME", "OOM: saved-items activity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    dashboardMenuOpen = false;
    requestUpdate();
  });
}
