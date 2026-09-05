#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iterator>
#include <limits>

#include "../../util/BookmarkFile.h"
#include "BookStatsActivity.h"
#include "BookmarkEntry.h"
#include "ClipSelectionActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryHistoryActivity.h"
#include "DictionarySelectActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderClippingsActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "PerBookReaderSettingsStore.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "ReaderActivity.h"
#include "ReaderFontSizes.h"
#include "ReaderToolbarUi.h"
#include "ReaderUtils.h"
#include "ReaderViewportLayout.h"
#include "ReadingStatsClock.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/settings/TextSettingsActivity.h"
#include "clippings/ClippingPageTools.h"
#include "clippings/ClippingStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "saved_items/SavedItemsCatalog.h"
#include "util/AdaptiveGrayscaleStrip.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkUtil.h"
#include "util/ButtonNavigator.h"
#include "util/DictionaryHistoryStore.h"
#include "util/QrUtils.h"
#include "util/ScreenshotUtil.h"
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
#include "render_lab/RenderLab.h"
#endif

namespace {
// The X4 Pro and X4 Classic carry the X4's panel but sit outside isXteinkDevice()
// (that helper also gates power management). Overlay refresh choices are per-panel:
// this family runs the grayscale anti-aliasing pass, so chrome painted over a
// fresh page needs the HALF ghost-cleanup and closing re-renders the page.
bool xteinkClassPanel() { return gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic(); }

constexpr uint16_t AUTO_TURN_SECONDS[] = {0, 5, 10, 15, 30, 45, 60, 90, 120};
constexpr size_t initialBookmarkCacheCapacity = 16;
constexpr float bookmarkProgressEpsilon = 0.0001f;

int autoTurnOptionForSeconds(const uint16_t seconds) {
  for (size_t i = 0; i < std::size(AUTO_TURN_SECONDS); ++i) {
    if (AUTO_TURN_SECONDS[i] == seconds) return static_cast<int>(i);
  }
  return 0;
}

std::string autoTurnIntervalLabel(const uint16_t seconds) {
  if (seconds == 0) return I18N.get(StrId::STR_STATE_OFF);
  char value[32];
  if (seconds < 60) {
    snprintf(value, sizeof(value), I18N.get(StrId::STR_STATS_SECONDS_VALUE), static_cast<unsigned>(seconds));
  } else if (seconds % 60 == 0) {
    snprintf(value, sizeof(value), I18N.get(StrId::STR_STATS_MINUTES_VALUE), static_cast<unsigned>(seconds / 60));
  } else {
    snprintf(value, sizeof(value), I18N.get(StrId::STR_STATS_MINUTES_SECONDS_VALUE),
             static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
  }
  return value;
}

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

uint32_t progressPositionKey(const int spineIndex, const int page) {
  return (static_cast<uint32_t>(static_cast<uint16_t>(spineIndex)) << 16) |
         static_cast<uint32_t>(static_cast<uint16_t>(page));
}

constexpr char READ_FOLDER[] = "/read";

bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

struct ProgressRange {
  float start;
  float end;
};

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page, const int pageCount,
                             const ProgressRange& pageRange) {
  if (bookmark.computedSpineIndex == spineIndex && bookmark.computedChapterPageCount == pageCount &&
      bookmark.computedChapterProgress == page) {
    return true;
  }

  const float bookmarkProgress = std::clamp(bookmark.percentage, 0.0f, 1.0f);
  return bookmarkProgress + bookmarkProgressEpsilon >= pageRange.start &&
         bookmarkProgress - bookmarkProgressEpsilon <= pageRange.end;
}

std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath,
                                  const std::string& oldCachePath) {
  LOG_INF("ERS", "Moving finished epub: %s -> %s", srcPath.c_str(), dstPath.c_str());
  if (!moveBookWithCache(srcPath, dstPath)) {
    LOG_ERR("ERS", "Failed to move finished book to '/Read' folder");
    return;
  }

  std::string newCachePath;
  getBookCachePath(dstPath, newCachePath);

  if (!ClippingStore::migrate(srcPath, oldCachePath, dstPath, newCachePath)) {
    LOG_ERR("CLIP", "Failed to re-key clipping data after moving the book");
  }
  if (!BookmarkFile::migrate(srcPath, dstPath)) {
    LOG_ERR("BKM", "Failed to move bookmarks with the book");
  }
  if (!ReadingStatsStore::migrateBook(srcPath, oldCachePath, dstPath, newCachePath)) {
    LOG_ERR("RSTAT", "Failed to re-key reading statistics after moving the book");
  }
  if (!SavedItemsCatalog::migrateBook(srcPath, dstPath)) {
    LOG_ERR("SAVED", "Failed to update saved-items path after moving the book");
  }

  RECENT_BOOKS.updatePath(srcPath, dstPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == srcPath) {
    APP_STATE.openEpubPath = dstPath;
    APP_STATE.saveToFile();
  }
}

std::unique_ptr<Page> loadClippingPage(void* context, const uint16_t pageIndex) {
  auto* clippingSection = static_cast<Section*>(context);
  return clippingSection ? clippingSection->loadPage(pageIndex) : nullptr;
}

bool sameClippingAnchor(const ClippingCodec::Record& lhs, const ClippingCodec::Record& rhs) {
  if (lhs.spineIndex != rhs.spineIndex || lhs.text != rhs.text ||
      ClippingCodec::segmentCount(lhs) != ClippingCodec::segmentCount(rhs)) {
    return false;
  }
  for (size_t i = 0; i < ClippingCodec::segmentCount(lhs); ++i) {
    if (ClippingCodec::segmentAt(lhs, i) != ClippingCodec::segmentAt(rhs, i)) return false;
  }
  return true;
}

}  // namespace

EpubReaderActivity::~EpubReaderActivity() {
  ImageBlock::setExtractor(nullptr, nullptr);
  discardOverlayPage();  // free the overlay's page snapshot if one is held

  if (footnoteDepth > 0 && epub && progressSaveDebouncer.hasPending()) {
    const SavedPosition& origin = savedPositions[0];
    saveProgress(origin.spineIndex, origin.pageNumber, 0);
  }

  section.reset();
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string oldCachePath = epub->getCachePath();
    const std::string dstPath = buildReadFolderDestination(srcPath);
    epub.reset();
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath);
  } else {
    epub.reset();
  }
}

void EpubReaderActivity::onExit() {
  if (!DICTIONARY_HISTORY.flush() && DICTIONARY_HISTORY.isWritable()) {
    LOG_ERR("DHIST", "Could not flush dictionary history on reader exit");
  }
  pauseReadingStats(false);
  finalizeReadingStats();
  ReaderActivity::onExit();
}

void EpubReaderActivity::pauseReadingStats(const bool forwardPageTurn) {
  if (!readingTimerActive) return;
  readingSession.recordInterval(static_cast<uint32_t>(millis() - readingPageStartedMs), forwardPageTurn,
                                readingIntervalStartedAt,
                                static_cast<uint32_t>(SETTINGS.getReadingIdleTimeThresholdSeconds()) * 1000U);
  readingTimerActive = false;
}

void EpubReaderActivity::resumeReadingStats() {
  if (SETTINGS.shouldTrackReadingStats() && !readingTimerActive && readingDisplayedSpine >= 0 &&
      readingDisplayedPage >= 0) {
    readingPageStartedMs = millis();
    if (!ReadingStatsClock::currentLocalDateTime(readingIntervalStartedAt)) readingIntervalStartedAt = {};
    readingTimerActive = true;
  }
}

void EpubReaderActivity::noteRenderedPageForStats() {
  if (!section) return;
  if (readingDisplayedSpine != currentSpineIndex || readingDisplayedPage != section->currentPage) {
    // Covers chapter/percent/bookmark jumps that do not use pageTurn(). A
    // normal page turn already paused the timer, so this is a no-op there.
    pauseReadingStats(false);
    readingDisplayedSpine = currentSpineIndex;
    readingDisplayedPage = section->currentPage;
  }
  if (overlay == Overlay::None) resumeReadingStats();
}

BookReadingStats EpubReaderActivity::readingStatsSnapshot() const {
  BookReadingStats snapshot = readingStats;
  GlobalReadingStats ignoredGlobal;
  const uint32_t today = ReadingStatsClock::currentLocalDateKey();
  const ReadingSessionTracker emptySession;
  const auto& session = SETTINGS.shouldTrackReadingStats() ? readingSession : emptySession;
  session.commit(snapshot, ignoredGlobal, completedDuringVisit || isAtEndOfBook(), today);
  const int progress = currentBookProgressPercent();
  snapshot.progressPermille = static_cast<uint16_t>(progress * 10);
  snapshot.estimatedTimeLeftSeconds = estimatedTimeLeftForCurrentPosition(snapshot);
  return snapshot;
}

GlobalReadingStats EpubReaderActivity::globalReadingStatsSnapshot() const {
  GlobalReadingStats snapshot;
  const auto status = ReadingStatsStore::loadGlobal(snapshot);
  if (status != ReadingStatsStore::LoadStatus::LOADED && status != ReadingStatsStore::LoadStatus::MISSING) {
    snapshot = {};
  }
  BookReadingStats ignoredBook = readingStats;
  const ReadingSessionTracker emptySession;
  const auto& session = SETTINGS.shouldTrackReadingStats() ? readingSession : emptySession;
  session.commit(ignoredBook, snapshot, completedDuringVisit || isAtEndOfBook(),
                 ReadingStatsClock::currentLocalDateKey());
  return snapshot;
}

int EpubReaderActivity::currentBookProgressPercent() const {
  if (!epub) return 0;
  if (isAtEndOfBook()) return 100;
  const int page = section ? section->currentPage : nextPageNumber;
  const int total = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  const float chapterProgress = total > 0 ? static_cast<float>(std::max(page, 0)) / total : 0.0f;
  return clampPercent(static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f));
}

uint32_t EpubReaderActivity::estimatedTimeLeftForCurrentPosition(const BookReadingStats& stats) const {
  if (!epub || stats.isCompleted || stats.avgSecondsPerForwardPage == 0 || currentSpineIndex < 0 ||
      currentSpineIndex >= epub->getSpineItemsCount()) {
    return 0;
  }

  const int page = section ? section->currentPage : nextPageNumber;
  const int pageCount = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  if (pageCount <= 0) return 0;

  const uint64_t previousBytes = currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  const uint64_t currentBytes = epub->getCumulativeSpineItemSize(currentSpineIndex);
  const uint64_t bookBytes = epub->getBookSize();
  if (currentBytes <= previousBytes || bookBytes < currentBytes) return 0;

  const uint64_t chapterBytes = currentBytes - previousBytes;
  const uint64_t completedChapterPages = static_cast<uint64_t>(std::clamp(page, 0, pageCount));
  const uint64_t positionBytes = previousBytes + chapterBytes * completedChapterPages / pageCount;
  const uint64_t remainingBytes = bookBytes > positionBytes ? bookBytes - positionBytes : 0;
  // Estimate page count from the current chapter's actual pagination density,
  // then apply only qualified forward-page dwell samples.
  const uint64_t remainingPages64 =
      (remainingBytes * static_cast<uint64_t>(pageCount) + chapterBytes - 1U) / chapterBytes;
  const uint32_t remainingPages = static_cast<uint32_t>(std::min<uint64_t>(remainingPages64, UINT32_MAX));
  return ReadingStatsMath::paceEstimatedSecondsLeft(stats, remainingPages);
}

void EpubReaderActivity::finalizeReadingStats() {
  if (readingStatsFinalized || !epub) return;
  readingStatsFinalized = true;

  // Per-book data is the durable source of truth for whether this visit and
  // completion have already been counted. If that record is newer than this
  // firmware or currently unreadable, updating the global counters alone
  // would count the same visit again on every exit.
  if (!readingStatsWritable) return;

  GlobalReadingStats globalStats;
  const auto globalLoad = ReadingStatsStore::loadGlobal(globalStats);
  const bool globalWritable = globalLoad != ReadingStatsStore::LoadStatus::NEWER_VERSION &&
                              globalLoad != ReadingStatsStore::LoadStatus::IO_ERROR;
  if (globalLoad == ReadingStatsStore::LoadStatus::INVALID) {
    LOG_ERR("RSTAT", "Replacing invalid global reading statistics");
    globalStats = {};
  }

  // When a future per-book format is present, its completion bit is unknown.
  // Do not count the same book as globally completed on every visit while this
  // firmware deliberately refuses to overwrite that newer record.
  const bool completedNow = completedDuringVisit || isAtEndOfBook();
  const ReadingSessionTracker emptySession;
  const auto& session = SETTINGS.shouldTrackReadingStats() ? readingSession : emptySession;
  const bool sessionChanged =
      session.commit(readingStats, globalStats, completedNow, ReadingStatsClock::currentLocalDateKey());
  const uint16_t progressPermille = static_cast<uint16_t>(currentBookProgressPercent() * 10);
  const uint32_t estimatedTimeLeft = estimatedTimeLeftForCurrentPosition(readingStats);
  const bool progressChanged =
      readingStats.progressPermille != progressPermille || readingStats.estimatedTimeLeftSeconds != estimatedTimeLeft;
  readingStats.progressPermille = progressPermille;
  readingStats.estimatedTimeLeftSeconds = estimatedTimeLeft;
  if (!sessionChanged && !progressChanged) return;
  if (ReadingStatsStore::saveBook(epub->getPath(), epub->getCachePath(), readingStats) !=
      ReadingStatsStore::SaveStatus::SAVED) {
    LOG_ERR("RSTAT", "Could not save book reading statistics");
    return;
  }
  if (sessionChanged && globalWritable &&
      ReadingStatsStore::saveGlobal(globalStats) != ReadingStatsStore::SaveStatus::SAVED) {
    LOG_ERR("RSTAT", "Could not save global reading statistics");
  }
}

bool EpubReaderActivity::prepareReaderSettings() {
  globalReaderSettings = captureReaderSettings(false);
  bookReaderSettings = globalReaderSettings;
  bookReaderSettings.hasOverrides = false;
  perBookSettingsWritable = true;

  auto pathProbe = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
  if (!pathProbe) return false;
  const auto status = PerBookReaderSettingsStore::load(pathProbe->getCachePath(), bookReaderSettings);
  if (status == PerBookReaderSettingsStore::LoadStatus::NEWER_VERSION) {
    perBookSettingsWritable = false;
    LOG_ERR("PBRS", "Per-book settings use a newer format; keeping global defaults");
  } else if (status == PerBookReaderSettingsStore::LoadStatus::IO_ERROR) {
    perBookSettingsWritable = false;
    LOG_ERR("PBRS", "Could not inspect per-book settings; writes disabled for this session");
  } else if (status == PerBookReaderSettingsStore::LoadStatus::INVALID) {
    LOG_ERR("PBRS", "Ignoring invalid per-book settings");
  }
  bookReaderSettings = mergeReaderSettings(globalReaderSettings, bookReaderSettings);
  SETTINGS.beginReaderPersistenceOverlay();
  if (bookReaderSettings.hasOverrides && perBookSettingsWritable) applyReaderSettings(bookReaderSettings);
  activeRenderMode = static_cast<EpubRenderMode>(bookReaderSettings.preferredRenderMode);
  pendingWorkingFallback = UINT8_MAX;
  preferredRenderTrialActive = false;
  if (bookReaderSettings.lastWorkingFallback != UINT8_MAX &&
      bookReaderSettings.lastWorkingFallback > bookReaderSettings.preferredRenderMode &&
      bookReaderSettings.fallbackRenderSignature == readerSettingsRenderSignature(bookReaderSettings)) {
    activeRenderMode = static_cast<EpubRenderMode>(bookReaderSettings.lastWorkingFallback);
  }
  autoTurnOption = autoTurnOptionForSeconds(bookReaderSettings.autoPageTurnSeconds);
  automaticPageTurnActive = autoTurnOption > 0;
  pageTurnDuration =
      automaticPageTurnActive ? static_cast<unsigned long>(bookReaderSettings.autoPageTurnSeconds) * 1000UL : 0UL;
  lastPageTurnTime = millis();
  readerSettingsPrepared = true;
  return true;
}

void EpubReaderActivity::restoreReaderSettings() {
  if (!readerSettingsPrepared) return;
  applyReaderSettings(globalReaderSettings);
  SETTINGS.endReaderPersistenceOverlay();
  pendingWorkingFallback = UINT8_MAX;
  preferredRenderTrialActive = false;
  readerSettingsPrepared = false;
}

