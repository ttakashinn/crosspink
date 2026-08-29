#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
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

enum class DashboardMenuAction { RECENTS, OPDS, READING_STATS, SAVED_ITEMS, FILE_TRANSFER };

DashboardMenuAction dashboardActionAt(int index, const bool hasOpds, const bool hasStats, const bool hasSavedItems) {
  if (index-- == 0) return DashboardMenuAction::RECENTS;
  if (hasOpds && index-- == 0) return DashboardMenuAction::OPDS;
  if (hasStats && index-- == 0) return DashboardMenuAction::READING_STATS;
  if (hasSavedItems && index-- == 0) return DashboardMenuAction::SAVED_ITEMS;
  return DashboardMenuAction::FILE_TRANSFER;
}

const char* savedItemsLabel(const bool hasBookmarks, const bool hasClippings) {
  if (hasBookmarks && hasClippings) return tr(STR_BOOKMARKS_AND_CLIPPINGS);
  return hasClippings ? tr(STR_CLIPPINGS) : tr(STR_BOOKMARKS);
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

}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (hasReadingStats) count++;
  if (hasSavedBookmarks || hasSavedClippings) count++;
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

int HomeActivity::getDashboardMenuItemCount() const {
  return 2 + (hasOpdsServers ? 1 : 0) + (hasReadingStats ? 1 : 0) + (hasSavedBookmarks || hasSavedClippings ? 1 : 0);
}

void HomeActivity::loadSavedItems() {
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

  hasSavedBookmarks = false;
  hasSavedClippings = false;
  if (status == SavedItemsCatalog::LoadStatus::LOADED || status == SavedItemsCatalog::LoadStatus::LOADED_TEMP ||
      status == SavedItemsCatalog::LoadStatus::LOADED_BACKUP) {
    for (const auto& entry : entries) {
      hasSavedBookmarks = hasSavedBookmarks || entry.bookmarkCount > 0;
      hasSavedClippings = hasSavedClippings || entry.clippingCount > 0;
    }
  }
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
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          if (!epub.load(false, true)) {
            // A cache-version upgrade is not proof that the book has no cover.
            // Keep the recorded cover pattern; opening the book will rebuild
            // metadata and a later Home visit can generate this thumbnail.
            LOG_DBG("HOME", "Deferring thumbnail until EPUB metadata cache is rebuilt: %s", book.path.c_str());
            continue;
          }

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  loadReadingStats();
  loadSavedItems();

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE
                      ? 0
                      : base + menuItemToIndex(initialMenuItem, hasOpdsServers, hasReadingStats,
                                               hasSavedBookmarks || hasSavedClippings);

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
    switch (dashboardActionAt(dashboardMenuIndex, hasOpdsServers, hasReadingStats,
                              hasSavedBookmarks || hasSavedClippings)) {
      case DashboardMenuAction::RECENTS:
        onRecentsOpen();
        break;
      case DashboardMenuAction::OPDS:
        onOpdsBrowserOpen();
        break;
      case DashboardMenuAction::READING_STATS:
        onReadingStatsOpen();
        break;
      case DashboardMenuAction::SAVED_ITEMS:
        onSavedItemsOpen();
        break;
      case DashboardMenuAction::FILE_TRANSFER:
        onFileTransferOpen();
        break;
    }
  };

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

    const int panelTop = metrics.homeTopPadding + 80;
    const int rowHeight = GUI.getMenuRowHeight(renderer);
    int row = -1;
    const auto touch = mappedInput.rowTouch(row, panelTop + BaseMetrics::values.verticalSpacing,
                                            BaseMetrics::values.menuRowHeight + BaseMetrics::values.menuSpacing,
                                            menuCount, 20, renderer.getScreenWidth() - 20, rowHeight);
    if (touch != MappedInputManager::RowTouch::None) {
      dashboardMenuIndex = row;
      if (touch == MappedInputManager::RowTouch::Tap)
        activateMenu();
      else
        requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      dashboardMenuOpen = false;
      requestUpdate();
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
    switch (indexToMenuItem(menuIndex, hasOpdsServers, hasReadingStats, hasSavedBookmarks || hasSavedClippings)) {
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
      case HomeMenuItem::SAVED_ITEMS:
        onSavedItemsOpen();
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
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, menuRowHeight + metrics.menuSpacing, renderedMenuCount,
                                              0, INT32_MAX, menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
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
    if (dashboardMenuOpen) {
      std::vector<const char*> menuItems = {tr(STR_MENU_RECENT_BOOKS)};
      if (hasOpdsServers) menuItems.push_back(tr(STR_OPDS_BROWSER));
      if (hasReadingStats) menuItems.push_back(tr(STR_READING_STATS));
      if (hasSavedBookmarks || hasSavedClippings) {
        menuItems.push_back(savedItemsLabel(hasSavedBookmarks, hasSavedClippings));
      }
      menuItems.push_back(tr(STR_FILE_TRANSFER));
      const Rect panel{
          20, metrics.homeTopPadding + 80, pageWidth - 40,
          static_cast<int>(menuItems.size()) * (BaseMetrics::values.menuRowHeight + BaseMetrics::values.menuSpacing) +
              BaseMetrics::values.verticalSpacing * 2};
      renderer.fillRect(panel.x, panel.y, panel.width, panel.height, false);
      renderer.drawRect(panel.x, panel.y, panel.width, panel.height);
      GUI.drawButtonMenu(
          renderer, panel, static_cast<int>(menuItems.size()), dashboardMenuIndex,
          [&menuItems](const int index) { return std::string(menuItems[index]); }, [](int) { return Recent; });
    }

    const auto labels = mappedInput.mapLabels(tr(STR_DASHBOARD_MENU), tr(STR_DASHBOARD_BROWSE), tr(STR_SETTINGS_TITLE),
                                              recentBooks.empty() ? "" : tr(STR_DASHBOARD_READ));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS)};
  std::vector<UIIcon> menuIcons = {Folder, Recent};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }
  if (hasReadingStats) {
    const auto position = menuItems.begin() + 2 + (hasOpdsServers ? 1 : 0);
    menuItems.insert(position, tr(STR_READING_STATS));
    menuIcons.insert(menuIcons.begin() + 2 + (hasOpdsServers ? 1 : 0), Recent);
  }
  if (hasSavedBookmarks || hasSavedClippings) {
    const auto position = menuItems.begin() + 2 + (hasOpdsServers ? 1 : 0) + (hasReadingStats ? 1 : 0);
    menuItems.insert(position, savedItemsLabel(hasSavedBookmarks, hasSavedClippings));
    menuIcons.insert(menuIcons.begin() + 2 + (hasOpdsServers ? 1 : 0) + (hasReadingStats ? 1 : 0), Bookmark);
  }
  menuItems.push_back(tr(STR_FILE_TRANSFER));
  menuIcons.push_back(Transfer);
  menuItems.push_back(tr(STR_SETTINGS_TITLE));
  menuIcons.push_back(Settings);

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

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

  startActivityForResult(
      std::make_unique<BookStatsActivity>(renderer, mappedInput, std::move(title), sourcePath, cachePath,
                                          currentBookStats, globalReadingStats, currentBookProgressPercent),
      [this](const ActivityResult&) {
        dashboardMenuOpen = false;
        loadReadingStats();
        requestUpdate();
      });
}

void HomeActivity::onSavedItemsOpen() {
  startActivityForResult(std::make_unique<SavedItemsActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    dashboardMenuOpen = false;
    loadSavedItems();
    requestUpdate();
  });
}