void EpubReaderActivity::saveBookReaderSettings() {
  if (!epub || !perBookSettingsWritable) return;
  const uint8_t wordSpacing = bookReaderSettings.wordSpacing;
  const uint8_t repairParagraphIndent = bookReaderSettings.repairParagraphIndent;
  const uint16_t autoPageTurnSeconds = bookReaderSettings.autoPageTurnSeconds;
  const uint8_t preferredRenderMode = bookReaderSettings.preferredRenderMode;
  const uint8_t lastWorkingFallback = bookReaderSettings.lastWorkingFallback;
  const uint32_t fallbackRenderSignature = bookReaderSettings.fallbackRenderSignature;
  const bool hasDictionaryOverride = bookReaderSettings.hasDictionaryOverride;
  const auto dictionaryName = bookReaderSettings.dictionaryName;
  bookReaderSettings = captureReaderSettings(false);
  bookReaderSettings.overrideMask = readerSettingsOverrideMask(bookReaderSettings, globalReaderSettings);
  bookReaderSettings.hasOverrides = bookReaderSettings.overrideMask != 0;
  bookReaderSettings.wordSpacing = wordSpacing;
  bookReaderSettings.repairParagraphIndent = repairParagraphIndent;
  bookReaderSettings.autoPageTurnSeconds = autoPageTurnSeconds;
  bookReaderSettings.preferredRenderMode = preferredRenderMode;
  bookReaderSettings.lastWorkingFallback = lastWorkingFallback;
  bookReaderSettings.fallbackRenderSignature = fallbackRenderSignature;
  bookReaderSettings.hasDictionaryOverride = hasDictionaryOverride;
  bookReaderSettings.dictionaryName = dictionaryName;
  if (PerBookReaderSettingsStore::save(epub->getCachePath(), bookReaderSettings) !=
      PerBookReaderSettingsStore::SaveStatus::SAVED) {
    LOG_ERR("PBRS", "Failed to save per-book reader settings");
    pendingBookSettingsSaveError = true;
  }
}

void EpubReaderActivity::saveBookDictionarySettings() {
  if (!epub || !perBookSettingsWritable) return;
  if (PerBookReaderSettingsStore::save(epub->getCachePath(), bookReaderSettings) !=
      PerBookReaderSettingsStore::SaveStatus::SAVED) {
    LOG_ERR("PBRS", "Failed to save per-book dictionary setting");
    pendingBookSettingsSaveError = true;
  }
}

std::string EpubReaderActivity::activeDictionaryName() const {
  if (!temporaryDictionaryName.empty()) return temporaryDictionaryName;
  if (bookReaderSettings.hasDictionaryOverride) return bookReaderSettings.dictionaryName.data();
  return SETTINGS.dictionaryName;
}

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
void EpubReaderActivity::onEnter() {
  ReaderActivity::onEnter();
  if (render_lab::enabled() && render_lab::targetHref()[0] != '\0') {
    navigateToHref(render_lab::targetHref());
  }
}
#endif

bool EpubReaderActivity::loadBook() {
  auto loadedEpub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
  if (!loadedEpub) {
    LOG_ERR("ERS", "Failed to allocate EPUB object");
    return false;
  }

  const bool uncached = !Storage.exists((loadedEpub->getCachePath() + "/book.bin").c_str());
  if (uncached) {
    disableFastInitialRefresh();
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  // A book.bin left by an older firmware still exists on disk, but load()
  // rejects its version and rebuilds it. Treat that path like a fresh book as
  // well: on the C3 the EPUB inflater needs the lent framebuffer scratch even
  // though the stale filename made `uncached` false. vns.2 only lent it for a
  // physically missing cache, which made first-open upgrades fail on X3 while
  // the roomier X4 completed the same rebuild.
  const auto tryLoad = [this](Epub& candidate, const bool buildIfMissing) {
    GfxRenderer::FrameBufferLoan loan(renderer);
    return candidate.load(buildIfMissing, SETTINGS.embeddedStyle == 0);
  };

  bool loaded = tryLoad(*loadedEpub, true);
  if (!loaded && Storage.exists((loadedEpub->getCachePath() + "/book.bin").c_str())) {
    // A failed final reload can still leave a fully published book.bin. Drop
    // every parser/file handle and try that durable generation once before
    // reporting an invalid book. The retry is cache-only: malformed EPUBs and
    // stale caches are not indexed twice.
    LOG_ERR("ERS", "Initial EPUB load failed; retrying the published metadata cache once");
    loadedEpub.reset();
    loadedEpub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
    if (loadedEpub) loaded = tryLoad(*loadedEpub, false);
  }
  if (!loaded) {
    LOG_ERR("ERS", "Failed to load EPUB");
    return false;
  }
  epub = std::move(loadedEpub);
  vnsContentSignature = 2166136261U;
  vnsContentSignature = vns_reference::appendSignature(vnsContentSignature, epub->getTitle());
  vnsContentSignature = vns_reference::appendSignature(vnsContentSignature, epub->getAuthor());
  vnsContentSignature = vns_reference::appendSignature(vnsContentSignature, static_cast<uint32_t>(epub->getBookSize()));
  for (int i = 0; i < epub->getSpineItemsCount(); ++i) {
    const auto spine = epub->getSpineItem(i);
    vnsContentSignature = vns_reference::appendSignature(vnsContentSignature, spine.href);
    vnsContentSignature =
        vns_reference::appendSignature(vnsContentSignature, static_cast<uint32_t>(epub->getCumulativeSpineItemSize(i)));
  }

  const auto statsLoad = ReadingStatsStore::loadBook(epub->getPath(), epub->getCachePath(), readingStats);
  readingStatsWritable =
      statsLoad != ReadingStatsStore::LoadStatus::NEWER_VERSION && statsLoad != ReadingStatsStore::LoadStatus::IO_ERROR;
  if (statsLoad == ReadingStatsStore::LoadStatus::INVALID) {
    LOG_ERR("RSTAT", "Ignoring invalid per-book reading statistics");
    readingStats = {};
  }

  ImageBlock::clearSessionRenderFailures();
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

  epub->setupCacheDir();

  HalFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[10];
    int dataSize = f.read(data, sizeof(data));
    if (dataSize == 4 || dataSize == 6 || dataSize == 10) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
      cachedChapterPageCountEstimated = cachedChapterTotalPageCount > 0;
    } else if (dataSize == 10) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
      cachedChapterPageCountEstimated = cachedChapterTotalPageCount > 0;
      cachedVisibleTextOffset = static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
                                (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
    }
  }

  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      cachedVisibleTextOffset.reset();
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  progressSaveDebouncer.seedPersisted(progressPositionKey(currentSpineIndex, nextPageNumber),
                                      static_cast<uint32_t>(std::max(cachedChapterTotalPageCount, 0)), millis());

  if (initialPosition) {
    const ProgressChangeResult position = std::move(*initialPosition);
    initialPosition.reset();
    if (position.hasVisibleTextOffset && position.spineIndex >= 0 && position.spineIndex < epub->getSpineItemsCount()) {
      currentSpineIndex = position.spineIndex;
      cachedSpineIndex = position.spineIndex;
      nextPageNumber = std::max(0, position.page);
      cachedVisibleTextOffset = position.visibleTextOffset;
    } else if (position.hasSavedProgress) {
      const CrossPointPosition mapped = ProgressMapper::toCrossPoint(
          epub, {position.xpath, position.percentage}, renderer, position.spineIndex, position.totalPages);
      currentSpineIndex = mapped.spineIndex;
      cachedSpineIndex = mapped.spineIndex;
      nextPageNumber = std::max(0, mapped.pageNumber);
      cachedVisibleTextOffset.reset();
    } else if (position.spineIndex >= 0 && position.spineIndex < epub->getSpineItemsCount()) {
      currentSpineIndex = position.spineIndex;
      cachedSpineIndex = position.spineIndex;
      nextPageNumber = std::max(0, position.page);
      cachedVisibleTextOffset.reset();
    }
  }

  loadCachedBookmarks();
  const auto clippingStatus = ClippingStore::load(epub->getPath(), epub->getCachePath(), cachedClippings);
  clippingsWritable = clippingStatus != ClippingStore::LoadStatus::NEWER_VERSION &&
                      clippingStatus != ClippingStore::LoadStatus::IO_ERROR;
  if (clippingStatus == ClippingStore::LoadStatus::INVALID) {
    LOG_ERR("CLIP", "Ignoring invalid clipping file; a verified replacement may be written");
  }
  const bool clippingCatalogSafe = clippingStatus != ClippingStore::LoadStatus::NEWER_VERSION &&
                                   clippingStatus != ClippingStore::LoadStatus::IO_ERROR &&
                                   clippingStatus != ClippingStore::LoadStatus::INVALID;
  const bool catalogSynced = clippingCatalogSafe
                                 ? SavedItemsCatalog::syncBook(epub->getPath(), epub->getTitle(), epub->getAuthor(),
                                                               cachedBookmarks.size(), cachedClippings.size())
                                 : SavedItemsCatalog::updateBookmarks(epub->getPath(), epub->getTitle(),
                                                                      epub->getAuthor(), cachedBookmarks.size());
  if (!catalogSynced) {
    LOG_ERR("SAVED", "Could not synchronize saved-items catalog");
  }
  return true;
}

void EpubReaderActivity::openReaderMenu() {
  pauseReadingStats(false);
  clearPendingManualTurns();
  if (usesToolbarMenu()) {
    // Reached from a child activity's result handler (footnotes, bookmarks,
    // go-to-percent... cancelled back to the menu), so the framebuffer holds
    // that screen, not the page: re-render the page and let renderBook() put
    // the toolbar on top. The in-reader fast path is openOverlay().
    overlay = Overlay::Toolbar;
    focusedTool = 0;
    panelHoldJumped = false;
    panelCursorShown = !mappedInput.hasTouch();
    if (!toolbarUi) toolbarUi = std::make_unique<ReaderToolbarUi>(renderer);
    toolbarUi->begin();
    discardOverlayPage();
    requestUpdate();
    return;
  }
  const reader_menu::ProgressPosition menuPosition = reader_menu::resolveProgressPosition(
      section != nullptr, section ? section->currentPage : 0, section ? section->estimatedTotalPages() : 0,
      section && (section->isBuilding() || section->isPartial()), currentSpineIndex, cachedSpineIndex, nextPageNumber,
      cachedChapterTotalPageCount, cachedChapterPageCountEstimated);
  float bookProgress = 0.0f;
  if (epub->getBookSize() > 0 && menuPosition.pageCount > 0) {
    bookProgress = epub->calculateProgress(currentSpineIndex, menuPosition.visiblePageProgress()) * 100.0f;
  }
  const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(
          renderer, mappedInput, epub->getTitle(), menuPosition.displayPage(), menuPosition.pageCount,
          menuPosition.pageCountEstimated, bookProgressPercent, SETTINGS.orientation, !currentPageFootnotes.empty(),
          !cachedBookmarks.empty(), bookReaderSettings.autoPageTurnSeconds, bookReaderSettings.wordSpacing,
          bookReaderSettings.repairParagraphIndent != 0, bookReaderSettings.preferredRenderMode,
          static_cast<uint8_t>(activeRenderMode)),
      [this](const ActivityResult& result) {
        const auto& menu = std::get<MenuResult>(result.data);
        const auto action = static_cast<EpubReaderMenuActivity::MenuAction>(menu.action);
        if (SETTINGS.orientation != menu.orientation) {
          applyOrientation(menu.orientation);
        }
        toggleAutoPageTurn(menu.autoPageTurnSeconds);
        applyExtendedReaderSettings(menu.wordSpacing, menu.repairParagraphIndent != 0, menu.renderMode,
                                    action == EpubReaderMenuActivity::MenuAction::TRY_FULL_RENDER_QUALITY);
        if (result.isCancelled) {
          resumeReadingStats();
        } else {
          onReaderMenuConfirm(action);
        }
      });
}

bool EpubReaderActivity::buildTickHeapGate() {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxBlock = ESP.getMaxAllocHeap();
  buildHeapPaused = !epubLayoutHeapSufficient(activeRenderMode, freeHeap, maxBlock);
  return !buildHeapPaused;
}

ReaderRenderSpec EpubReaderActivity::activeReaderRenderSpec(const uint16_t viewportWidth,
                                                            const uint16_t viewportHeight) const {
  ReaderRenderSpec spec =
      applyEpubRenderMode(SETTINGS.readerRenderSpec(viewportWidth, viewportHeight), activeRenderMode);
  spec.wordSpacing = bookReaderSettings.wordSpacing;
  spec.repairParagraphIndent = bookReaderSettings.repairParagraphIndent != 0;
  return spec;
}

bool EpubReaderActivity::recoverSectionBuildFailure() {
  if (!section) return false;
  const SectionBuildFailure failure = section->lastBuildFailure();

  if (section->currentPage >= 0 && section->currentPage < section->pageCount) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->estimatedTotalPages();
    cachedChapterPageCountEstimated = section->isBuilding() || section->isPartial();
    nextPageNumber = section->currentPage;
  } else if (currentPageVisibleOffset.has_value()) {
    // A parser OOM abandons the in-progress section before this retry helper
    // runs. Keep the last rendered content offset so re-pagination in Safe
    // mode returns to the same text instead of falling back to page 0.
    cachedVisibleTextOffset = currentPageVisibleOffset;
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = std::max(lastSavedPageCount, 0);
    cachedChapterPageCountEstimated = cachedChapterTotalPageCount > 0;
    nextPageNumber = std::max(section->currentPage, 0);
  }

  return recoverSectionFailure(failure);
}

bool EpubReaderActivity::recoverSectionFailure(const SectionBuildFailure failure) {
  if (preferredRenderTrialActive) {
    section.reset();
    activeRenderMode = preferredRenderTrialRollbackMode;
    preferredRenderTrialActive = false;
    pendingWorkingFallback = UINT8_MAX;
    partialRebuildStartFailed = false;
    partialRebuildPausedForLowMemory = false;
    buildHeapPaused = false;
    LOG_ERR("ERS", "Preferred render trial failed (%u); restoring %s mode", static_cast<unsigned>(failure),
            epubRenderModeName(activeRenderMode));
    requestUpdate();
    return true;
  }

  const EpubBuildRecovery recovery = epubBuildRecoveryFor(failure, activeRenderMode, bootWasLowMemoryRestart());
  if (recovery == EpubBuildRecovery::None) return false;

  if (recovery == EpubBuildRecovery::RestartReaderOnce) {
    flushReaderState();
    section.reset();
    partialRebuildStartFailed = false;
    partialRebuildPausedForLowMemory = false;
    buildHeapPaused = false;
    LOG_ERR("ERS", "Safe section build ran out of memory; restarting reader once to defragment heap");
    silentRestartToReaderForLowMemory();
    return true;
  }

  section.reset();
  activeRenderMode = nextLighterEpubRenderMode(activeRenderMode);
  pendingWorkingFallback = static_cast<uint8_t>(activeRenderMode);
  partialRebuildStartFailed = false;
  partialRebuildPausedForLowMemory = false;
  buildHeapPaused = false;
  LOG_ERR("ERS", "Section build ran out of memory; retrying in %s mode", epubRenderModeName(activeRenderMode));
  requestUpdate();
  return true;
}

void EpubReaderActivity::showSectionBuildError(const SectionBuildFailure failure) {
  StrId message = StrId::STR_INDEX_FAILED;
  if (failure == SectionBuildFailure::LowMemory) {
    message = StrId::STR_MEMORY_ERROR;
  } else if (failure == SectionBuildFailure::Io) {
    message = StrId::STR_FILE_OPEN_FAILED;
  }
  renderer.clearScreen();
  GUI.drawPopup(renderer, I18N.get(message));
  automaticPageTurnActive = false;
}

void EpubReaderActivity::rememberRenderedFallback() {
  if (preferredRenderTrialActive) {
    preferredRenderTrialActive = false;
    bookReaderSettings.lastWorkingFallback = UINT8_MAX;
    bookReaderSettings.fallbackRenderSignature = 0;
    pendingWorkingFallback = UINT8_MAX;
    saveBookReaderSettings();
    return;
  }
  if (pendingWorkingFallback == UINT8_MAX || pendingWorkingFallback != static_cast<uint8_t>(activeRenderMode) ||
      !shouldRememberEpubFallback(bookReaderSettings.preferredRenderMode, activeRenderMode, true)) {
    return;
  }
  bookReaderSettings.lastWorkingFallback = pendingWorkingFallback;
  bookReaderSettings.fallbackRenderSignature = readerSettingsRenderSignature(bookReaderSettings);
  pendingWorkingFallback = UINT8_MAX;
  saveBookReaderSettings();
}

void EpubReaderActivity::showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (!buildPopupPending || !renderer.hasFrameBuffer()) return;
  GUI.drawPopup(renderer, tr(STR_INDEXING));
  pagesUntilFullRefresh = 1;
  buildPopupPending = false;
}

void EpubReaderActivity::openDictionaryWordSelect() {
  const std::string dictionaryName = activeDictionaryName();
  if (dictionaryName.empty()) {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  if (!section) return;
  auto page = section->loadPage(section->currentPage);
  if (!page) return;
  pauseReadingStats(false);

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;

  startActivityForResult(
      std::make_unique<DictionaryWordSelectActivity>(renderer, mappedInput, std::move(page), orientedMarginLeft,
                                                     orientedMarginTop, dictionaryName),
      [this](const ActivityResult&) {
        resumeReadingStats();
        requestUpdate();
      });
}

void EpubReaderActivity::openClipSelection() {
  if (!clippingsWritable) {
    clippingMessage = StrId::STR_HIGHLIGHT_SAVE_FAILED;
    showClippingMessage = true;
    clippingMessageTime = millis();
    requestUpdate();
    return;
  }
  if (!section) return;
  auto page = section->loadPage(section->currentPage);
  if (!page) return;
  pauseReadingStats(false);
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  top += SETTINGS.screenMargin;
  left += SETTINGS.screenMargin;
  const uint16_t pageIndex = static_cast<uint16_t>(section->currentPage);
  const uint32_t pageVisibleOffset = page->visibleTextOffset;
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId, SETTINGS.getReaderLineCompression());
  const ClipSelectionActivity::PageProvider pageProvider{section.get(), &loadClippingPage, section->pageCount};
  startActivityForResult(
      std::make_unique<ClipSelectionActivity>(renderer, mappedInput, std::move(page), left, top, cachedClippings,
                                              static_cast<uint16_t>(currentSpineIndex), pageIndex, pageVisibleOffset,
                                              pageProvider, fontId, lineHeight),
      [this](const ActivityResult& result) {
        if (!result.isCancelled && std::holds_alternative<ClippingSelectionResult>(result.data)) {
          toggleClipping(std::get<ClippingSelectionResult>(result.data));
        }
        requestUpdate();
      });
}

void EpubReaderActivity::toggleClipping(const ClippingSelectionResult& selection) {
  static_assert(ClippingSelectionResult::MAX_SEGMENTS == ClippingCodec::MAX_SEGMENTS_PER_CLIPPING);
  if (!epub || !section || selection.text.empty() || selection.segmentCount == 0 ||
      selection.segmentCount > ClippingCodec::MAX_SEGMENTS_PER_CLIPPING) {
    return;
  }
  ClippingCodec::Record candidate;
  candidate.spineIndex = selection.spineIndex;
  candidate.text = selection.text;
  const auto& first = selection.segments[0];
  candidate.pageHint = first.pageHint;
  candidate.pageVisibleOffset = first.pageVisibleOffset;
  candidate.startWordIndex = first.startWordIndex;
  candidate.endWordIndex = first.endWordIndex;
  if (selection.segmentCount > 1) {
    candidate.segmentCount = selection.segmentCount;
    for (size_t i = 0; i < selection.segmentCount; ++i) {
      const auto& source = selection.segments[i];
      candidate.segments[i] = {source.pageHint,     source.pageVisibleOffset, source.startWordIndex,
                               source.endWordIndex, source.textOffset,        source.textLength};
    }
  }
  if (!ClippingCodec::validRecord(candidate, false)) {
    clippingMessage = StrId::STR_HIGHLIGHT_SAVE_FAILED;
    showClippingMessage = true;
    clippingMessageTime = millis();
    return;
  }
  auto duplicate = selection.clippingId == 0
                       ? cachedClippings.end()
                       : std::find_if(cachedClippings.begin(), cachedClippings.end(),
                                      [&](const auto& clipping) { return clipping.id == selection.clippingId; });
  if (duplicate == cachedClippings.end()) {
    duplicate = std::find_if(cachedClippings.begin(), cachedClippings.end(),
                             [&](const auto& clipping) { return sameClippingAnchor(clipping, candidate); });
  }
  const size_t duplicateIndex = static_cast<size_t>(duplicate - cachedClippings.begin());
  std::optional<ClippingCodec::Record> removed;
  if (duplicate != cachedClippings.end()) {
    removed.emplace(std::move(*duplicate));
    cachedClippings.erase(duplicate);
    clippingMessage = StrId::STR_HIGHLIGHT_REMOVED;
  } else if (cachedClippings.size() >= ClippingCodec::MAX_CLIPPINGS_PER_BOOK) {
    clippingMessage = StrId::STR_HIGHLIGHTS_FULL;
    showClippingMessage = true;
    clippingMessageTime = millis();
    return;
  } else {
    candidate.id = ClippingCodec::makeStableId(candidate, cachedClippings);
    cachedClippings.push_back(std::move(candidate));
    clippingMessage = StrId::STR_HIGHLIGHT_SAVED;
  }
  const auto saveStatus = ClippingStore::save(epub->getPath(), epub->getCachePath(), cachedClippings);
  if (saveStatus != ClippingStore::SaveStatus::SAVED) {
    clippingMessage = StrId::STR_HIGHLIGHT_SAVE_FAILED;
    // Restore the in-memory view without reading and decoding a second full
    // clipping set. The transactional store keeps a canonical or backup
    // candidate recoverable if publication was interrupted.
    if (removed) {
      cachedClippings.insert(cachedClippings.begin() + static_cast<std::ptrdiff_t>(duplicateIndex),
                             std::move(*removed));
    } else {
      cachedClippings.pop_back();
    }
    clippingsWritable = false;
  } else if (!SavedItemsCatalog::updateClippings(epub->getPath(), epub->getTitle(), epub->getAuthor(),
                                                 cachedClippings.size())) {
    LOG_ERR("SAVED", "Could not update clipping count in saved-items catalog");
  }
  showClippingMessage = true;
  clippingMessageTime = millis();
}

void EpubReaderActivity::loop() {
  if (!epub) {
    finish();
    return;
  }

  requestProgressSaveIfDue();

  // Someone else turned the screen while this reader was stacked (the control
  // center's orientation tile). Reflow before the next render, or the page
  // would be drawn with a layout built for the previous frame size.
  if (appliedOrientation != SETTINGS.orientation) {
    applyOrientation(SETTINGS.orientation);
    requestUpdate();
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  const bool userInputPending = mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() || touch.prev ||
                                touch.next || mappedInput.wasScreenTouchReleased();
  if (userInputPending) noteReaderInput(static_cast<uint32_t>(millis()));

  const int renderFontId = SETTINGS.getReaderFontId();
  // Only SD-font mini caches survive the PrewarmScope cleanup, so doing this
  // for built-in fonts was pure work with no next-page benefit. It also ran
  // before input dispatch and could make a slow/problematic SD card look like
  // a frozen reader. Match CrossInk's useful path but keep VNS's stricter heap
  // gates and never start it when input, an overlay, or auto-turn is pending.
  if (!userInputPending && overlay == Overlay::None && !automaticPageTurnActive &&
      renderer.isSdCardFont(renderFontId) && section && !section->isBuilding() && !RenderLock::peek() &&
      renderer.hasFrameBuffer() && lastRenderCompleteMs != 0 &&
      canRunDeferredReaderWork(static_cast<uint32_t>(millis())) && ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP &&
      ESP.getMaxAllocHeap() > SD_FONT_PREWARM_MIN_MAX_ALLOC &&
      (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage ||
       idlePrewarmFontId != renderFontId)) {
    const uint32_t idleWorkStarted = beginReaderIdleWork("font_prewarm");
    RenderLock lock;
    if (section && !section->isBuilding() &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage ||
         idlePrewarmFontId != renderFontId)) {
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      idlePrewarmFontId = renderFontId;
      const int nextPage = section->currentPage + 1;
      if (nextPage < static_cast<int>(section->pageCount)) {
        if (const auto p = section->loadPage(nextPage)) {
          if (auto* fcm = renderer.getFontCacheManager()) {
            const auto t0 = millis();
            auto scope = fcm->createPrewarmScope();
            p->renderText(renderer, renderFontId, 0, 0);
            scope.endScanAndPrewarm();
            LOG_DBG("ERS", "Idle SD font prewarm: page %d in %lums", nextPage, millis() - t0);
          }
        }
      }
    }
    endReaderIdleWork("font_prewarm", idleWorkStarted);
  }

  if (!userInputPending && canRunDeferredReaderWork(static_cast<uint32_t>(millis())) && section &&
      !section->isBuilding() && section->isPartial() && !RenderLock::peek() && buildViewportWidth > 0 &&
      !partialRebuildStartFailed && !partialRebuildPausedForLowMemory &&
      section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount)) {
    const uint32_t idleWorkStarted = beginReaderIdleWork("partial_build_start");
    RenderLock lock;
    const ReaderRenderSpec buildSpec = activeReaderRenderSpec(buildViewportWidth, buildViewportHeight);
    if (!section->startBuild(buildSpec)) {
      if (recoverSectionBuildFailure()) {
        endReaderIdleWork("partial_build_start", idleWorkStarted);
        return;
      }
      partialRebuildStartFailed = true;
      LOG_ERR("ERS", "Failed to start deferred partial extension build");
    } else {
      LOG_DBG("ERS", "Reader near partial watermark (%d/%d), resuming extension build", section->currentPage,
              section->pageCount);
    }
    endReaderIdleWork("partial_build_start", idleWorkStarted);
  }

  if (!userInputPending && canRunDeferredReaderWork(static_cast<uint32_t>(millis())) && section &&
      section->isBuilding() && !RenderLock::peek() &&
      (section->isPartial() || static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD) &&
      buildTickHeapGate()) {
    const uint32_t idleWorkStarted = beginReaderIdleWork("section_build_tick");
    RenderLock lock;
    if (section->isBuilding() && buildTickHeapGate()) {
      if (!section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK)) {
        LOG_ERR("ERS", "Background section build failed");
        if (shouldPauseEpubBackgroundBuild(section->lastBuildFailure(), section->isPartial(), section->pageCount)) {
          partialRebuildPausedForLowMemory = true;
          buildHeapPaused = true;
          LOG_ERR("ERS", "Background section build suspended at %u readable pages after low-memory abort",
                  section->pageCount);
          endReaderIdleWork("section_build_tick", idleWorkStarted);
          return;
        }
        if (recoverSectionBuildFailure()) {
          endReaderIdleWork("section_build_tick", idleWorkStarted);
          return;
        }
        section.reset();
        requestUpdate();
      } else if (section->isBuildComplete() && applyDeferredReposition()) {
        requestUpdate();
      }
    }
    endReaderIdleWork("section_build_tick", idleWorkStarted);
  }

  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();
  clearEndOfBookOptionsIfNeeded();

  if (SETTINGS.removeReadBooksFromRecents) {
    if (atEndOfBook && !recentsEntryRemoved) {
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
      recentsEntryRemoved = false;
    }
  }

  if (atEndOfBook) {
    completedDuringVisit = true;
    pendingReadFolderMove = SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else {
    pendingReadFolderMove = false;
  }

  if (showBookmarkMessage && (millis() - bookmarkMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showBookmarkMessage = false;
    requestUpdate();
  }

  if (showDictionaryMessage && (millis() - dictionaryMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showDictionaryMessage = false;
    requestUpdate();
  }

  if (showClippingMessage && (millis() - clippingMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showClippingMessage = false;
    requestUpdate();
  }

  // The toolbar owns input before automatic page turning. Opening its rate
  // picker can enable auto-turn while the panel remains visible; a fresh
  // interval starts only after the panel closes.
  if (overlay != Overlay::None) {
    if (usesToolbarMenu()) {
      lastPageTurnTime = millis();
      handleOverlayInput();
      return;
    }
    // The style was switched off while an overlay was up (Settings reached via
    // the More panel); fall back to the clean page.
    overlay = Overlay::None;
    discardOverlayPage();
    requestUpdate();
    return;
  }

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
      toggleAutoPageTurn(0);
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      pageTurnInternal(true, false);
      requestUpdate();
      return;
    }
  }

  // The end-of-book suggestion menu owns Confirm/Back/navigation before the
  // long-press shortcuts below. Polling wasLongPressed() while it is open can
  // suppress the release the menu needs to activate its selected row.
  if (handleEndOfBookMenu()) return;
  const bool endMenuOpen = endOfBookMenuActive();

  const unsigned long confirmHoldMs = confirmLongPressThreshold();
  const bool confirmLongPressed = !endMenuOpen && confirmHoldMs != 0 &&
                                  mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, confirmHoldMs);
  const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  if (confirmLongPressed) {
    switch (SETTINGS.longPressMenuFunction) {
      case CrossPointSettings::LP_MENU_BOOKMARK:
        addBookmark();
        showBookmarkMessage = true;
        bookmarkMessageTime = millis();
        requestUpdate();
        break;
      case CrossPointSettings::LP_MENU_KOSYNC:
        if (launchKOReaderSync()) {
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_DICTIONARY:
        openDictionaryWordSelect();
        return;
      case CrossPointSettings::LP_MENU_READER_MENU:
      case CrossPointSettings::LP_MENU_DISABLED:
      default:
        break;
    }
  }

  // Home-key boards have no front Confirm button, so a Home-key hold runs the
  // same user-selected long-press action. The SDK emits this event once per
  // hold and suppresses the short Home tap for the same contact.
  if (mappedInput.wasHomeKeyHold() && !endMenuOpen) {
    switch (SETTINGS.longPressMenuFunction) {
      case CrossPointSettings::LP_MENU_BOOKMARK:
        if (!showBookmarkMessage) {
          addBookmark();
          showBookmarkMessage = true;
          bookmarkMessageTime = millis();
          requestUpdate();
        }
        return;
      case CrossPointSettings::LP_MENU_KOSYNC:
        launchKOReaderSync();
        return;
      case CrossPointSettings::LP_MENU_DICTIONARY:
        if (!showDictionaryMessage) {
          openDictionaryWordSelect();
        }
        return;
      case CrossPointSettings::LP_MENU_READER_MENU:
        if (usesToolbarMenu() && section) {
          openOverlay(Overlay::Toolbar);
        } else {
          openReaderMenu();
        }
        return;
      case CrossPointSettings::LP_MENU_DISABLED:
      default:
        break;
    }
  }

  // Internal EPUB links take priority over the menu and page-turn touch zones.
  if (!atEndOfBook && !currentPageLinks.empty() && SETTINGS.touchReaderControls && mappedInput.hasTouch()) {
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      const auto* link = EpubReaderUtils::linkAtPoint(currentPageLinks, touchX, touchY, currentPageLinkMarginLeft,
                                                      currentPageLinkMarginTop);
      if (link) {
        navigateToHref(link->href, true);
        return;
      }
    }
  }

  if (confirmReleased || ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
    // Toolbar style: the page is on screen and in the framebuffer, so paint the
    // toolbar over it (one refresh) instead of pushing a full-screen menu.
    if (usesToolbarMenu() && section) {
      clearPendingManualTurns();
      openOverlay(Overlay::Toolbar);
    } else {
      openReaderMenu();
    }
  }

  if (footnoteDepth > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }

  if (handleBackNavigation()) {
    return;
  }

  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES &&
      mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
    } else {
      if (currentPageFootnotes.size() == 1) {
        navigateToHref(currentPageFootnotes[0].href, true);
      } else if (currentPageFootnotes.size() > 1) {
        pauseReadingStats(false);
        startActivityForResult(
            std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                navigateToHref(footnoteResult.href, true);
              }
              requestUpdate();
            });
      }
    }
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  const bool pageTurnTriggered = prevTriggered || nextTriggered;

  if (pageTurnTriggered) {
    if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) {
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      return;
    }

    const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
    const bool longPress = !fromTilt && heldMs >= ReaderUtils::SKIP_HOLD_MS;
    if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP) {
      clearPendingManualTurns();
      beginReaderTurn(nextTriggered ? 1 : -1);
      if (!skipPages(nextTriggered ? 1 : -1)) {
        cancelReaderTurn();
        return;
      }
      requestUpdate();
      return;
    }

    if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.ORIENTATION_CHANGE) {
      clearPendingManualTurns();
      const uint8_t newOrientation =
          nextTriggered ? (SETTINGS.orientation - 1 + SETTINGS.ORIENTATION_COUNT) % SETTINGS.ORIENTATION_COUNT
                        : (SETTINGS.orientation + 1) % SETTINGS.ORIENTATION_COUNT;
      applyOrientation(newOrientation);
      requestUpdate();
      return;
    }

    // Latch this frame's one-shot input before draining an older request. InputManager
    // clears press/release edges on its next update, so returning after a queue pop
    // without doing this would silently lose the fresh press.
    queueManualTurn(!prevTriggered);
  }

  constexpr unsigned long kMinManualTurnGapMs = 200;
  const bool turnGuardActive = RenderLock::peek() || (millis() - lastPageTurnTime) < kMinManualTurnGapMs;
  if (!section) {
    // A turn across a spine boundary deliberately releases Section while the
    // next spine is opened. Keep every manual intent queued until pagination is
    // ready, and make sure a render remains scheduled to open the new section.
    if (pageTurnTriggered) requestUpdate();
    return;
  }

  if (turnGuardActive) {
    return;
  }

  if (pendingManualTurns.empty()) return;

  const bool forward = pendingManualTurns.pop() > 0;
  const int pendingDepth = pendingManualTurns.pending();
  updateReaderTurnQueueDepth(pendingDepth < 0 ? -pendingDepth : pendingDepth);
  if (!pageTurn(forward)) {
    clearPendingManualTurns();
    return;
  }
  requestUpdate();
}

void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) return;
  clearPendingManualTurns();
  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) return;

  percent = clampPercent(percent);

  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) targetSize = bookSize - 1;

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) return;

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  pendingSpineProgress = std::clamp(pendingSpineProgress, 0.0f, 1.0f);

  {
    RenderLock lock;
    clearDeferredReposition();
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  auto progressChangeResultHandler = [this](const ActivityResult& result) {
    loadCachedBookmarks();
    if (result.isCancelled) {
      openReaderMenu();
    } else {
      const auto& sync = std::get<ProgressChangeResult>(result.data);

      if (sync.hasVisibleTextOffset && sync.spineIndex >= 0 && sync.spineIndex < epub->getSpineItemsCount()) {
        RenderLock lock;
        clearDeferredReposition();
        if (section && currentSpineIndex == sync.spineIndex) {
          const auto page = section->getPageForVisibleTextOffset(sync.visibleTextOffset);
          section->currentPage = page.value_or(std::max(0, sync.page));
        } else {
          currentSpineIndex = sync.spineIndex;
          pendingOffsetJump = sync.visibleTextOffset;
          nextPageNumber = std::max(0, sync.page);
          section.reset();
        }
        requestUpdate();
        return;
      }

      int targetSpineIndex = sync.spineIndex;
      int targetPage = sync.page;
      const int activeTotalPages = section ? section->estimatedTotalPages() : 0;
      const bool cachedPageMatchesActiveSection = section && sync.totalPages > 0 &&
                                                  currentSpineIndex == sync.spineIndex && sync.page >= 0 &&
                                                  sync.page < sync.totalPages && activeTotalPages == sync.totalPages;

      if (!cachedPageMatchesActiveSection && sync.hasSavedProgress) {
        const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
        CrossPointPosition fallback =
            ProgressMapper::toCrossPoint(epub, {sync.xpath, sync.percentage}, renderer, currentSpineIndex, totalPages);
        targetSpineIndex = fallback.spineIndex;
        targetPage = fallback.pageNumber;
      }

      RenderLock lock;
      clearDeferredReposition();

      if (currentSpineIndex != targetSpineIndex) {
        currentSpineIndex = targetSpineIndex;
        nextPageNumber = targetPage;
        section.reset();
      } else if (section && section->currentPage != targetPage) {
        const int clampedTargetPage = std::max(0, targetPage);
        section->currentPage = clampedTargetPage;
      } else if (!section) {
        nextPageNumber = targetPage;
      }
      requestUpdate();
    }
  };

  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      // Release the section while the chapter list is up (mirrors the
      // TEXT_SETTINGS path): picking a chapter resets it anyway, and its
      // tens-of-KB footprint is the difference between the chapter list
      // holding its CJK glyph arena (RAM-only repaints) and re-reading
      // glyphs from SD on every row step. Cancel restores via the same
      // cached-position rebuild TEXT_SETTINGS uses.
      {
        RenderLock lock;
        if (section) {
          rememberCurrentContentOffset();
          cachedSpineIndex = currentSpineIndex;
          cachedChapterTotalPageCount = section->estimatedTotalPages();
          cachedChapterPageCountEstimated = section->isBuilding() || section->isPartial();
          nextPageNumber = section->currentPage;
        }
        section.reset();
      }
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, spineIdx),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
              return;
            }
            const auto& chapterResult = std::get<ChapterResult>(result.data);
            RenderLock lock;
            clearDeferredReposition();
            currentSpineIndex = chapterResult.spineIndex;
            pendingAnchor = chapterResult.anchor;
            nextPageNumber = 0;
            section.reset();
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled) {
                                 openReaderMenu();
                                 return;
                               }
                               const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                               navigateToHref(footnoteResult.href, true);
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS: {
      startActivityForResult(std::make_unique<TextSettingsActivity>(
                                 renderer, mappedInput, &sdFontSystem.registry(), TextSettingsActivity::Tab::Family,
                                 false, [this] { saveBookReaderSettings(); }, &bookReaderSettings.wordSpacing,
                                 &bookReaderSettings.repairParagraphIndent),
                             [this](const ActivityResult&) {
                               {
                                 RenderLock lock;
                                 if (section) {
                                   rememberCurrentContentOffset();
                                   cachedSpineIndex = currentSpineIndex;
                                   cachedChapterTotalPageCount = section->estimatedTotalPages();
                                   cachedChapterPageCountEstimated = section->isBuilding() || section->isPartial();
                                   nextPageNumber = section->currentPage;
                                 }
                                 section.reset();
                               }
                               openReaderMenu();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::NIGHT_MODE:
      // Handled in-place by EpubReaderMenuActivity so its On/Off value updates
      // without closing the menu.
      break;
    case EpubReaderMenuActivity::MenuAction::FRONTLIGHT:
      // Handled in-place by EpubReaderMenuActivity using the live frontlight HAL.
      break;
    case EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN:
    case EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN:
    case EpubReaderMenuActivity::MenuAction::WORD_SPACING:
    case EpubReaderMenuActivity::MenuAction::REPAIR_PARAGRAPH_INDENT:
    case EpubReaderMenuActivity::MenuAction::RENDER_MODE:
    case EpubReaderMenuActivity::MenuAction::TRY_FULL_RENDER_QUALITY:
      // The classic menu applies these through MenuResult before dispatch;
      // the toolbar More panel handles them in place.
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
            } else {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READING_STATS: {
      startActivityForResult(std::make_unique<BookStatsActivity>(
                                 renderer, mappedInput, epub->getTitle(), epub->getPath(), epub->getCachePath(),
                                 readingStatsSnapshot(), globalReadingStatsSnapshot(), currentBookProgressPercent()),
                             [this](const ActivityResult&) {
                               BookReadingStats edited;
                               if (ReadingStatsStore::loadBook(epub->getPath(), epub->getCachePath(), edited) ==
                                   ReadingStatsStore::LoadStatus::LOADED) {
                                 readingStats = edited;
                               }
                               openReaderMenu();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY: {
      openDictionaryWordSelect();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::LOOKUP_HISTORY: {
      pauseReadingStats(false);
      const std::string dictionaryName = activeDictionaryName();
      if (dictionaryName.empty()) {
        showDictionaryMessage = true;
        dictionaryMessageTime = millis();
        resumeReadingStats();
        requestUpdate();
        break;
      }
      startActivityForResult(std::make_unique<DictionaryHistoryActivity>(renderer, mappedInput, dictionaryName),
                             [this](const ActivityResult&) {
                               resumeReadingStats();
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY_SWITCH: {
      pauseReadingStats(false);
      startActivityForResult(
          std::make_unique<DictionarySelectActivity>(renderer, mappedInput, DictionarySelectActivity::Mode::Temporary,
                                                     activeDictionaryName(), SETTINGS.dictionaryName),
          [this](const ActivityResult& result) {
            if (result.isCancelled || !std::holds_alternative<DictionarySelectionResult>(result.data)) {
              resumeReadingStats();
              requestUpdate();
              return;
            }
            temporaryDictionaryName = std::get<DictionarySelectionResult>(result.data).name;
            openDictionaryWordSelect();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY_BOOK: {
      pauseReadingStats(false);
      startActivityForResult(
          std::make_unique<DictionarySelectActivity>(renderer, mappedInput, DictionarySelectActivity::Mode::PerBook,
                                                     activeDictionaryName(), SETTINGS.dictionaryName),
          [this](const ActivityResult& result) {
            if (!result.isCancelled && std::holds_alternative<DictionarySelectionResult>(result.data)) {
              const auto& selection = std::get<DictionarySelectionResult>(result.data);
              bookReaderSettings.hasDictionaryOverride = !selection.inheritGlobal;
              bookReaderSettings.dictionaryName.fill('\0');
              if (!selection.name.empty()) {
                std::strncpy(bookReaderSettings.dictionaryName.data(), selection.name.c_str(),
                             bookReaderSettings.dictionaryName.size() - 1);
              }
              temporaryDictionaryName.clear();
              saveBookDictionarySettings();
            }
            resumeReadingStats();
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::HIGHLIGHT_TEXT: {
      openClipSelection();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::MY_CLIPPINGS: {
      startActivityForResult(std::make_unique<EpubReaderClippingsActivity>(renderer, mappedInput, epub, cachedClippings,
                                                                           clippingsWritable),
                             progressChangeResultHandler);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          const std::string qrPayload =
              vns_reference::buildQrPayload(fullText, currentVnsReferencePosition(), QrUtils::MAX_PAYLOAD_BYTES);
          startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, qrPayload),
                                 [this](const ActivityResult&) { openReaderMenu(); });
          break;
        }
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock;
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          saveBookReaderSettings();
          if (readingStatsWritable &&
              ReadingStatsStore::saveBook(epub->getPath(), epub->getCachePath(), readingStats) !=
                  ReadingStatsStore::SaveStatus::SAVED) {
            LOG_ERR("RSTAT", "Failed to restore reading statistics after cache clear");
          }
          if (ClippingStore::save(epub->getPath(), epub->getCachePath(), cachedClippings) !=
              ClippingStore::SaveStatus::SAVED) {
            LOG_ERR("CLIP", "Failed to restore clippings after cache clear");
          }
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          }
        }
      }
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock;
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      launchKOReaderSync();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS: {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, epub, epub->getPath()),
          progressChangeResultHandler);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK: {
      addBookmark();
      break;
    }
  }
}

unsigned long EpubReaderActivity::confirmLongPressThreshold() const {
  switch (SETTINGS.longPressMenuFunction) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
    case CrossPointSettings::LP_MENU_DICTIONARY:
      return ReaderUtils::BOOKMARK_HOLD_MS;
    case CrossPointSettings::LP_MENU_KOSYNC:
      return KOREADER_STORE.hasCredentials() ? ReaderUtils::GO_HOME_MS : 0;
    case CrossPointSettings::LP_MENU_READER_MENU:
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return 0;
  }
}

bool EpubReaderActivity::launchKOReaderSync() {
  if (!KOREADER_STORE.hasCredentials()) return false;

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoPos = ProgressMapper::toSavedProgress(epub, localPos);
  const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
  const std::string savedEpubPath = epub->getPath();

  if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
    LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
    pendingSyncSaveError = true;
    requestUpdate();
    return true;
  }

  LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
  {
    RenderLock lock;
    if (section) {
      nextPageNumber = section->currentPage;
    }
    ImageBlock::setExtractor(nullptr, nullptr);
    section.reset();
    epub.reset();
  }
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
      renderer, mappedInput, savedEpubPath, currentSpineIndex, currentPage, totalPages, std::move(localKoPos),
      std::move(localChapterName), paragraphIndex, SETTINGS.orientation));
  return true;
}

void EpubReaderActivity::applyInitialOrientation() {
  ReaderActivity::applyInitialOrientation();
  appliedOrientation = SETTINGS.orientation;
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // Also runs when SETTINGS already holds the new value but this layout was
  // built for the old one — that is what an external change looks like here.
  if (SETTINGS.orientation == orientation && appliedOrientation == orientation) {
    return;
  }

  clearPendingManualTurns();

  RenderLock lock(*this);
  if (section) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->estimatedTotalPages();
    cachedChapterPageCountEstimated = section->isBuilding() || section->isPartial();
    nextPageNumber = section->currentPage;
  }

  if (SETTINGS.orientation != orientation) {
    SETTINGS.orientation = orientation;
    saveBookReaderSettings();
  }
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  appliedOrientation = orientation;
  section.reset();
}

void EpubReaderActivity::toggleAutoPageTurn(const uint16_t seconds) {
  clearPendingManualTurns();
  const int selectedOption = autoTurnOptionForSeconds(seconds);
  const uint16_t normalizedSeconds = selectedOption > 0 ? AUTO_TURN_SECONDS[selectedOption] : 0;
  const bool settingChanged = bookReaderSettings.autoPageTurnSeconds != normalizedSeconds;
  bookReaderSettings.autoPageTurnSeconds = normalizedSeconds;
  autoTurnOption = selectedOption;
  if (settingChanged) saveBookReaderSettings();

  if (normalizedSeconds == 0) {
    automaticPageTurnActive = false;
    pageTurnDuration = 0;
    return;
  }

  lastPageTurnTime = millis();
  pageTurnDuration = static_cast<unsigned long>(normalizedSeconds) * 1000UL;
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    RenderLock lock;
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->estimatedTotalPages();
      cachedChapterPageCountEstimated = section->isBuilding() || section->isPartial();
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
}

void EpubReaderActivity::applyExtendedReaderSettings(const uint8_t wordSpacing, const bool repairParagraphIndent,
                                                     const uint8_t preferredRenderMode,
                                                     const bool retryPreferredRender) {
  const uint8_t normalizedSpacing = std::min<uint8_t>(wordSpacing, 2);
  const uint8_t normalizedMode = std::min<uint8_t>(preferredRenderMode, static_cast<uint8_t>(EpubRenderMode::Safe));
  const bool settingsChanged =
      bookReaderSettings.wordSpacing != normalizedSpacing ||
      bookReaderSettings.repairParagraphIndent != static_cast<uint8_t>(repairParagraphIndent) ||
      bookReaderSettings.preferredRenderMode != normalizedMode;
  if (!settingsChanged && !retryPreferredRender) return;

  const EpubRenderMode preferredMode = static_cast<EpubRenderMode>(normalizedMode);
  if (!settingsChanged && retryPreferredRender) {
    if (preferredRenderTrialActive ||
        !shouldStartPreferredRenderTrial(normalizedMode, activeRenderMode, settingsChanged)) {
      return;
    }
    RenderLock lock(*this);
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->estimatedTotalPages();
      cachedChapterPageCountEstimated = section->isBuilding() || section->isPartial();
      nextPageNumber = section->currentPage;
    }
    preferredRenderTrialRollbackMode = activeRenderMode;
    preferredRenderTrialActive = true;
    pendingWorkingFallback = UINT8_MAX;
    activeRenderMode = preferredMode;
    section.reset();
    requestUpdate();
    return;
  }

  RenderLock lock(*this);
  if (section) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->estimatedTotalPages();
    cachedChapterPageCountEstimated = section->isBuilding() || section->isPartial();
    nextPageNumber = section->currentPage;
  }
  const uint8_t oldPreferredMode = bookReaderSettings.preferredRenderMode;
  const EpubRenderMode oldActiveMode = activeRenderMode;
  bookReaderSettings.wordSpacing = normalizedSpacing;
  bookReaderSettings.repairParagraphIndent = repairParagraphIndent ? 1 : 0;
  bookReaderSettings.preferredRenderMode = normalizedMode;
  bookReaderSettings.lastWorkingFallback = UINT8_MAX;
  bookReaderSettings.fallbackRenderSignature = 0;
  preferredRenderTrialActive = false;
  if (oldPreferredMode == normalizedMode && oldActiveMode > preferredMode) {
    // A spacing/indent-only change invalidates the old cache signature, but it
    // does not prove that the known working render mode became unsafe. Rebuild
    // there first and persist its new signature only after a page renders.
    activeRenderMode = oldActiveMode;
    pendingWorkingFallback = static_cast<uint8_t>(oldActiveMode);
  } else {
    activeRenderMode = preferredMode;
    pendingWorkingFallback = UINT8_MAX;
  }
  saveBookReaderSettings();
  section.reset();
  requestUpdate();
}

bool EpubReaderActivity::pageTurn(bool isForwardTurn) { return pageTurnInternal(isForwardTurn, true); }

void EpubReaderActivity::clearPendingManualTurns() {
  pendingManualTurns.clear();
  turnTelemetry.clear();
}

void EpubReaderActivity::queueManualTurn(const bool forward) {
  const int before = pendingManualTurns.pending();
  pendingManualTurns.push(forward);
  const int after = pendingManualTurns.pending();
  const int beforeDepth = before < 0 ? -before : before;
  const int afterDepth = after < 0 ? -after : after;

  if (afterDepth < beforeDepth) {
    // An opposite press cancels one queued refresh, so it also cancels the
    // corresponding newest telemetry record instead of inventing a page turn.
    cancelReaderTurn();
    if (afterDepth > 0) updateReaderTurnQueueDepth(afterDepth);
    return;
  }
  if (after == before) return;  // Bounded queue is already full.
  beginReaderTurn(forward ? 1 : -1, afterDepth);
}

bool EpubReaderActivity::pageTurnInternal(bool isForwardTurn, bool countForwardPace) {
  if (!section) return false;
  {
    RenderLock lock;
    clearDeferredReposition();
  }
  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1 || section->isBuilding()) {
      pauseReadingStats(countForwardPace);
      section->currentPage++;
      lastPageTurnTime = millis();
      return true;
    } else if (currentSpineIndex + 1 < epub->getSpineItemsCount()) {
      pauseReadingStats(countForwardPace);
      RenderLock lock;
      nextPageNumber = 0;
      currentSpineIndex++;
      section.reset();
      lastPageTurnTime = millis();
      return true;
    } else {
      pauseReadingStats(countForwardPace);
      currentSpineIndex = epub->getSpineItemsCount();
      lastPageTurnTime = millis();
      return true;
    }
  } else {
    if (section->currentPage > 0) {
      pauseReadingStats(false);
      section->currentPage--;
      lastPageTurnTime = millis();
      return true;
    } else if (currentSpineIndex > 0) {
      pauseReadingStats(false);
      RenderLock lock;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      currentSpineIndex--;
      section.reset();
      lastPageTurnTime = millis();
      return true;
    }
  }
  return false;
}

bool EpubReaderActivity::skipPages(int amount) {
  if (!section) return false;
  if (amount > 0) {
    pauseReadingStats(false);
    RenderLock lock;
    nextPageNumber = 0;
    currentSpineIndex++;
    section.reset();
    return true;
  } else {
    if (section->currentPage > 0) {
      pauseReadingStats(false);
      section->currentPage = 0;
      return true;
    } else if (currentSpineIndex > 0) {
      pauseReadingStats(false);
      RenderLock lock;
      nextPageNumber = 0;
      currentSpineIndex--;
      section.reset();
      return true;
    }
  }
  return false;
}

bool EpubReaderActivity::isAtEndOfBook() const { return epub && currentSpineIndex >= epub->getSpineItemsCount(); }

void EpubReaderActivity::onReturnFromEndOfBook() {
  clearPendingManualTurns();
  if (epub && epub->getSpineItemsCount() > 0) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = 0;
    pendingPageJump = std::numeric_limits<uint16_t>::max();
  }
}

bool EpubReaderActivity::skipLoopDelay() {
  return section && section->isBuilding() && !buildHeapPaused &&
         (section->isPartial() || static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD);
}

void EpubReaderActivity::renderBook() {
  renderedReadingPage = false;
  currentPageLinks.clear();
  currentPublisherPageLabel.clear();
  if (!epub) return;
#if !defined(SIMULATOR)
  HalSystem::setPanicBreadcrumb(HalSystem::PanicStage::ReaderRenderBegin, currentSpineIndex,
                                section ? section->currentPage : nextPageNumber);
#endif

  const auto showPendingSyncSaveError = [this]() {
    if (!pendingSyncSaveError) return;
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  };

  const auto showPendingBookSettingsSaveError = [this]() {
    if (!pendingBookSettingsSaveError) return;
    pendingBookSettingsSaveError = false;
    GUI.drawPopup(renderer, tr(STR_READER_SETTINGS_SAVE_FAILED));
  };

  const auto handleBuildFailure = [this]() {
    const SectionBuildFailure failure = section ? section->lastBuildFailure() : SectionBuildFailure::InvalidContent;
    if (recoverSectionBuildFailure()) return;
    section.reset();
    showSectionBuildError(failure);
  };

  if (currentSpineIndex < 0) currentSpineIndex = 0;
  if (currentSpineIndex > epub->getSpineItemsCount()) currentSpineIndex = epub->getSpineItemsCount();

  if (currentSpineIndex == epub->getSpineItemsCount()) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

  const bool autoTurnNeedsTextLane =
      automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight());
  const int extraStatusHeight = autoTurnNeedsTextLane ? UITheme::getInstance().getMetrics().statusBarVerticalMargin : 0;
  orientedMarginBottom += reader_viewport::bottomInset(SETTINGS.screenMargin, statusBarHeight, extraStatusHeight);

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;

  const ReaderRenderSpec renderSpec = activeReaderRenderSpec(viewportWidth, viewportHeight);

  if (!section) {
    partialRebuildPausedForLowMemory = false;
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = makeUniqueNoThrow<Section>(epub, currentSpineIndex, renderer);
    partialRebuildStartFailed = false;
    if (!section) {
      LOG_ERR("ERS", "OOM: Section for spine %d", currentSpineIndex);
      if (!recoverSectionFailure(SectionBuildFailure::LowMemory)) {
        showSectionBuildError(SectionBuildFailure::LowMemory);
      }
      return;
    }

    const bool cacheLoaded = section->loadSectionFile(renderSpec);
    if (cacheLoaded) {
      cachedChapterTotalPageCount = 0;
      cachedChapterPageCountEstimated = false;
      cachedVisibleTextOffset.reset();
    }
    const bool cacheComplete = cacheLoaded && !section->isPartial();
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
    render_lab::recordSectionCacheHit(cacheComplete);
#endif
    const bool explicitOffsetJump = pendingOffsetJump.has_value();
    const std::optional<uint32_t> offsetJump =
        explicitOffsetJump ? pendingOffsetJump
        : (pendingPageJump.has_value() || !pendingAnchor.empty() || currentSpineIndex != cachedSpineIndex)
            ? std::nullopt
            : cachedVisibleTextOffset;
    if (!cacheComplete) {
      if (section->isPartial()) {
        LOG_DBG("ERS", "Partial cache found (%d pages), resuming build...", section->pageCount);
      } else {
        LOG_DBG("ERS", "Cache not found, building...");
      }

      const bool needsFullBuild = pendingPercentJump
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
                                  || (render_lab::enabled() && render_lab::requiresFullBuild())
#endif
          ;
      if (needsFullBuild) {
        GUI.drawPopup(renderer, tr(STR_INDEXING));
        pagesUntilFullRefresh = 1;
        const auto popupFn = [this]() {
          if (renderer.hasFrameBuffer()) GUI.drawPopup(renderer, tr(STR_INDEXING));
        };
        GfxRenderer::FrameBufferLoan loan(renderer);
        if (!section->createSectionFile(renderSpec, popupFn)) {
          LOG_ERR("ERS", "Failed to persist page data to SD");
          loan.end();
          handleBuildFailure();
          return;
        }
        loan.end();
      } else {
        const int target = pendingPageJump.has_value() ? *pendingPageJump : (nextPageNumber < 0 ? 0 : nextPageNumber);
        const bool anchorJump = !pendingAnchor.empty();

        if (section->isPartial() &&
            (anchorJump ? section->getPageForAnchor(pendingAnchor).has_value()
                        : target + PARTIAL_REBUILD_START_MARGIN < static_cast<int>(section->pageCount))) {
          LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", target, section->pageCount);
        } else {
          const size_t spineBytes =
              epub->getCumulativeSpineItemSize(currentSpineIndex) -
              (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
          const bool willInflate = !section->hasHtmlCache();
          bool showPopup;
          if (anchorJump) {
            showPopup = !section->findAnchor(pendingAnchor).has_value() && spineBytes > BUILD_POPUP_BYTE_THRESHOLD;
          } else {
            const bool targetAvailable = target < static_cast<int>(section->pageCount);
            showPopup = !targetAvailable && ((spineBytes > BUILD_POPUP_BYTE_THRESHOLD && willInflate) ||
                                             target > BUILD_POPUP_PAGE_THRESHOLD);
          }
          if (showPopup) {
            GUI.drawPopup(renderer, tr(STR_INDEXING));
            pagesUntilFullRefresh = 1;
          }
          buildPopupPending = !showPopup;
          const unsigned long buildStartMs = millis();
          bool started;
          {
            GfxRenderer::FrameBufferLoan loan(renderer);
            started = section->startBuild(renderSpec, [this] { showBuildPopup(renderer, pagesUntilFullRefresh); });
          }
          if (!started) {
            LOG_ERR("ERS", "Failed to start section build");
            buildPopupPending = false;
            handleBuildFailure();
            return;
          }
          while (!section->isBuildComplete() &&
                 (anchorJump               ? !section->findAnchor(pendingAnchor)
                  : offsetJump.has_value() ? !section->buildReachedVisibleTextOffset(*offsetJump)
                                           : static_cast<int>(section->pageCount) <= target)) {
            if (buildPopupPending && millis() - buildStartMs >= BUILD_POPUP_DEADLINE_MS) {
              showBuildPopup(renderer, pagesUntilFullRefresh);
            }
            if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
              LOG_ERR("ERS", "Failed during incremental section build");
              buildPopupPending = false;
              handleBuildFailure();
              return;
            }
          }
          buildPopupPending = false;
        }
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      section->currentPage = *pendingPageJump;
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) section->currentPage = 0;
    }

    if (offsetJump.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*offsetJump)) {
        section->currentPage = *offsetPage;
        clearDeferredReposition();
      }
    }
    if (explicitOffsetJump) {
      clearDeferredReposition();
    }
    pendingOffsetJump.reset();

    if (!pendingAnchor.empty()) {
      const auto page = section->findAnchor(pendingAnchor);
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
      render_lab::recordAnchorResolution(pendingAnchor.c_str(), page.has_value());
#endif
      if (page) {
        section->currentPage = *page;
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
        if (render_lab::enabled()) {
          section->currentPage += render_lab::targetPageOffset();
        }
#endif
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      }
      pendingAnchor.clear();
    }

    if (pendingPercentJump && section->pageCount > 0) {
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) newPage = section->pageCount - 1;
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  if (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    pagesUntilFullRefresh = 1;
  }
  while (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    if (!section->isBuilding() && !section->startBuild(renderSpec)) {
      LOG_ERR("ERS", "Failed to start partial extension build");
      handleBuildFailure();
      return;
    }
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        handleBuildFailure();
        return;
      }
    }
  }
  if (section->isBuilding()) {
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        handleBuildFailure();
        return;
      }
    }
  }

  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }

  applyDeferredReposition();

  renderer.clearScreen();

  if (section->pageCount == 0) {
    if (preferredRenderTrialActive && recoverSectionBuildFailure()) return;
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  updateBookmarkFlag();

  {
    auto p = section->loadPage(section->currentPage);
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      automaticPageTurnActive = false;
      if (preferredRenderTrialActive) {
        // Loading is part of the quality trial's success criterion. Drop only
        // the failed preferred-mode generation, then return to the still-known
        // fallback instead of repeatedly rebuilding the trial mode.
        section->abandonBuild();
        section->clearCache();
        pageLoadRetryCount = 0;
        recoverSectionBuildFailure();
        showPendingSyncSaveError();
        return;
      }
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      section->abandonBuild();
      section->clearCache();
      section.reset();
      if (giveUp) {
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
        renderer.displayBuffer();
        showPendingSyncSaveError();
        return;
      }
      requestUpdate();
      showPendingSyncSaveError();
      return;
    }
    pageLoadRetryCount = 0;
#if !defined(SIMULATOR)
    HalSystem::setPanicBreadcrumb(HalSystem::PanicStage::ReaderPageLoaded, currentSpineIndex, section->currentPage);
#endif

    currentPageVisibleOffset = p->visibleTextOffset;
    currentPageFootnotes = std::move(p->footnotes);
    currentPageLinks = std::move(p->links);
    currentPageLinkMarginLeft = orientedMarginLeft;
    currentPageLinkMarginTop = orientedMarginTop;
    currentPublisherPageLabel = p->publisherPageLabel;

    // The overlay and non-tiled grayscale renderer share one stored-BW slot.
    // Release the stale page before renderContents() may need that slot.
    discardOverlayPage();

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
#if !defined(SIMULATOR)
    HalSystem::setPanicBreadcrumb(HalSystem::PanicStage::ReaderRenderDone, currentSpineIndex,
                                  section ? section->currentPage : -1);
#endif
    rememberRenderedFallback();
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    lastRenderCompleteMs = millis();
    renderedReadingPage = true;
    noteRenderedPageForStats();
  }

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
  if (render_lab::enabled()) {
    render_lab::complete(renderer, currentSpineIndex, section->currentPage, section->estimatedTotalPages(),
                         currentPageVisibleOffset.value_or(0), activeRenderMode, currentPublisherPageLabel.c_str());
  }
#endif

  queueProgressSave();

  showPendingSyncSaveError();
  showPendingBookSettingsSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  if (showBookmarkMessage) {
    GUI.drawPopup(renderer, bookmarkRemoved ? tr(STR_BOOKMARK_REMOVED) : tr(STR_BOOKMARK_ADDED));
  }

  if (showDictionaryMessage) {
    GUI.drawPopup(renderer, tr(STR_DICT_NO_DICT_SET));
  }

  if (showClippingMessage) {
    GUI.drawPopup(renderer, I18N.get(clippingMessage));
  }

  // Toolbar menu: overlay the toolbar / panel on top of the freshly rendered page.
  if (overlay != Overlay::None && usesToolbarMenu()) {
    // The page just re-rendered under the overlay: refresh the snapshot that
    // backs panel->toolbar restores (any previous copy is stale).
    overlayPageStored = renderer.storeBwBuffer();
    renderOverlay();
    // An open option picker rides on top of the freshly drawn panel.
    if (overlayPopup.isActive()) overlayPopup.render(renderer);
    // HALF on the Xteink grayscale panels: the page render above just ran the
    // anti-aliasing waveform, and a FAST differential leaves the covered text
    // ghosting gray through the chrome background (see openOverlay).
    renderer.displayBuffer(xteinkClassPanel() ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  }
}

void EpubReaderActivity::onEndOfBookRendered() {
  automaticPageTurnActive = false;
  if (pendingSyncSaveError) {
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  }
  if (pendingBookSettingsSaveError) {
    pendingBookSettingsSaveError = false;
    GUI.drawPopup(renderer, tr(STR_READER_SETTINGS_SAVE_FAILED));
  }
}

bool EpubReaderActivity::applyDeferredReposition() {
  if ((!cachedVisibleTextOffset.has_value() && cachedChapterTotalPageCount == 0) || !section) return false;

  // A content anchor can be resolved from the live LUT before the rest of a
  // long spine finishes building.  Waiting for the complete chapter renders a
  // transient page at the old numeric index first, which looks like a jump.
  if (section->isBuilding()) {
    if (!cachedVisibleTextOffset.has_value() || !section->buildReachedVisibleTextOffset(*cachedVisibleTextOffset)) {
      return false;
    }
  }
  bool changed = false;
  if (currentSpineIndex == cachedSpineIndex) {
    int newPage = section->currentPage;
    bool mappedOffset = false;
    if (cachedVisibleTextOffset.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*cachedVisibleTextOffset)) {
        newPage = *offsetPage;
        mappedOffset = true;
      }
    }
    if (!mappedOffset && cachedChapterTotalPageCount > 0 && section->pageCount != cachedChapterTotalPageCount) {
      const float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
      newPage = static_cast<int>(progress * static_cast<float>(section->pageCount));
    }
    if (newPage < 0) newPage = 0;
    if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
      newPage = section->pageCount - 1;
    }
    if (newPage != section->currentPage) {
      section->currentPage = newPage;
      changed = true;
    }
  }
  clearDeferredReposition();
  return changed;
}

void EpubReaderActivity::clearDeferredReposition() {
  cachedChapterTotalPageCount = 0;
  cachedChapterPageCountEstimated = false;
  cachedVisibleTextOffset.reset();
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  if (!epub) return false;
  std::optional<uint32_t> offset;
  if (section && spineIndex == currentSpineIndex && currentPage >= 0 && currentPage < section->pageCount) {
    offset = (currentPage == section->currentPage && currentPageVisibleOffset.has_value())
                 ? currentPageVisibleOffset
                 : section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage));
  }
  if (!EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount, offset)) {
    progressSaveDebouncer.markAttemptFailed(millis());
    return false;
  }
  lastSavedSpineIndex = spineIndex;
  lastSavedPage = currentPage;
  lastSavedPageCount = pageCount;
  progressSaveDebouncer.seedPersisted(progressPositionKey(spineIndex, currentPage),
                                      static_cast<uint32_t>(std::max(pageCount, 0)), millis());
  return true;
}

void EpubReaderActivity::queueProgressSave() {
  if (!epub || !section) return;
  const int page = section->currentPage;
  const int pageCount = section->estimatedTotalPages();
  const uint32_t position = progressPositionKey(currentSpineIndex, page);
  const uint32_t metadata = static_cast<uint32_t>(std::max(pageCount, 0));
  if (progressSaveDebouncer.observe(position, metadata, millis())) {
    saveProgress(currentSpineIndex, page, pageCount);
  }
}

void EpubReaderActivity::flushReaderState() {
  if (!epub || !progressSaveDebouncer.hasPending()) return;
  if (footnoteDepth > 0) {
    const SavedPosition& origin = savedPositions[0];
    saveProgress(origin.spineIndex, origin.pageNumber, 0);
    return;
  }
  // Persist the last page that was actually rendered/observed. The live reader
  // position may already be the end-of-book sentinel, which must never replace
  // the final readable page in progress.bin.
  const uint32_t position = progressSaveDebouncer.lastObservedPosition();
  const int spineIndex = static_cast<int>((position >> 16) & UINT16_MAX);
  const int page = static_cast<int>(position & UINT16_MAX);
  const int pageCount = static_cast<int>(progressSaveDebouncer.lastObservedMetadata());
  saveProgress(spineIndex, page, pageCount);
}

void EpubReaderActivity::requestProgressSaveIfDue() {
  // Input is sampled on the main loop. Never wait behind a multi-stage e-ink
  // render here: a complete button tap during that wait would never become an
  // edge event. The render task owns the same mutex and will leave the pending
  // save intact for a later idle iteration.
  RenderLock lock(RenderLock::AcquireMode::Try);
  if (!lock.locked()) return;
  if (progressSaveDebouncer.due(millis())) flushReaderState();
}

void EpubReaderActivity::rememberCurrentContentOffset() {
  cachedVisibleTextOffset.reset();
  if (!section || section->currentPage < 0 || section->currentPage >= section->pageCount) return;

  const uint16_t currentPage = static_cast<uint16_t>(section->currentPage);
  const std::optional<uint32_t> pageStart = currentPageVisibleOffset.has_value()
                                                ? currentPageVisibleOffset
                                                : section->getVisibleTextOffsetForPage(currentPage);
  std::optional<uint32_t> nextPageStart;
  if (section->currentPage + 1 < section->pageCount) {
    nextPageStart = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage + 1));
  }
  cachedVisibleTextOffset = reader_interaction::readingAnchorAtPageCenter(pageStart, nextPageStart);
}

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  const int fontId = SETTINGS.getReaderFontId();

  ImageBlock::beginRenderCycle();

  struct PxcSlotGuard {
    ~PxcSlotGuard() { ImageBlock::releaseRenderCache(); }
  } pxcSlotGuard;

  // First visits have neither the extracted source image nor its .pxc cache.
  // Decode while contiguous heap is still at its peak; font prewarm can
  // otherwise leave the PNG/JPEG decoder without a sufficiently large block.
  // A revisit succeeds more often only because this cache was created by the
  // first attempt. The decoder paints as it caches, so discard that scratch
  // output before scanning fonts and rendering the visible page.
  const bool pageHasImages = page->hasImages();
  auto* fcm = renderer.getFontCacheManager();
  if (pageHasImages && page->hasImagesNeedingDecode()) {
    // CrossInk releases rebuildable font caches before inline-image work. Do
    // the same at the narrower and safer point here: only for a cold image
    // page, after layout is complete and immediately before decoder
    // allocation. The scan below rebuilds exactly the glyphs this page needs.
    if (fcm) fcm->releaseSdFontCaches();

    const auto imageWarmStarted = millis();
    const uint32_t imageWarmFreeBefore = ESP.getFreeHeap();
    const uint32_t imageWarmLargestBefore = ESP.getMaxAllocHeap();
    if (page->warmImageCaches(renderer, fontId, orientedMarginLeft, orientedMarginTop)) {
      renderer.clearScreen();
      LOG_DBG("ERS", "Image cache warmup: elapsed=%lums free=%u->%u largest=%u->%u", millis() - imageWarmStarted,
              static_cast<unsigned>(imageWarmFreeBefore), static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(imageWarmLargestBefore), static_cast<unsigned>(ESP.getMaxAllocHeap()));

      // A warmup failure may be transient (SD extraction visibility or a
      // fragmented decoder allocation). Start the visible render as a fresh
      // cycle so it gets one controlled retry after temporary decoder/file
      // objects have been released. A second failure remains suppressed for
      // all B/W and grayscale passes of this page.
      ImageBlock::beginRenderCycle();
    }
  }

  auto scope = fcm->createPrewarmScope();
#if !defined(SIMULATOR)
  HalSystem::setPanicBreadcrumb(HalSystem::PanicStage::ReaderScanBegin, currentSpineIndex,
                                section ? section->currentPage : -1);
#endif
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  // Scan the status bar too: a CJK book/chapter title redirected to the SD
  // fallback font joins the page's single batch prewarm instead of triggering
  // its own SD pass after the scope ends.
  renderStatusBar();
#if !defined(SIMULATOR)
  HalSystem::setPanicBreadcrumb(HalSystem::PanicStage::ReaderScanDone, currentSpineIndex,
                                section ? section->currentPage : -1);
#endif
  scope.endScanAndPrewarm();
#if !defined(SIMULATOR)
  HalSystem::setPanicBreadcrumb(HalSystem::PanicStage::ReaderPrewarmDone, currentSpineIndex,
                                section ? section->currentPage : -1);
#endif
  const auto tPrewarm = millis();
  std::optional<uint32_t> nextPageVisibleOffset;
  if (section && std::any_of(cachedClippings.begin(), cachedClippings.end(),
                             [this](const auto& clipping) { return clipping.spineIndex == currentSpineIndex; })) {
    const uint16_t pageIndex = static_cast<uint16_t>(section->currentPage);
    if (pageIndex < UINT16_MAX) {
      nextPageVisibleOffset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(pageIndex + 1));
    }
  }
  const auto highlightPlan = ClippingPageTools::buildHighlightPlan(
      renderer, *page, fontId, orientedMarginLeft, orientedMarginTop,
      renderer.getLineHeight(fontId, SETTINGS.getReaderLineCompression()), cachedClippings,
      static_cast<uint16_t>(currentSpineIndex), static_cast<uint16_t>(section ? section->currentPage : 0),
      page->visibleTextOffset, nextPageVisibleOffset);

  const bool pageHasImagesNeedingDecode = pageHasImages && page->hasImagesNeedingDecode();
  const bool manualRefreshPending = forcedRefreshPending;
  forcedRefreshPending = false;
  const bool cleanImageBasePending = manualRefreshPending || pagesUntilFullRefresh <= 1;
  const bool needsTextGrayscale = SETTINGS.textAntiAliasing;
  const bool needsAnyGrayscale = needsTextGrayscale || pageHasImages;
  bool forceAdaptiveStrip = false;
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
  forceAdaptiveStrip = render_lab::maxGrayscaleStripRows() > 0;
#endif
  // On PSRAM devices, decode every text glyph once and emit the B/W base plus
  // both gray selector planes together. Image pages retain their established
  // renderer because their grayscale comes from the image decoder, not glyph
  // coverage. C3 and allocation failures fall back to the strip/copy paths.
  const bool capturedTextGrayscale =
      needsTextGrayscale && !pageHasImages && !forceAdaptiveStrip && renderer.beginTextGrayscaleCapture();
  const bool tiledGrayscale = needsAnyGrayscale && !capturedTextGrayscale && renderer.supportsStripGrayscale();
  // Paper Mono only (no other panel combines): defer the B/W base activation so
  // the gray planes join it in a single waveform. Displaying the base
  // separately makes the gray pass re-drive the whole text body — a visible
  // flash on every AA page.
  const bool combinedGrayscaleBase = tiledGrayscale && !pageHasImages && renderer.combinesGrayscaleBase();
  const bool overlapRefresh = tiledGrayscale && renderer.supportsAsyncRefresh() && !pageHasImages;
  auto renderGrayscalePass = [&]() {
    if (needsTextGrayscale) {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    } else {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    }
    highlightPlan.clearGrayscale(renderer);
  };

  if (pageHasImagesNeedingDecode) {
    page->renderWithImagePlaceholders(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    renderStatusBar();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    renderer.clearScreen();
  }

  renderer.setCrispMonochromeText(!needsTextGrayscale);
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  if (capturedTextGrayscale) renderer.endTextGrayscaleCapture();
  renderer.setCrispMonochromeText(false);
  renderStatusBar();
  highlightPlan.drawUnderline(renderer);

  if (capturedTextGrayscale && highlightPlan.count > 0) {
    const int gh = renderer.getDisplayHeight();
    const auto clearHighlightFromPlane = [&](uint8_t* plane, const GfxRenderer::RenderMode mode) {
      renderer.setRenderMode(mode);
      renderer.beginStripTarget(plane, 0, gh);
      highlightPlan.clearGrayscale(renderer);
      renderer.endStripTarget();
    };
    clearHighlightFromPlane(renderer.capturedTextGrayscaleLsb(), GfxRenderer::GRAYSCALE_LSB);
    clearHighlightFromPlane(renderer.capturedTextGrayscaleMsb(), GfxRenderer::GRAYSCALE_MSB);
    renderer.setRenderMode(GfxRenderer::BW);
  }
  const auto tBwRender = millis();

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
  render_lab::beginFrame(renderer);
#endif

  if (pageHasImages) {
    // Image pages use one base refresh before the grayscale pass. FAST leaves
    // the panel receptive to the gray waveform; pending cleanup still honors
    // the scheduled/manual HALF refresh.
    renderer.displayBuffer(cleanImageBasePending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    pagesUntilFullRefresh = 1;
  } else if (combinedGrayscaleBase) {
    // Stash the base without activating; displayGrayBuffer() below commits
    // base + grays as one waveform.
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, overlapRefresh);
  }
  const auto tDisplay = millis();
  const uint32_t grayscaleFreeBefore = ESP.getFreeHeap();
  const uint32_t grayscaleLargestBefore = ESP.getMaxAllocHeap();

  if (capturedTextGrayscale) {
    ReaderUtils::prepareGrayscalePlanes(renderer, true, false);
    const auto tWait = millis();

    renderer.copyCapturedTextGrayscaleBuffers();
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
    render_lab::captureGrayscalePlaneStrip(true, renderer.capturedTextGrayscaleLsb(), 0, renderer.getDisplayHeight(),
                                           renderer.getDisplayWidthBytes());
    render_lab::captureGrayscalePlaneStrip(false, renderer.capturedTextGrayscaleMsb(), 0, renderer.getDisplayHeight(),
                                           renderer.getDisplayWidthBytes());
#endif
    const auto tGrayWrite = millis();

    renderer.displayGrayBuffer();
    const auto tGrayDisplay = millis();
    renderer.cleanupGrayscaleWithFrameBuffer();
    const auto tEnd = millis();

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
    render_lab::recordGrayscaleOutcome(true, "single-pass-text", renderer.getDisplayHeight(), "none");
#endif
    LOG_DBG("ERS",
            "Page render (single-pass text AA): prewarm=%lums bw_and_gray_render=%lums display=%lums wait=%lums "
            "gray_write=%lums gray_display=%lums cleanup=%lums total=%lums plane_bytes=%u free=%u largest=%u",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tWait - tDisplay, tGrayWrite - tWait,
            tGrayDisplay - tGrayWrite, tEnd - tGrayDisplay, tEnd - t0, static_cast<unsigned>(renderer.getBufferSize()),
            static_cast<unsigned>(grayscaleFreeBefore), static_cast<unsigned>(grayscaleLargestBefore));
  } else if (tiledGrayscale) {
    constexpr int DEFAULT_STRIP_ROWS = adaptive_grayscale_strip::DEFAULT_ROWS;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();
    const size_t planeBytes = static_cast<size_t>(gwBytes) * gh;

    auto renderPlaneToBuffer = [&](const bool lsbPlane, uint8_t* buf) {
      renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < gh; y += DEFAULT_STRIP_ROWS) {
        const int rows = (gh - y < DEFAULT_STRIP_ROWS) ? (gh - y) : DEFAULT_STRIP_ROWS;
        renderer.beginStripTarget(buf + static_cast<size_t>(y) * gwBytes, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
      }
    };

    constexpr size_t PLANE_BUF_HEADROOM = 60000;
    constexpr size_t PLANE_BUF_MAX_ALLOC_RESERVE = 16 * 1024;
    const auto planeBufFits = [planeBytes] {
      return ESP.getFreeHeap() >= planeBytes + PLANE_BUF_HEADROOM &&
             ESP.getMaxAllocHeap() >= planeBytes + PLANE_BUF_MAX_ALLOC_RESERVE;
    };
    auto lsbPlaneBuf =
        (overlapRefresh && !forceAdaptiveStrip && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;
    auto msbPlaneBuf = (lsbPlaneBuf && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;

    if (lsbPlaneBuf) {
      renderPlaneToBuffer(true, lsbPlaneBuf.get());
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      const auto tGrayRender = millis();

      // X3 vendor AA expects a gentle settle pass between the B/W base and the
      // gray selectors. Skipping it made the weak edge waveform depend on
      // whether this page happened to use FAST or periodic HALF, which showed
      // up as alternating soft/dotted and crisp glyphs. The hook is a no-op on
      // X4/other panels whose driver does not require this pass.
      ReaderUtils::prepareGrayscalePlanes(renderer, true, combinedGrayscaleBase);
      const auto tWait = millis();

      renderer.writeGrayscalePlaneStrip(true, lsbPlaneBuf.get(), 0, gh);
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
      render_lab::captureGrayscalePlaneStrip(true, lsbPlaneBuf.get(), 0, gh, gwBytes);
#endif
      if (msbPlaneBuf) {
        renderer.writeGrayscalePlaneStrip(false, msbPlaneBuf.get(), 0, gh);
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
        render_lab::captureGrayscalePlaneStrip(false, msbPlaneBuf.get(), 0, gh, gwBytes);
#endif
      } else {
        renderPlaneToBuffer(false, lsbPlaneBuf.get());
        renderer.writeGrayscalePlaneStrip(false, lsbPlaneBuf.get(), 0, gh);
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
        render_lab::captureGrayscalePlaneStrip(false, lsbPlaneBuf.get(), 0, gh, gwBytes);
#endif
      }
      const auto tGrayWrite = millis();

      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();

      renderer.cleanupGrayscaleWithFrameBuffer();
      const auto tEnd = millis();

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
      render_lab::recordGrayscaleOutcome(true, "buffered-planes", DEFAULT_STRIP_ROWS, "none");
#endif

      LOG_DBG("ERS",
              "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
              "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums "
              "(planes buffered: %d free=%u largest=%u)",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayRender - tDisplay, tWait - tGrayRender,
              tGrayWrite - tWait, tGrayDisplay - tGrayWrite, tEnd - tGrayDisplay, tEnd - t0, msbPlaneBuf ? 2 : 1,
              static_cast<unsigned>(grayscaleFreeBefore), static_cast<unsigned>(grayscaleLargestBefore));
    } else {
      uint16_t stripRows = 0;
      auto scratch = adaptive_grayscale_strip::allocate(
          static_cast<size_t>(gwBytes), stripRows, [gwBytes](const size_t bytes) -> std::unique_ptr<uint8_t[]> {
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
            const int forcedRows = render_lab::maxGrayscaleStripRows();
            if (forcedRows > 0 && bytes > static_cast<size_t>(gwBytes) * forcedRows) return nullptr;
#endif
            return makeUniqueNoThrow<uint8_t[]>(bytes);
          });
      ReaderUtils::prepareGrayscalePlanes(renderer, scratch != nullptr, combinedGrayscaleBase);
      if (!scratch) {
        LOG_ERR("ERS", "Grayscale applied=0 path=strip reason=scratch-oom free=%u largest=%u min_rows=%u min_bytes=%u",
                static_cast<unsigned>(grayscaleFreeBefore), static_cast<unsigned>(grayscaleLargestBefore),
                static_cast<unsigned>(adaptive_grayscale_strip::ROW_CANDIDATES.back()),
                static_cast<unsigned>(gwBytes * adaptive_grayscale_strip::ROW_CANDIDATES.back()));
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
        render_lab::recordGrayscaleOutcome(false, "strip", 0, "scratch-oom");
#endif
        if (overlapRefresh || combinedGrayscaleBase) {
          // The BW refresh ran the shadow-free async path, so controller RAM's
          // differential baseline was never rebuilt. Even with AA skipped it must
          // be re-synced from the intact BW framebuffer, or the next differential
          // update diffs against stale contents. On the combined-base path the
          // base activation is still deferred; this cleanup commits it so the
          // page reaches the panel even without its grays.
          renderer.cleanupGrayscaleWithFrameBuffer();
        }
      } else {
        if (stripRows < adaptive_grayscale_strip::DEFAULT_ROWS) {
          LOG_INF("ERS", "Grayscale applied=1 path=strip rows=%u bytes=%u free=%u largest=%u degraded_memory=1",
                  static_cast<unsigned>(stripRows), static_cast<unsigned>(gwBytes * stripRows),
                  static_cast<unsigned>(grayscaleFreeBefore), static_cast<unsigned>(grayscaleLargestBefore));
        } else {
          LOG_DBG("ERS", "Grayscale applied=1 path=strip rows=%u bytes=%u free=%u largest=%u",
                  static_cast<unsigned>(stripRows), static_cast<unsigned>(gwBytes * stripRows),
                  static_cast<unsigned>(grayscaleFreeBefore), static_cast<unsigned>(grayscaleLargestBefore));
        }
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        for (int y = 0; y < gh; y += stripRows) {
          const int rows = (gh - y < stripRows) ? (gh - y) : stripRows;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(true, scratch.get(), y, rows);
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
          render_lab::captureGrayscalePlaneStrip(true, scratch.get(), y, rows, gwBytes);
#endif
        }
        const auto tGrayLsb = millis();

        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        for (int y = 0; y < gh; y += stripRows) {
          const int rows = (gh - y < stripRows) ? (gh - y) : stripRows;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(false, scratch.get(), y, rows);
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
          render_lab::captureGrayscalePlaneStrip(false, scratch.get(), y, rows, gwBytes);
#endif
        }
        const auto tGrayMsb = millis();

        renderer.setRenderMode(GfxRenderer::BW);
        renderer.displayGrayBuffer();
        const auto tGrayDisplay = millis();

        renderer.cleanupGrayscaleWithFrameBuffer();
        const auto tCleanup = millis();

        const auto tEnd = millis();
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
        render_lab::recordGrayscaleOutcome(true, "strip", stripRows, "none");
#endif
        LOG_DBG("ERS",
                "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums rows=%u free=%u largest=%u",
                tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayLsb - tDisplay, tGrayMsb - tGrayLsb,
                tGrayDisplay - tGrayMsb, tCleanup - tGrayDisplay, tEnd - t0, static_cast<unsigned>(stripRows),
                static_cast<unsigned>(grayscaleFreeBefore), static_cast<unsigned>(grayscaleLargestBefore));
      }
    }
  } else {
    if (needsAnyGrayscale) {
      if (!renderer.storeBwBuffer()) {
        LOG_ERR("ERS", "Grayscale applied=0 path=framebuffer-copy reason=bw-store-oom free=%u largest=%u",
                static_cast<unsigned>(grayscaleFreeBefore), static_cast<unsigned>(grayscaleLargestBefore));
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
        render_lab::recordGrayscaleOutcome(false, "framebuffer-copy", 0, "bw-store-oom");
        render_lab::recordTimings(tPrewarm - t0, tBwRender - tPrewarm, millis() - t0);
#endif
        return;
      }
      const auto tBwStore = millis();

      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderGrayscalePass();
      renderer.copyGrayscaleLsbBuffers();
      const auto tGrayLsb = millis();

      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderGrayscalePass();
      renderer.copyGrayscaleMsbBuffers();
      const auto tGrayMsb = millis();

      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.restoreBwBuffer();
      const auto tBwRestore = millis();

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
      render_lab::recordGrayscaleOutcome(true, "framebuffer-copy", 0, "none");
#endif

      const auto tEnd = millis();
      LOG_DBG("ERS",
              "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
              "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
              tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
    } else {
      const auto tEnd = millis();
      LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
              tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
    }
  }

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
  render_lab::recordTimings(tPrewarm - t0, tBwRender - tPrewarm, millis() - t0);
#endif
}

vns_reference::Position EpubReaderActivity::currentVnsReferencePosition() const {
  uint32_t visibleOffset = currentPageVisibleOffset.value_or(0);
  if (!currentPageVisibleOffset.has_value() && section && section->currentPage >= 0) {
    visibleOffset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage)).value_or(0);
  }
  const size_t cumulative = epub && currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  return vns_reference::make(vnsContentSignature, static_cast<uint16_t>(std::max(currentSpineIndex, 0)),
                             static_cast<uint32_t>(std::min<size_t>(cumulative, UINT32_MAX)), visibleOffset);
}

void EpubReaderActivity::renderStatusBar() const {
  const int currentPage = section ? section->currentPage + 1 : 1;
  const float pageCount = section ? section->estimatedTotalPages() : 1;
  const float sectionChapterProg = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) : 0;
  const float bookProgress = epub ? (epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100) : 0;

  std::string title;
  int textYOffset = 0;
  const auto sb = SETTINGS.statusBarSpec();

  if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + autoTurnIntervalLabel(bookReaderSettings.autoPageTurnSeconds);
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    if (epub) {
      const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
      if (tocIndex != -1) {
        const auto tocItem = epub->getTocItem(tocIndex);
        title = tocItem.title;
      }
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub ? epub->getTitle() : "";
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, true, currentPageBookmarked,
                    section ? section->isBuilding() : false);
}

// ---------------------------------------------------------------------------
// Toolbar reader menu
// ---------------------------------------------------------------------------

namespace {
constexpr StrId kTextRowNames[] = {StrId::STR_FONT, StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING,
                                   StrId::STR_PARA_ALIGNMENT, StrId::STR_FOCUS_READING};
constexpr StrId kSpacingIds[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId kAlignIds[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                               StrId::STR_BOOK_S_STYLE};
constexpr int kTextRowCount = static_cast<int>(std::size(kTextRowNames));
static_assert(std::size(kSpacingIds) == CrossPointSettings::LINE_COMPRESSION_COUNT, "line spacing labels");
static_assert(std::size(kAlignIds) == CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, "alignment labels");
}  // namespace

bool EpubReaderActivity::usesToolbarMenu() const {
  return mappedInput.hasTouch() && SETTINGS.readerMenuStyle == CrossPointSettings::READER_MENU_TOOLBAR;
}

std::string EpubReaderActivity::currentChapterTitle() const {
  if (!epub) return "";
  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return tr(STR_UNNAMED);
}

std::string EpubReaderActivity::textRowName(int row) const {
  return row >= 0 && row < kTextRowCount ? I18N.get(kTextRowNames[row]) : "";
}

std::string EpubReaderActivity::textRowValue(int row) const {
  static constexpr StrId kFamily[] = {StrId::STR_SOURCE_SERIF, StrId::STR_SOURCE_SANS};
  switch (row) {
    case 0:
      if (SETTINGS.sdFontFamilyName[0] != '\0') return SETTINGS.sdFontFamilyName;
      return I18N.get(kFamily[SETTINGS.fontFamily % CrossPointSettings::FONT_FAMILY_COUNT]);
    case 1:
      return std::to_string(SETTINGS.fontPointSize) + " pt";
    case 2:
      return I18N.get(kSpacingIds[SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT]);
    case 3:
      return I18N.get(kAlignIds[SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT]);
    case 4:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

// Live apply: persist, re-paginate, and let renderBook() redraw the page with
// the open panel back on top -- the book itself is the preview.
void EpubReaderActivity::applyTextSettingLive() {
  applyReaderTextSettings();
  discardOverlayPage();  // the stored page is laid out with the old settings
  requestUpdate();
}

// Settings-style option pickers for the Text panel's enum rows. Every
// selection applies immediately to the page under the sheet.
void EpubReaderActivity::showTextRowPopup(const int row) {
  switch (row) {
    case 1: {
      // The point sizes the active family actually ships.
      const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
      if (sizes.empty()) return;
      std::vector<std::string> labels;
      labels.reserve(sizes.size());
      for (const uint8_t size : sizes) labels.push_back(std::to_string(size) + " pt");
      const uint8_t cur = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
      int curIdx = 0;
      for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i] == cur) curIdx = static_cast<int>(i);
      }
      overlayPopup.show(StrId::STR_FONT_SIZE, labels, curIdx, [this, sizes](int idx) {
        if (idx < 0 || idx >= static_cast<int>(sizes.size())) return;
        SETTINGS.fontPointSize = sizes[idx];
        applyTextSettingLive();
      });
      break;
    }
    case 2:
      overlayPopup.show(StrId::STR_LINE_SPACING, kSpacingIds, static_cast<int>(std::size(kSpacingIds)),
                        SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT, [this](int idx) {
                          SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    case 3:
      overlayPopup.show(StrId::STR_PARA_ALIGNMENT, kAlignIds, static_cast<int>(std::size(kAlignIds)),
                        SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, [this](int idx) {
                          SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    default:
      return;
  }
  paintOverlayPopup();
}

void EpubReaderActivity::discardOverlayPage() {
  if (!overlayPageStored) return;
  renderer.discardStoredBwBuffer();
  overlayPageStored = false;
}

void EpubReaderActivity::openOverlay(Overlay target) {
  const Overlay previous = overlay;
  if (previous == Overlay::None) pauseReadingStats(false);
  overlay = target;
  if (!toolbarUi) toolbarUi = std::make_unique<ReaderToolbarUi>(renderer);
  if (previous == Overlay::None) toolbarUi->begin();
  // Buttons show a cursor from the start; touch boards only once a button moves it.
  panelCursorShown = !mappedInput.hasTouch();
  switch (target) {
    case Overlay::Toolbar:
      focusedTool = 0;
      break;
    case Overlay::Contents:
      focusedTool = 2;
      panelIndex = std::max(0, epub->getTocIndexForSpineIndex(currentSpineIndex));
      // Fresh viewport opening on the current chapter, cursor shown or not.
      toolbarUi->nav().reset(panelIndex);
      toolbarUi->nav().top = panelIndex;
      break;
    case Overlay::Text:
      focusedTool = 3;
      panelIndex = 0;
      toolbarUi->nav().reset();
      break;
    case Overlay::More:
      focusedTool = 4;
      panelIndex = 0;
      buildMoreActions();
      toolbarUi->nav().reset();
      break;
    default:
      break;
  }
  panelHoldJumped = false;

  // The page is already on screen and still in the framebuffer, so paint the
  // chrome straight onto it and push one refresh. requestUpdate() would
  // re-render the whole page first: slow, and visibly wrong, since that repaint
  // lands before the overlay does.
  //
  // Refresh mode: FAST for every overlay paint, first open included. The AA
  // pass only grays glyph edges, and residue a FAST differential leaves under
  // the sheet has not shown in practice; it also self-heals on the
  // Xteink-class panels, whose close path re-renders the page. If text or
  // images ever visibly ghost through the chrome, restore a HALF cleanup on
  // the first open (see #2190 for the mechanism).
  if (section) {
    // Serialize against the render task: renderBook may be mid-page (status
    // bar included) in the shared framebuffer, and painting the chrome from
    // the loop task at the same time interleaves the two frames.
    RenderLock lock;
    if (previous == Overlay::None) {
      // Snapshot the clean page so stepping back from a panel to the toolbar
      // (and closing, where supported) can restore it without a re-render.
      overlayPageStored = renderer.storeBwBuffer();
    } else if (overlayPageStored) {
      // Overlay -> overlay: wipe the previous chrome (toolbar header, sheet,
      // progress row) back to the clean page so none of it shows around or
      // through the new sheet; re-store for the next transition. No baseline
      // resync: the glass still shows the old chrome, and the differential
      // must keep diffing against it to erase it.
      renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      overlayPageStored = renderer.storeBwBuffer();
    }
    renderOverlay();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    requestUpdate();  // no page yet: renderBook() draws the overlay once it is
  }
}

// Close the overlay back to the reading page. Boards without the Xteink
// grayscale-AA pass restore the page snapshot and push one FAST refresh -- no
// re-render, no flash; Xteink boards re-render to restore the AA planes.
void EpubReaderActivity::closeOverlayToPage() {
  overlay = Overlay::None;
  overlayPopup.dismiss();  // an option picker cannot outlive its panel
  toolbarUi.reset();       // ~1 KB of interaction table + props, only needed while open
  if (!xteinkClassPanel() && overlayPageStored) {
    RenderLock lock;  // the render task shares the framebuffer
    // No baseline resync: the glass shows the chrome, and erasing it needs
    // the differential to keep diffing against the last pushed frame.
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = false;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    resumeReadingStats();
    return;
  }
  discardOverlayPage();
  requestUpdate();  // redraw the clean page
}

void EpubReaderActivity::renderOverlay() {
  if (!epub || !section || !toolbarUi) return;

  ReaderToolbarUi::Model model;
  // The toolbar's tool pill is the button-navigation cursor: tap-first (same
  // convention as the panel lists), it only shows once a button has moved it.
  // Panels override below: there the pill marks the open panel on every board.
  model.activeTool = (overlay == Overlay::Toolbar && !panelCursorShown) ? -1 : focusedTool;
  // Strings the model points at live here until render() returns.
  std::string chapterTitle, pageInfo;

  if (overlay == Overlay::Toolbar) {
    chapterTitle = currentChapterTitle();
    const int pageCount = section->estimatedTotalPages();
    const float chapterProgress =
        pageCount > 0 ? static_cast<float>(section->currentPage + 1) / static_cast<float>(pageCount) : 0.0f;
    const float bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress);
    pageInfo = std::to_string(section->currentPage + 1) + "/" + std::to_string(pageCount) + "   " +
               std::to_string(clampPercent(static_cast<int>(bookProgress * 100.0f + 0.5f))) + "%";
    model.chapterTitle = chapterTitle.c_str();
    model.pageInfo = pageInfo.c_str();
    model.progressPermille = static_cast<int>(bookProgress * 1000.0f + 0.5f);
    toolbarUi->setModel(model);
    toolbarUi->render();
    return;
  }

  // Panels (Contents / Text / More): a bottom sheet over the page + button hints.
  model.panel = true;
  if (!mappedInput.hasTouch()) {
    model.denseRows = true;
  }
  // Tap-first: the cursor is only drawn once a button has moved it, so a
  // tapped row does not stay inverted after its action.
  model.selectedIndex = panelCursorShown ? panelIndex : -1;
  if (overlay == Overlay::Contents) {
    model.panelTitle = tr(STR_TOOL_CONTENTS);
    model.itemCount = epub->getTocItemsCount();
    model.rowText = [this](int i) {
      const auto item = epub->getTocItem(i);
      const int depth = item.level > 1 ? (item.level - 1) * 2 : 0;
      return std::string(depth, ' ') + item.title;
    };
  } else if (overlay == Overlay::Text) {
    model.panelTitle = tr(STR_TOOL_TEXT);
    model.itemCount = kTextRowCount;
    model.rowText = [this](int i) { return textRowName(i); };
    model.rowValue = [this](int i) { return textRowValue(i); };
    model.rowChevron = [](int i) { return i == 0; };
  } else {
    model.panelTitle = tr(STR_TOOL_MORE);
    model.itemCount = static_cast<int>(moreItems.size());
    model.rowText = [this](int i) { return moreRowName(i); };
    model.rowValue = [this](int i) { return moreRowValue(i); };
    model.rowChevron = [this](int i) {
      return i >= 0 && i < static_cast<int>(moreItems.size()) &&
             EpubReaderMenuActivity::opensChildScreen(moreItems[i].action);
    };
  }
  toolbarUi->setModel(model);
  toolbarUi->render();

  if (!mappedInput.hasTouch()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

void EpubReaderActivity::handleOverlayInput() {
  if (!toolbarUi) return;

  // A modal option picker over the panel owns all input while open.
  if (overlayPopup.isActive()) {
    overlayPopup.handleInput(mappedInput, [this] {
      if (overlayPopup.isActive()) {
        paintOverlayPopup();  // highlight moved
        return;
      }
      // Dismissed or selected: erase the dialog -- clean page back, then the
      // panel over it (the dialog can overhang the sheet onto the page).
      RenderLock lock;
      if (overlayPageStored) {
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
        renderOverlay();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      } else {
        requestUpdate();
      }
    });
    return;
  }
  const auto fastRedraw = [this] {
    RenderLock lock;  // the render task shares the framebuffer
    renderOverlay();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  };

  // Jump to another spine item (chapter scrub). The overlay stays up and is
  // re-drawn over the new page by renderBook().
  const auto gotoSpine = [this](int target) {
    const int spineCount = epub->getSpineItemsCount();
    target = std::clamp(target, 0, spineCount - 1);
    if (target != currentSpineIndex) {
      RenderLock lock;
      clearDeferredReposition();
      nextPageNumber = 0;
      currentSpineIndex = target;
      section.reset();
    }
    requestUpdate();
  };
  const auto toolOverlay = [](int tool) {
    return tool == 2 ? Overlay::Contents : (tool == 3 ? Overlay::Text : Overlay::More);
  };
  const auto activateTool = [this, &toolOverlay](int tool) {
    focusedTool = std::clamp(tool, 0, 4);
    if (focusedTool >= 2) {
      openOverlay(toolOverlay(focusedTool));
      return;
    }

    // Lookup and History are immediate tools, not panels. Tear down the
    // overlay first so their child activities own a clean reader page and
    // resume the reader timer; the launched activity pauses it again.
    overlay = Overlay::None;
    overlayPopup.dismiss();
    toolbarUi.reset();
    discardOverlayPage();
    resumeReadingStats();
    onReaderMenuConfirm(focusedTool == 0 ? EpubReaderMenuActivity::MenuAction::DICTIONARY
                                         : EpubReaderMenuActivity::MenuAction::LOOKUP_HISTORY);
  };

  // Touch first: FreeInkUI routes the frame against the tap targets the last
  // render registered and hands back the action it mapped to.
  const auto routed = toolbarUi->route(mappedInput);

  // --- Toolbar ---
  if (overlay == Overlay::Toolbar) {
    switch (routed.event) {
      case ReaderToolbarUi::Event::Dismiss:
        closeOverlayToPage();
        return;
      case ReaderToolbarUi::Event::Tool:
        activateTool(routed.value);
        return;
      case ReaderToolbarUi::Event::PrevChapter:
        gotoSpine(currentSpineIndex - 1);
        return;
      case ReaderToolbarUi::Event::NextChapter:
        gotoSpine(currentSpineIndex + 1);
        return;
      case ReaderToolbarUi::Event::Scrub:
        gotoSpine(static_cast<int>((static_cast<float>(routed.permille) / 1000.0f) *
                                       static_cast<float>(epub->getSpineItemsCount() - 1) +
                                   0.5f));
        return;
      default:
        break;
    }
    if (routed.routed) return;  // a touch frame the chrome consumed (or dead space)

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeOverlayToPage();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      focusedTool = (focusedTool + 4) % 5;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      focusedTool = (focusedTool + 1) % 5;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateTool(focusedTool);
      return;
    }
    const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
    const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (prev || next) {
      gotoSpine(currentSpineIndex + (next ? 1 : -1));
    }
    return;
  }

  // --- Panels (Contents / Text / More) ---
  const int count = overlay == Overlay::Contents ? epub->getTocItemsCount()
                    : overlay == Overlay::Text   ? kTextRowCount
                                                 : static_cast<int>(moreItems.size());
  const int pageRows = std::max(1, toolbarUi->visibleRows());

  // Activate the highlighted row: change a value / jump to a chapter / run an
  // action. Shared by the Confirm button and a row tap.
  const auto activateRow = [this, count, &fastRedraw] {
    if (panelIndex < 0 || panelIndex >= count) return;
    if (overlay == Overlay::Text) {
      if (panelIndex == 0) {
        // Full font picker (built-in + SD fonts, live preview) -- the same
        // screen Settings uses; a popup cannot scroll a long font list.
        overlay = Overlay::None;
        overlayPopup.dismiss();
        discardOverlayPage();
        startActivityForResult(std::make_unique<TextSettingsActivity>(
                                   renderer, mappedInput, &sdFontSystem.registry(), TextSettingsActivity::Tab::Family,
                                   false, [this] { saveBookReaderSettings(); }, &bookReaderSettings.wordSpacing,
                                   &bookReaderSettings.repairParagraphIndent),
                               [this](const ActivityResult&) {
                                 applyReaderTextSettings();
                                 overlay = Overlay::Text;  // back to the Text panel
                                 panelIndex = 0;
                                 if (toolbarUi) toolbarUi->begin();  // the picker drew its own FUI screen
                                 requestUpdate();                    // re-render page + Text panel
                               });
      } else if (panelIndex == 4) {
        // Focus Reading is a genuine on/off: a tap toggles and applies live.
        SETTINGS.focusReadingEnabled = SETTINGS.focusReadingEnabled ? 0 : 1;
        applyTextSettingLive();
      } else {
        // Enum rows open the Settings-style option picker.
        showTextRowPopup(panelIndex);
      }
    } else if (overlay == Overlay::Contents) {
      const auto item = epub->getTocItem(panelIndex);
      if (item.spineIndex != -1) {
        RenderLock lock;
        clearDeferredReposition();
        currentSpineIndex = item.spineIndex;
        pendingAnchor = item.anchor;
        nextPageNumber = 0;
        section.reset();
      }
      overlay = Overlay::None;
      discardOverlayPage();
      requestUpdate();
    } else if (overlay == Overlay::More) {
      activateMoreRow(panelIndex);
    }
  };

  // Steps up to the toolbar -- the Back button and a tap on the page above
  // the sheet.
  const auto dismissPanel = [this, &fastRedraw] {
    overlay = Overlay::Toolbar;
    // Restore the snapshotted page under the toolbar instead of re-rendering
    // it (2+ refreshes -> one FAST). Re-store right away so another panel
    // round-trip can restore again.
    if (overlayPageStored) {
      {
        RenderLock lock;  // the render task shares the framebuffer
        // No baseline resync: the glass shows the panel, and erasing it needs
        // the differential to keep diffing against the last pushed frame.
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
      }
      fastRedraw();  // takes its own RenderLock
      return;
    }
    requestUpdate();
  };

  // Pages the list by one screen of rows through the nav (measured page size,
  // no-op at the ends). A shown cursor rides along so the buttons continue
  // from what is visible; on touch boards only the viewport moves.
  const auto pageList = [this, count, pageRows, &fastRedraw](int direction) {
    if (count <= 0) return;
    const bool moved = toolbarUi->nav().scrollBy(direction * pageRows, count);
    if (panelCursorShown) {
      panelIndex = std::clamp(panelIndex + direction * pageRows, 0, count - 1);
      fastRedraw();
      return;
    }
    if (moved) fastRedraw();
  };

  switch (routed.event) {
    case ReaderToolbarUi::Event::Dismiss:
      dismissPanel();
      return;
    case ReaderToolbarUi::Event::Tool: {
      // Sheet-bottom switcher: launch quick dictionary tools or hop directly
      // to another panel.
      if (routed.value < 2 || toolOverlay(routed.value) != overlay) activateTool(routed.value);
      return;
    }
    case ReaderToolbarUi::Event::Row:
      // A tap on the right-edge strip pages the sheet instead (upper half =
      // previous page, lower half = next): swipes are unreliable on etched
      // glass, and a long contents list needs a fast way through.
      if (routed.x >= renderer.getScreenWidth() - 44) {
        pageList(routed.y >= renderer.getScreenHeight() - (renderer.getScreenHeight() * 62) / 200 ? 1 : -1);
        return;
      }
      panelIndex = routed.value;
      panelCursorShown = false;
      activateRow();
      return;
    default:
      break;
  }
  // Swipe up/down pages the list. Checked before the routed-frame return:
  // FUI routes every touch frame over the sheet, so a swipe's frames count as
  // routed (without dispatching -- too much travel for a tap) and the gesture
  // would otherwise never be seen.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    pageList(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return;
  }
  if (routed.routed) return;  // consumed by the chrome (title band, dead space)

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    dismissPanel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow();
    return;
  }

  // Up/Down (side) and Left/Right (front) move the cursor: a tap steps one
  // row, holding past PANEL_HOLD_MS jumps PANEL_HOLD_STEP rows in one go, which
  // is how you cross a hundreds-of-chapters contents list without a press per
  // row. The jump fires once on the hold and swallows the release that ends it,
  // so it never doubles up with the tap step.
  if (count > 0) {
    const bool up = mappedInput.isPressed(MappedInputManager::Button::Up) ||
                    mappedInput.isPressed(MappedInputManager::Button::Left);
    const bool down = mappedInput.isPressed(MappedInputManager::Button::Down) ||
                      mappedInput.isPressed(MappedInputManager::Button::Right);
    if (!panelHoldJumped && (up || down) && mappedInput.getHeldTime() >= PANEL_HOLD_MS) {
      const int step = down ? PANEL_HOLD_STEP : -PANEL_HOLD_STEP;
      panelIndex = std::clamp(panelIndex + step, 0, count - 1);
      panelHoldJumped = true;
      panelCursorShown = true;
      fastRedraw();
      return;
    }

    const bool releasedUp = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool releasedDown = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
    if (releasedUp || releasedDown) {
      if (!panelHoldJumped) {
        panelIndex = releasedUp ? ButtonNavigator::previousIndex(panelIndex, count)
                                : ButtonNavigator::nextIndex(panelIndex, count);
        panelCursorShown = true;
        fastRedraw();
      }
      panelHoldJumped = false;
    }
  }
}

// First paint of the option picker over the panel (and highlight repaints).
// The dialog draws over the current framebuffer without clearing; erasing it
// on dismissal is the popup gate's restore in handleOverlayInput().
void EpubReaderActivity::paintOverlayPopup() {
  RenderLock lock;
  overlayPopup.render(renderer);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubReaderActivity::applyReaderTextSettings() {
  bookReaderSettings.lastWorkingFallback = UINT8_MAX;
  bookReaderSettings.fallbackRenderSignature = 0;
  preferredRenderTrialActive = false;
  const EpubRenderMode preferredMode = static_cast<EpubRenderMode>(bookReaderSettings.preferredRenderMode);
  pendingWorkingFallback = activeRenderMode > preferredMode ? static_cast<uint8_t>(activeRenderMode) : UINT8_MAX;
  saveBookReaderSettings();
  // (Re)load or unload the selected SD-card font for the current family/size.
  // The reader otherwise only loads SD fonts on book open, so without this an
  // in-reader font change wouldn't take effect until re-opening the book.
  sdFontSystem.ensureLoaded(renderer);
  RenderLock lock;
  if (section) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->estimatedTotalPages();
    cachedChapterPageCountEstimated = section->isBuilding() || section->isPartial();
    nextPageNumber = section->currentPage;
  }
  section.reset();  // force re-pagination with the new settings
}

// The More panel carries everything the classic list menu offers except the
// two entries that have their own tool (chapters -> Contents, text -> Text).
void EpubReaderActivity::buildMoreActions() {
  using MA = EpubReaderMenuActivity::MenuAction;
  EpubReaderMenuActivity::buildMenuItems(
      moreItems, !currentPageFootnotes.empty(), !cachedBookmarks.empty(),
      activeRenderMode > static_cast<EpubRenderMode>(bookReaderSettings.preferredRenderMode));
  moreItems.erase(std::remove_if(moreItems.begin(), moreItems.end(),
                                 [](const auto& item) {
                                   return item.action == MA::SELECT_CHAPTER || item.action == MA::TEXT_SETTINGS;
                                 }),
                  moreItems.end());
}

std::string EpubReaderActivity::moreRowName(int row) const {
  return row >= 0 && row < static_cast<int>(moreItems.size()) ? I18N.get(moreItems[row].labelId) : "";
}

std::string EpubReaderActivity::moreRowValue(int row) const {
  using MA = EpubReaderMenuActivity::MenuAction;
  static constexpr StrId kOrient[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED,
                                      StrId::STR_LANDSCAPE_CCW};
  static_assert(std::size(kOrient) == CrossPointSettings::ORIENTATION_COUNT, "orientation labels");
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return "";
  switch (moreItems[row].action) {
    case MA::ROTATE_SCREEN:
      return I18N.get(kOrient[SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT]);
    case MA::AUTO_PAGE_TURN:
      return autoTurnIntervalLabel(bookReaderSettings.autoPageTurnSeconds);
    case MA::WORD_SPACING: {
      static constexpr StrId labels[] = {StrId::STR_SPACING_DEFAULT, StrId::STR_SPACING_MEDIUM,
                                         StrId::STR_SPACING_WIDE};
      return I18N.get(labels[std::min<uint8_t>(bookReaderSettings.wordSpacing, 2)]);
    }
    case MA::REPAIR_PARAGRAPH_INDENT:
      return I18N.get(bookReaderSettings.repairParagraphIndent ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    case MA::RENDER_MODE: {
      static constexpr StrId labels[] = {StrId::STR_RENDER_STANDARD, StrId::STR_RENDER_SIMPLIFIED,
                                         StrId::STR_RENDER_SAFE};
      return I18N.get(labels[std::min<uint8_t>(bookReaderSettings.preferredRenderMode, 2)]);
    }
    case MA::NIGHT_MODE:
      return SETTINGS.screenInverted ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case MA::FRONTLIGHT:
      return Frontlight.isOn() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

void EpubReaderActivity::activateMoreRow(int row) {
  using MA = EpubReaderMenuActivity::MenuAction;
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return;
  const auto action = moreItems[row].action;
  // In-place toggles keep the panel open and re-render the page beneath it.
  switch (action) {
    case MA::ROTATE_SCREEN: {
      static constexpr StrId kOrientIds[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                             StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
      static_assert(std::size(kOrientIds) == CrossPointSettings::ORIENTATION_COUNT, "orientation options");
      overlayPopup.show(StrId::STR_ORIENTATION, kOrientIds, static_cast<int>(std::size(kOrientIds)),
                        SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT, [this](int idx) {
                          if (idx == SETTINGS.orientation) return;
                          applyOrientation(static_cast<uint8_t>(idx));
                          // The stored page is laid out for the old orientation.
                          discardOverlayPage();
                          requestUpdate();
                        });
      paintOverlayPopup();
      return;
    }
    case MA::TRY_FULL_RENDER_QUALITY:
      applyExtendedReaderSettings(bookReaderSettings.wordSpacing, bookReaderSettings.repairParagraphIndent != 0,
                                  bookReaderSettings.preferredRenderMode, true);
      discardOverlayPage();
      buildMoreActions();
      requestUpdate();
      return;
    case MA::AUTO_PAGE_TURN: {
      std::vector<std::string> labels;
      labels.reserve(std::size(AUTO_TURN_SECONDS));
      for (const uint16_t seconds : AUTO_TURN_SECONDS) labels.push_back(autoTurnIntervalLabel(seconds));
      overlayPopup.show(StrId::STR_AUTO_TURN_INTERVAL, labels, autoTurnOption,
                        [this](int idx) { toggleAutoPageTurn(AUTO_TURN_SECONDS[idx]); });
      paintOverlayPopup();
      return;
    }
    case MA::WORD_SPACING: {
      static constexpr StrId labels[] = {StrId::STR_SPACING_DEFAULT, StrId::STR_SPACING_MEDIUM,
                                         StrId::STR_SPACING_WIDE};
      overlayPopup.show(StrId::STR_WORD_SPACING, labels, static_cast<int>(std::size(labels)),
                        bookReaderSettings.wordSpacing, [this](int idx) {
                          applyExtendedReaderSettings(static_cast<uint8_t>(idx),
                                                      bookReaderSettings.repairParagraphIndent != 0,
                                                      bookReaderSettings.preferredRenderMode);
                          discardOverlayPage();
                        });
      paintOverlayPopup();
      return;
    }
    case MA::REPAIR_PARAGRAPH_INDENT:
      applyExtendedReaderSettings(bookReaderSettings.wordSpacing, bookReaderSettings.repairParagraphIndent == 0,
                                  bookReaderSettings.preferredRenderMode);
      discardOverlayPage();
      requestUpdate();
      return;
    case MA::RENDER_MODE: {
      static constexpr StrId labels[] = {StrId::STR_RENDER_STANDARD, StrId::STR_RENDER_SIMPLIFIED,
                                         StrId::STR_RENDER_SAFE};
      overlayPopup.show(StrId::STR_RENDER_MODE, labels, static_cast<int>(std::size(labels)),
                        bookReaderSettings.preferredRenderMode, [this](int idx) {
                          applyExtendedReaderSettings(bookReaderSettings.wordSpacing,
                                                      bookReaderSettings.repairParagraphIndent != 0,
                                                      static_cast<uint8_t>(idx));
                          discardOverlayPage();
                        });
      paintOverlayPopup();
      return;
    }
    case MA::NIGHT_MODE:
      SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      discardOverlayPage();
      requestUpdate();
      return;
    case MA::FRONTLIGHT: {
      const bool lightOn = !Frontlight.isOn();
      Frontlight.setOn(lightOn);
      SETTINGS.frontlightOn = lightOn ? 1 : 0;
      SETTINGS.saveToFile();
      {
        RenderLock lock;  // the render task shares the framebuffer
        renderOverlay();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      }
      return;
    }
    default:
      break;
  }
  // Leaf actions open their own screen / perform the action; close the overlay first.
  overlay = Overlay::None;
  discardOverlayPage();
  if (action == MA::TOGGLE_BOOKMARK) {
    // No child activity here to trigger the re-render the list menu relies on:
    // show the same confirmation popup the long-press path does.
    addBookmark();
    showBookmarkMessage = true;
    bookmarkMessageTime = millis();
    requestUpdate();
    return;
  }
  onReaderMenuConfirm(action);
  // Actions that neither open a screen nor leave the reader (a sync with no
  // credentials, say) would otherwise leave the closed panel on screen.
  if (action != MA::GO_HOME && action != MA::DELETE_CACHE) requestUpdate();
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  clearPendingManualTurns();
  if (!epub) return;

  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';
  int targetSpineIndex = sameFile ? currentSpineIndex : epub->resolveHrefToSpineIndex(hrefStr);

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;
    return;
  }

  {
    RenderLock lock;
    clearDeferredReposition();
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  clearPendingManualTurns();
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock;
    clearDeferredReposition();
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::loadCachedBookmarks() {
  cachedBookmarks.clear();
  if (cachedBookmarks.capacity() < initialBookmarkCacheCapacity) {
    cachedBookmarks.reserve(initialBookmarkCacheCapacity);
  }
  if (!epub) {
    currentPageBookmarked = false;
    return;
  }

  BookmarkFile::load(epub->getPath(), cachedBookmarks);
  updateBookmarkFlag();
}

void EpubReaderActivity::addBookmark() {
  if (!section || !epub) return;
  LOG_DBG("ERS", "Toggle bookmark at spine %d, page %d", currentSpineIndex, section ? section->currentPage : -1);
  int currentPage;
  int pageCount;
  {
    RenderLock lock;
    pageCount = section->estimatedTotalPages();
    currentPage = section->currentPage;
  }

  SavedProgressPosition progress = ProgressMapper::toSavedProgress(epub, getCurrentPosition());
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, currentPage, pageCount);

  const size_t bookmarkCountBeforeToggle = cachedBookmarks.size();
  cachedBookmarks.erase(std::remove_if(cachedBookmarks.begin(), cachedBookmarks.end(),
                                       [&](const BookmarkEntry& b) {
                                         return bookmarkMatchesProgress(b, currentSpineIndex, currentPage, pageCount,
                                                                        pageRange);
                                       }),
                        cachedBookmarks.end());
  if (cachedBookmarks.size() != bookmarkCountBeforeToggle) {
    bookmarkRemoved = true;
    currentPageBookmarked = false;
  } else {
    std::string pageText;
    if (currentPage >= 0 && currentPage < pageCount) {
      pageText = section->getTextFromSectionFile();
    }
    BookmarkEntry entry;
    entry.percentage = progress.percentage;
    entry.xpath = progress.xpath;
    entry.summary = BookmarkUtil::sanitizeBookmarkSummary(pageText);
    entry.computedSpineIndex = currentSpineIndex;
    entry.computedChapterPageCount = pageCount;
    entry.computedChapterProgress = currentPage;
    const std::optional<uint32_t> offset =
        currentPageVisibleOffset.has_value() ? currentPageVisibleOffset
        : (currentPage >= 0 && currentPage < section->pageCount)
            ? section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))
            : std::nullopt;
    if (offset.has_value()) {
      entry.visibleTextOffset = *offset;
      entry.hasVisibleTextOffset = true;
    }
    cachedBookmarks.insert(cachedBookmarks.begin(), entry);
    bookmarkRemoved = false;
    currentPageBookmarked = true;
  }

  if (!BookmarkFile::save(epub->getPath(), cachedBookmarks)) {
    LOG_ERR("ERS", "Failed to save bookmarks");
  } else if (!SavedItemsCatalog::updateBookmarks(epub->getPath(), epub->getTitle(), epub->getAuthor(),
                                                 cachedBookmarks.size())) {
    LOG_ERR("SAVED", "Could not update bookmark count in saved-items catalog");
  }
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || cachedBookmarks.empty()) {
    currentPageBookmarked = false;
    return;
  }
  const int pageCount = section->estimatedTotalPages();
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, section->currentPage, pageCount);
  currentPageBookmarked = std::any_of(cachedBookmarks.begin(), cachedBookmarks.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, currentSpineIndex, section->currentPage, pageCount, pageRange);
  });
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->estimatedTotalPages();
    if (epub && epub->getBookSize() > 0 && info.totalPages > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(info.totalPages);
      int pct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    if (const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))) {
      localPos.visibleTextOffset = *offset;
      localPos.hasVisibleTextOffset = true;
    }
  }
  if (paragraphIndex.has_value()) {
    localPos.paragraphIndex = *paragraphIndex;
    localPos.hasParagraphIndex = true;
  }
  return localPos;
}
