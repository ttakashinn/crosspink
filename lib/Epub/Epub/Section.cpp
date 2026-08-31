#include "Section.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <cstdlib>

#include "Epub/css/CssParser.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v28: text decoration bits now include line-through in serialized wordStyles.
// v29: TextBlock word data stored as one flat arena (offset table + NUL-terminated
// text blob) instead of length-prefixed strings and per-field arrays.
// v30: Arabic shaping changed both drawing and measurement (getTextAdvanceX now
//      measures the shaped visual text); cached word positions from v29 no longer
//      match what drawText renders.
// v32: ImageBlock serializes the book-internal source href after the cache path
//      (lazy extraction: images are header-probed at build time and extracted on
//      first render).
// v33: Support <ruby> and <rt> tags. Skip <rp> tags
// v34: Word gaps are only suppressed for tokens glued in the source, so spaces between
//      Hangul words survive again; ruby element boundaries carry the continuation flag
//      instead. Invalidates v33 caches, whose word positions have the spaces collapsed.

// v34: <br> handling changed layout — a <br> after text is now a margin-stripped
//      line break (browser-like) and only a <br> whose block stays empty injects
//      the scene-break gap, so cached pages laid out by older versions no longer
//      match. Keeps <br>-per-paragraph books (common CJK formatting) from
//      re-adding container spacing at every paragraph.
// v35: Persist a uint32_t visible-text start offset for every page.
// v36: Ruby and CJK justification layout changes invalidate cached word positions.
// v37: Footnote href records grew from 96 to 256 bytes.
// v38: Focus Reading line breaking changed — a visible hyphen/dash inside a word is now a
//      break opportunity, and hyphenation of a focus-split word considers the whole word
//      instead of only its regular-weight suffix. Pages cached by older versions were laid
//      out with the previous, more restrictive break set and no longer match.
// v39: Image top margin is clamped so a full-viewport-height image cannot
//      overflow the page bottom; older caches can hold placements that panels
//      with no bottom inset refuse to draw.
// v40: Ruby groups remain intact when a large text block is soft-flushed.
// v41: Simple HTML table rows are laid out as positioned columns instead of
//      flattened paragraphs with synthetic row/cell labels.
// v42: Two-part descendant selectors and page-break-before/after change layout.
// v43: Cache header records the automatic EPUB render mode.
// v44: Vietnamese-aware small caps changed glyph measurement and rendering.
// v45: Section payload validation and pixel-cache clipping were hardened.
// v46: CSS cascade now honors !important and rejects invalid declarations.
// v47: Per-book word spacing and paragraph-indent repair join the render spec.
// v48: Persist bounded internal-link hit rectangles with each page.
// v49: Persist the publisher page label resolved for each page.
constexpr uint8_t SECTION_FILE_VERSION = 49;
// Written into the version field while a build is in progress; patched to
// SECTION_FILE_VERSION only when the build is finalized. An abandoned /
// crash-interrupted .bin therefore carries version 0, which loadSectionFile rejects
// as unknown and clears -- so an incomplete file is never mistaken for a valid one.
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
// Written when a build is suspended partway (reader exited or device slept mid-build).
// The file carries valid pages 0..pageCount-1, all LUTs, and a trailer with the parse
// watermark (bytesConsumed, totalBytes) appended after the li LUT. loadSectionFile
// accepts it so a resume shows those pages instantly; the reader extends it by
// rebuilding in the background. Uses the same header layout as SECTION_FILE_VERSION,
// so finalized files are untouched by this feature; older firmware treats the sentinel
// as an unknown version and rebuilds, which is a safe downgrade.
// MUST change in lockstep with SECTION_FILE_VERSION: the sentinel IS the partial's
// format version, so a stale-format partial otherwise passes the header check and
// only fails (noisily, via the block-decode error path) when a page is loaded.
// Derived so the pairing can't be forgotten: 0xFE for v28, 0xFD for v29, ...
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = 0xFE - (SECTION_FILE_VERSION - 28);
// CrossInk uses a fallible geometric LUT rather than std::vector. Start smaller
// here because VNS keeps more per-book reader state resident: 256 entries cost
// 3 KB and cover ordinary chapters; unusually long spines grow explicitly and
// report LowMemory instead of aborting the firmware.
constexpr uint16_t INITIAL_SECTION_PAGE_LUT_ENTRIES = 256;
constexpr size_t SECTION_HTML_STREAM_CHUNK_SIZE = 8192;
constexpr size_t LOW_MEMORY_SECTION_HTML_STREAM_CHUNK_SIZE = 1024;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(bool) + sizeof(uint8_t) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
                                 sizeof(uint32_t);

SectionBuildFailure sectionFailureFromParser(const ChapterHtmlSlimParser::Failure failure) {
  switch (failure) {
    case ChapterHtmlSlimParser::Failure::LowMemory:
      return SectionBuildFailure::LowMemory;
    case ChapterHtmlSlimParser::Failure::Io:
      return SectionBuildFailure::Io;
    case ChapterHtmlSlimParser::Failure::InvalidContent:
      return SectionBuildFailure::InvalidContent;
    case ChapterHtmlSlimParser::Failure::None:
    default:
      return SectionBuildFailure::InvalidContent;
  }
}

template <typename T>
bool writePodChecked(HalFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

bool writeStringChecked(HalFile& file, const std::string& value) {
  const uint32_t length = value.size();
  return writePodChecked(file, length) && file.write(reinterpret_cast<const uint8_t*>(value.data()), length) == length;
}

template <typename Entry>
bool ensurePageLutCapacity(std::unique_ptr<Entry[]>& lut, uint16_t& capacity, const uint16_t count) {
  if (count < capacity) return true;
  if (capacity == UINT16_MAX) return false;

  uint32_t nextCapacity = static_cast<uint32_t>(capacity) * 2U;
  if (nextCapacity > UINT16_MAX) nextCapacity = UINT16_MAX;
  auto grown = makeUniqueNoThrow<Entry[]>(nextCapacity);
  if (!grown) return false;
  for (uint16_t i = 0; i < count; ++i) grown[i] = lut[i];
  lut = std::move(grown);
  capacity = static_cast<uint16_t>(nextCapacity);
  return true;
}

size_t sectionHtmlStreamChunkSize(const EpubRenderMode mode) {
  // The fallback modes exist specifically for a tight/fragmented heap. Their
  // slower 1 KB stream uses 14 KB less transient heap than the normal pair of
  // 8 KB ZIP buffers; the buffers are released before layout begins.
  return mode == EpubRenderMode::Standard ? SECTION_HTML_STREAM_CHUNK_SIZE : LOW_MEMORY_SECTION_HTML_STREAM_CHUNK_SIZE;
}
}  // namespace

// Out-of-line so the unique_ptr<ChapterHtmlSlimParser> in BuildContext can be
// constructed/destroyed where the parser's full definition is visible.
Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePathBase(epub->getCachePath() + "/sections/" + std::to_string(spineIndex)),
      filePath(filePathBase + ".bin") {}

void Section::selectSectionFile(const ReaderRenderSpec& spec) {
  switch (spec.renderMode) {
    case EpubRenderMode::Simplified:
      filePath = filePathBase + ".simplified.bin";
      break;
    case EpubRenderMode::Safe:
      filePath = filePathBase + ".safe.bin";
      break;
    case EpubRenderMode::Standard:
    default:
      filePath = filePathBase + ".bin";
      break;
  }
}

// Suspend any in-progress build so every section.reset() / navigation / sleep path
// persists the pages already laid out as a partial .bin instead of discarding them
// (no-op once a build has completed or never started).
Section::~Section() { suspendBuild(); }

void Section::recoverSectionBackup() const {
  const std::string backupPath = binBackupPath();
  if (Storage.exists(filePath.c_str())) {
    if (Storage.exists(backupPath.c_str())) Storage.remove(backupPath.c_str());
    return;
  }
  if (Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), filePath.c_str())) {
    LOG_ERR("SCT", "Failed to recover previous section cache");
  }
}

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", builtPageCount_);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", builtPageCount_);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", builtPageCount_);

  builtPageCount_++;
  // pageCount is the pages available to read: a rebuild over a partial only raises it
  // once it has laid out more pages than the partial already covers.
  if (builtPageCount_ > pageCount) {
    pageCount = builtPageCount_;
  }
  return position;
}

bool Section::writeSectionFileHeader(const ReaderRenderSpec& spec) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return false;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(spec.fontId) + sizeof(spec.lineCompression) +
                                   sizeof(spec.extraParagraphSpacing) + sizeof(spec.paragraphAlignment) +
                                   sizeof(spec.viewportWidth) + sizeof(spec.viewportHeight) + sizeof(pageCount) +
                                   sizeof(spec.hyphenationEnabled) + sizeof(spec.embeddedStyle) +
                                   sizeof(spec.imageRendering) + sizeof(spec.focusReadingEnabled) +
                                   sizeof(spec.wordSpacing) + sizeof(spec.repairParagraphIndent) + sizeof(uint8_t) +
                                   sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
                                   sizeof(uint32_t),
                "Header size mismatch");
  // Written as the incomplete sentinel; finalizeBuild() patches it to
  // SECTION_FILE_VERSION as the last step, committing the file.
  const uint32_t zero = 0;
  return writePodChecked(file, SECTION_FILE_INCOMPLETE_VERSION) && writePodChecked(file, spec.fontId) &&
         writePodChecked(file, spec.lineCompression) && writePodChecked(file, spec.extraParagraphSpacing) &&
         writePodChecked(file, spec.paragraphAlignment) && writePodChecked(file, spec.viewportWidth) &&
         writePodChecked(file, spec.viewportHeight) && writePodChecked(file, spec.hyphenationEnabled) &&
         writePodChecked(file, spec.embeddedStyle) && writePodChecked(file, spec.imageRendering) &&
         writePodChecked(file, spec.focusReadingEnabled) && writePodChecked(file, spec.wordSpacing) &&
         writePodChecked(file, spec.repairParagraphIndent) &&
         writePodChecked(file, static_cast<uint8_t>(spec.renderMode)) && writePodChecked(file, pageCount) &&
         writePodChecked(file, zero) && writePodChecked(file, zero) && writePodChecked(file, zero) &&
         writePodChecked(file, zero) && writePodChecked(file, zero);
}

bool Section::loadSectionFile(const ReaderRenderSpec& spec) {
  selectSectionFile(spec);
  recoverSectionBackup();
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  bool filePartial = false;
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }
    filePartial = (version == SECTION_FILE_PARTIAL_VERSION);

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    uint8_t fileImageRendering;
    bool fileFocusReadingEnabled;
    uint8_t fileWordSpacing;
    bool fileRepairParagraphIndent;
    uint8_t fileRenderMode;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileEmbeddedStyle);
    serialization::readPod(file, fileImageRendering);
    serialization::readPod(file, fileFocusReadingEnabled);
    serialization::readPod(file, fileWordSpacing);
    serialization::readPod(file, fileRepairParagraphIndent);
    serialization::readPod(file, fileRenderMode);

    if (spec.fontId != fileFontId || spec.lineCompression != fileLineCompression ||
        spec.extraParagraphSpacing != fileExtraParagraphSpacing || spec.paragraphAlignment != fileParagraphAlignment ||
        spec.viewportWidth != fileViewportWidth || spec.viewportHeight != fileViewportHeight ||
        spec.hyphenationEnabled != fileHyphenationEnabled || spec.embeddedStyle != fileEmbeddedStyle ||
        spec.imageRendering != fileImageRendering || spec.focusReadingEnabled != fileFocusReadingEnabled ||
        spec.wordSpacing != fileWordSpacing || spec.repairParagraphIndent != fileRepairParagraphIndent ||
        static_cast<uint8_t>(spec.renderMode) != fileRenderMode) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);

  if (filePartial) {
    // A partial's pageCount is the watermark of a suspended build. Read the watermark
    // trailer (appended after the visible-offset LUT) so estimatedTotalPages can extrapolate.
    uint32_t liLutOffset = 0;
    file.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
    serialization::readPod(file, liLutOffset);
    uint32_t visibleLutOffset = 0;
    file.seek(HEADER_SIZE - sizeof(uint32_t));
    serialization::readPod(file, visibleLutOffset);
    const uint32_t trailerOffset = visibleLutOffset + static_cast<uint32_t>(pageCount) * sizeof(uint32_t);
    const bool trailerValid = pageCount > 0 && liLutOffset >= HEADER_SIZE && visibleLutOffset > liLutOffset &&
                              trailerOffset + 2 * sizeof(uint32_t) <= file.size();
    if (!trailerValid) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: malformed partial section");
      clearCache();
      pageCount = 0;
      return false;
    }
    file.seek(trailerOffset);
    serialization::readPod(file, partialBytesConsumed_);
    serialization::readPod(file, partialTotalBytes_);
    partial_ = true;
    partialPageCount_ = pageCount;
  }

  // Explicit close() required: member variable persists beyond function scope
  file.close();
  LOG_DBG("SCT", "Deserialization succeeded: %d pages%s", pageCount, filePartial ? " (partial)" : "");
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  const std::string tmpBin = binTmpPath();
  if (Storage.exists(tmpBin.c_str())) {
    Storage.remove(tmpBin.c_str());
  }
  const std::string backupBin = binBackupPath();
  if (Storage.exists(backupBin.c_str())) {
    Storage.remove(backupBin.c_str());
  }
  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  // One-shot build: start, then lay out the whole section in a single pass.
  if (!startBuild(spec, popupFn)) {
    return false;
  }
  if (!buildSomeMore(0)) {  // 0 = build to completion
    return false;
  }
  return buildComplete_;
}

bool Section::startBuild(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  if (build_) {
    LOG_ERR("SCT", "startBuild called while a build is already active");
    lastBuildFailure_ = SectionBuildFailure::InvalidContent;
    return false;
  }
  buildComplete_ = false;
  lastBuildFailure_ = SectionBuildFailure::None;
  builtPageCount_ = 0;
  selectSectionFile(spec);
  recoverSectionBackup();
  // Pages from a loaded partial stay readable (from filePath) while this build writes
  // to the tmp .bin, so availability never drops below the partial's watermark.
  pageCount = partial_ ? partialPageCount_ : 0;

  if (!epubLayoutHeapSufficient(spec.renderMode, ESP.getFreeHeap(), ESP.getMaxAllocHeap())) {
    if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseSdFontCaches();
    if (!epubLayoutHeapSufficient(spec.renderMode, ESP.getFreeHeap(), ESP.getMaxAllocHeap())) {
      lastBuildFailure_ = SectionBuildFailure::LowMemory;
      const EpubLayoutHeapFloor floor = epubLayoutHeapFloor(spec.renderMode);
      LOG_ERR("SCT", "Insufficient heap for %s section build (%u/%u, need %u/%u)", epubRenderModeName(spec.renderMode),
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
              static_cast<unsigned>(floor.minFreeHeap), static_cast<unsigned>(floor.minMaxAlloc));
      return false;
    }
  }

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
  const char* forceLowMemory = std::getenv("CROSSPOINT_RENDER_LAB_FORCE_BUILD_LOW_MEMORY");
  if (spec.renderMode != EpubRenderMode::Safe && forceLowMemory && forceLowMemory[0] == '1') {
    lastBuildFailure_ = SectionBuildFailure::LowMemory;
    LOG_ERR("SCT", "Render-lab fault injection: %s section build reports low memory",
            epubRenderModeName(spec.renderMode));
    return false;
  }
#endif

  // Remove a stale tmp .bin from a crash-interrupted build; this build recreates it.
  {
    const std::string staleTmp = binTmpPath();
    if (Storage.exists(staleTmp.c_str())) {
      Storage.remove(staleTmp.c_str());
    }
  }

  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto htmlDir = epub->getCachePath() + "/html";
  const auto htmlPath = htmlDir + "/" + std::to_string(spineIndex) + ".html";
  const auto tmpHtmlPath = htmlDir + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Reuse the previously unzipped HTML if we already have it. The unzipped HTML is keyed only on the
  // book (it lives in the per-book cache dir), not on render settings, so it survives the invalidation
  // that wipes the layout (.bin) caches when font/margin/orientation change -- rebuilds then skip zip
  // inflation entirely. It's promoted by an atomic rename as soon as the inflate succeeds (below), so
  // even a window-only giant spine -- whose .bin never finalizes -- still caches its HTML, letting a
  // reopen skip the multi-second inflate. If htmlPath exists it is known-complete.
  const bool reusedHtml = Storage.exists(htmlPath.c_str());
  bool htmlCached = reusedHtml;
  if (reusedHtml) {
    LOG_DBG("SCT", "Reusing cached HTML %s", htmlPath.c_str());
  } else {
    Storage.mkdir(htmlDir.c_str());

    // Retry logic for SD card timing issues
    bool streamed = false;
    EpubItemReadFailure streamFailure = EpubItemReadFailure::Io;
    uint32_t fileSize = 0;
    for (int attempt = 0; attempt < 3 && !streamed; attempt++) {
      if (attempt > 0) {
        LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
        delay(50);  // Brief delay before retry
      }

      // Remove any incomplete file from previous attempt before retrying
      if (Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
      }

      HalFile tmpHtml;
      if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
        continue;
      }
      // Standard keeps the fast 8 KB path. Low-memory fallback modes trade
      // some first-open speed for 14 KB less transient ZIP buffer heap.
      streamed = epub->readItemContentsToStream(localPath, tmpHtml, sectionHtmlStreamChunkSize(spec.renderMode), false,
                                                &streamFailure);
      fileSize = tmpHtml.size();
      // Explicitly close() file before calling Storage.remove()
      tmpHtml.close();

      // If streaming failed, remove the incomplete file immediately
      if (!streamed && Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
        LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
      }
      if (streamFailure == EpubItemReadFailure::LowMemory || streamFailure == EpubItemReadFailure::InvalidContent) {
        break;
      }
    }

    if (!streamed) {
      switch (streamFailure) {
        case EpubItemReadFailure::LowMemory:
          lastBuildFailure_ = SectionBuildFailure::LowMemory;
          break;
        case EpubItemReadFailure::InvalidContent:
          lastBuildFailure_ = SectionBuildFailure::InvalidContent;
          break;
        case EpubItemReadFailure::Io:
        case EpubItemReadFailure::None:
        default:
          lastBuildFailure_ = SectionBuildFailure::Io;
          break;
      }
      LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
      return false;
    }

    LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

    // Promote to the persistent HTML cache immediately -- the inflate is complete and the bytes are
    // valid regardless of whether the layout build finishes, so reopening (even a window-only spine
    // that never finalizes its .bin) skips re-inflation. If the rename fails we just parse the temp.
    if (Storage.rename(tmpHtmlPath.c_str(), htmlPath.c_str())) {
      htmlCached = true;
    } else {
      LOG_DBG("SCT", "Failed to promote HTML cache; parsing from temp");
    }
  }

  if (!Storage.openFileForWrite("SCT", binTmpPath(), file)) {
    lastBuildFailure_ = SectionBuildFailure::Io;
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  // Header is written with the incomplete-version sentinel; finalizeBuild() commits it.
  if (!writeSectionFileHeader(spec)) {
    lastBuildFailure_ = SectionBuildFailure::Io;
    LOG_ERR("SCT", "Failed to write section header");
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }

  auto ctx = makeUniqueNoThrow<BuildContext>();
  if (!ctx) {
    lastBuildFailure_ = SectionBuildFailure::LowMemory;
    LOG_ERR("SCT", "OOM: BuildContext");
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  ctx->lutCapacity = INITIAL_SECTION_PAGE_LUT_ENTRIES;
  ctx->lut = makeUniqueNoThrow<PageLutEntry[]>(ctx->lutCapacity);
  if (!ctx->lut) {
    lastBuildFailure_ = SectionBuildFailure::LowMemory;
    LOG_ERR("SCT", "OOM: section page LUT (%u bytes)", static_cast<unsigned>(sizeof(PageLutEntry) * ctx->lutCapacity));
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  // htmlCached == "htmlPath is the live cache" (reused, or just promoted). finalizeBuild/abandonBuild
  // then leave the cached HTML alone; only an un-promoted temp (rename failed) is theirs to clean up.
  ctx->reusedHtml = htmlCached;
  ctx->htmlPath = htmlPath;
  ctx->tmpHtmlPath = tmpHtmlPath;
  ctx->parsePath = htmlCached ? htmlPath : tmpHtmlPath;

  // Derive the content base directory and image cache path prefix for the parser
  const size_t lastSlash = localPath.find_last_of('/');
  ctx->contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  ctx->imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";

  if (spec.embeddedStyle) {
    ctx->cssParser = epub->getCssParser();
    if (ctx->cssParser) {
      const CssParser::CacheLoadResult cacheResult = ctx->cssParser->loadFromCache();
      if (cacheResult == CssParser::CacheLoadResult::LowMemory) {
        lastBuildFailure_ = SectionBuildFailure::LowMemory;
        LOG_ERR("SCT", "Insufficient heap to hydrate CSS; section build deferred");
        ctx->cssParser->clear();
        file.close();
        Storage.remove(binTmpPath().c_str());
        if (!ctx->reusedHtml) Storage.remove(ctx->tmpHtmlPath.c_str());
        return false;
      }
      if (cacheResult == CssParser::CacheLoadResult::Invalid) {
        LOG_ERR("SCT", "Failed to load CSS from cache");
      }
    }
  }

  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  std::vector<std::pair<std::string, std::string>> publisherPageAnchors;
  for (auto& entry : epub->getPageListEntriesForSpine(spineIndex)) {
    publisherPageAnchors.emplace_back(std::move(entry.anchor), std::move(entry.label));
  }

  // The parser stores the path/contentBase/imageBasePath by reference, so they must
  // live in the BuildContext (which outlives the parser). The page-complete callback
  // captures the BuildContext pointer to append to its in-RAM LUT; build_ owns the
  // context for the parser's whole lifetime.
  BuildContext* ctxPtr = ctx.get();
  ctx->parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
      epub, ctxPtr->parsePath, renderer, spec.fontId, spec.lineCompression, spec.extraParagraphSpacing,
      spec.paragraphAlignment, spec.viewportWidth, spec.viewportHeight, spec.hyphenationEnabled,
      spec.focusReadingEnabled,
      [this, ctxPtr](std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex,
                     const uint32_t visibleTextOffset) {
        if (!ensurePageLutCapacity(ctxPtr->lut, ctxPtr->lutCapacity, ctxPtr->lutCount)) {
          ctxPtr->lutAllocationFailed = true;
          return;
        }
        const uint32_t fileOffset = this->onPageComplete(std::move(page));
        if (fileOffset == 0) ctxPtr->pageWriteFailed = true;
        ctxPtr->lut[ctxPtr->lutCount++] = {fileOffset, paragraphIndex, listItemIndex, visibleTextOffset};
      },
      spec.embeddedStyle, ctxPtr->contentBase, ctxPtr->imageBasePath, spec.imageRendering, std::move(tocAnchors),
      std::move(publisherPageAnchors), popupFn, ctxPtr->cssParser, spec.renderMode, spec.repairParagraphIndent,
      spec.wordSpacing);
  if (!ctx->parser) {
    lastBuildFailure_ = SectionBuildFailure::LowMemory;
    LOG_ERR("SCT", "OOM: ChapterHtmlSlimParser");
    if (ctx->cssParser) ctx->cssParser->clear();
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }

  Hyphenator::setPreferredLanguage(epub->getLanguage());
  build_ = std::move(ctx);

  if (!build_->parser->beginParse()) {
    const SectionBuildFailure failure = sectionFailureFromParser(build_->parser->failure());
    LOG_ERR("SCT", "Failed to begin parse");
    failBuild(failure);
    return false;
  }
  build_->totalBytes = build_->parser->parseTotalBytes();
  return true;
}

bool Section::buildSomeMore(const int maxPages) {
  if (!build_ || !build_->parser) {
    LOG_ERR("SCT", "buildSomeMore with no active build");
    return false;
  }
  // Pace on pages laid out by THIS build, not pageCount: during a rebuild over a partial,
  // pageCount stays pinned at the partial's watermark until the build passes it, which
  // would otherwise turn one "small" chunk into a blocking rebuild of the whole watermark.
  const int startCount = builtPageCount_;
  for (;;) {
    const auto status = build_->parser->parseStep();
    if (build_->lutAllocationFailed) {
      LOG_ERR("SCT", "Failed to grow section page LUT");
      failBuild(SectionBuildFailure::LowMemory);
      return false;
    }
    if (build_->pageWriteFailed) {
      LOG_ERR("SCT", "Page write failed during incremental build");
      failBuild(SectionBuildFailure::Io);
      return false;
    }
    if (status == ChapterHtmlSlimParser::ParseStatus::Error) {
      const SectionBuildFailure failure = sectionFailureFromParser(build_->parser->failure());
      LOG_ERR("SCT", "Parse error during incremental build");
      failBuild(failure);
      return false;
    }
    if (status == ChapterHtmlSlimParser::ParseStatus::Done) {
      return finalizeBuild();
    }
    // ParseStatus::More: yield once we've laid out the requested number of pages.
    if (maxPages > 0 && (builtPageCount_ - startCount) >= maxPages) {
      build_->bytesConsumed = build_->parser->parseBytesConsumed();
      return true;
    }
  }
}

bool Section::hasHtmlCache() const {
  const std::string htmlPath = epub->getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
  return Storage.exists(htmlPath.c_str());
}

std::optional<uint16_t> Section::findAnchorDuringBuild(const std::string& anchor) const {
  if (!build_ || !build_->parser) return std::nullopt;
  for (const auto& [key, page] : build_->parser->getAnchors()) {
    if (key == anchor) return page;
  }
  return std::nullopt;
}

std::optional<uint16_t> Section::findAnchor(const std::string& anchor) const {
  if (const auto page = findAnchorDuringBuild(anchor)) {
    return page;
  }
  // Fall back to the on-disk anchor map: a finalized section, or a partial whose map
  // covers everything up to its watermark (nullopt past it -- build further and retry).
  return getPageForAnchor(anchor);
}

uint16_t Section::estimatedTotalPages() const {
  // Extrapolation from a suspended session's watermark trailer. A static snapshot, so no EMA
  // damping is needed. Also the best guess while a rebuild is running but hasn't laid out
  // enough pages yet to extrapolate from its own progress.
  const auto partialEstimate = [this]() -> uint16_t {
    if (!partial_ || partialBytesConsumed_ == 0 || partialTotalBytes_ <= partialBytesConsumed_) {
      return pageCount;
    }
    const uint64_t est = static_cast<uint64_t>(partialPageCount_) * partialTotalBytes_ / partialBytesConsumed_;
    if (est <= pageCount) return pageCount;
    return est > 60000 ? 60000 : static_cast<uint16_t>(est);
  };

  if (!build_) {
    return partial_ ? partialEstimate() : pageCount;  // partial -> extrapolate, finalized -> exact
  }
  const uint32_t consumed = build_->bytesConsumed;
  const uint32_t total = build_->totalBytes;
  if (builtPageCount_ == 0 || consumed == 0 || total <= consumed) return partialEstimate();

  // Raw extrapolation: scale the pages built so far by the fraction of HTML still unparsed. This
  // re-derives from a growing, non-uniform sample, so it jitters up and down as the build crosses
  // dense vs sparse regions of the chapter.
  const uint64_t raw = static_cast<uint64_t>(builtPageCount_) * total / consumed;

  // Damp that jitter with an exponential moving average. Step it once per build advance (keyed on
  // bytesConsumed) rather than per status-bar redraw, so the smoothing rate doesn't depend on how
  // often we repaint. As the build nears the end, consumed -> total and raw -> the built count, so
  // the average settles onto the true count (and finalizeBuild then returns the exact pageCount).
  constexpr float ALPHA = 0.25f;  // weight of each new sample; lower = steadier but slower to settle
  if (build_->smoothedEstimate <= 0) {
    build_->smoothedEstimate = static_cast<float>(raw);  // seed on the first estimate
  } else if (consumed != build_->smoothedAtConsumed) {
    build_->smoothedEstimate += ALPHA * (static_cast<float>(raw) - build_->smoothedEstimate);
  }
  build_->smoothedAtConsumed = consumed;

  const uint64_t est = static_cast<uint64_t>(build_->smoothedEstimate + 0.5f);
  if (est <= pageCount) return pageCount;  // never fewer than the pages already available
  return est > 60000 ? 60000 : static_cast<uint16_t>(est);
}

// Write the LUTs and anchor map into the open tmp .bin, patch the header with the built
// page count and table offsets, stamp `version` as the commit point, then swap the tmp
// file over filePath. For SECTION_FILE_PARTIAL_VERSION a watermark trailer
// (bytesConsumed, totalBytes) is appended after the li LUT so a later open can estimate
// the total page count. The parser must still be alive (anchors are read from it).
// On failure the tmp is removed and any pre-existing file at filePath is left intact.
bool Section::commitBuildFile(const uint8_t version, const uint32_t bytesConsumed, const uint32_t totalBytes) {
  const bool asPartial = (version == SECTION_FILE_PARTIAL_VERSION);

  const auto failCommit = [this]() {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
    return false;
  };

  const uint32_t lutOffset = file.position();
  for (uint16_t i = 0; i < build_->lutCount; ++i) {
    const auto& entry = build_->lut[i];
    if (entry.fileOffset == 0) {
      LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
      return failCommit();
    }
    if (!writePodChecked(file, entry.fileOffset)) return failCommit();
  }

  // Write anchor-to-page map for fragment navigation (e.g. footnote targets). For a
  // partial, skip anchors that landed on the incomplete trailing page the suspend drops.
  const uint32_t anchorMapOffset = file.position();
  const auto& anchors = build_->parser->getAnchors();
  uint16_t anchorCount = 0;
  for (const auto& [anchor, page] : anchors) {
    if (!asPartial || page < builtPageCount_) anchorCount++;
  }
  if (!writePodChecked(file, anchorCount)) return failCommit();
  for (const auto& [anchor, page] : anchors) {
    if (asPartial && page >= builtPageCount_) continue;
    if (!writeStringChecked(file, anchor) || !writePodChecked(file, page)) return failCommit();
  }

  const uint32_t paragraphLutOffset = file.position();
  if (!writePodChecked(file, build_->lutCount)) return failCommit();
  for (uint16_t i = 0; i < build_->lutCount; ++i) {
    if (!writePodChecked(file, build_->lut[i].paragraphIndex)) return failCommit();
  }

  const uint32_t liLutFileOffset = static_cast<uint32_t>(file.position());
  for (uint16_t i = 0; i < build_->lutCount; ++i) {
    if (!writePodChecked(file, build_->lut[i].listItemIndex)) return failCommit();
  }

  const uint32_t visibleLutFileOffset = static_cast<uint32_t>(file.position());
  for (uint16_t i = 0; i < build_->lutCount; ++i) {
    if (!writePodChecked(file, build_->lut[i].visibleTextOffset)) return failCommit();
  }

  if (asPartial) {
    // Watermark trailer, located on load immediately after the visible-offset LUT.
    if (!writePodChecked(file, bytesConsumed) || !writePodChecked(file, totalBytes)) return failCommit();
  }

  // Patch header with the built page count and section offsets...
  if (!file.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(builtPageCount_)) ||
      !writePodChecked(file, builtPageCount_) || !writePodChecked(file, lutOffset) ||
      !writePodChecked(file, anchorMapOffset) || !writePodChecked(file, paragraphLutOffset) ||
      !writePodChecked(file, liLutFileOffset) || !writePodChecked(file, visibleLutFileOffset)) {
    return failCommit();
  }
  // ...then commit by overwriting the sentinel version with the real one. Writing the
  // version last makes it the commit point: a crash before here leaves version 0.
  if (!file.seek(0) || !writePodChecked(file, version)) return failCommit();
  // Explicit close() required: member variable persists beyond function scope
  file.flush();
  if (!file.close()) {
    LOG_ERR("SCT", "Failed to close completed section cache");
    Storage.remove(binTmpPath().c_str());
    return false;
  }

  // Rotate the previous readable generation aside before publishing the new
  // one. If the SD rename fails, restore it instead of turning a transient I/O
  // error into a missing cache on every subsequent open.
  const std::string backupPath = binBackupPath();
  const bool hadExistingCache = Storage.exists(filePath.c_str());
  if (hadExistingCache) {
    if (Storage.exists(backupPath.c_str())) Storage.remove(backupPath.c_str());
    if (!Storage.rename(filePath.c_str(), backupPath.c_str())) {
      LOG_ERR("SCT", "Failed to back up previous section cache");
      Storage.remove(binTmpPath().c_str());
      return false;
    }
  }
  if (!Storage.rename(binTmpPath().c_str(), filePath.c_str())) {
    LOG_ERR("SCT", "Failed to move built section into place");
    Storage.remove(binTmpPath().c_str());
    if (hadExistingCache && !Storage.rename(backupPath.c_str(), filePath.c_str())) {
      LOG_ERR("SCT", "Failed to restore previous section cache");
    }
    return false;
  }
  if (Storage.exists(backupPath.c_str())) Storage.remove(backupPath.c_str());
  return true;
}

bool Section::finalizeBuild() {
  // Flush the trailing page (emits the last page via the completePageFn into the LUT).
  if (!build_->parser->finishParse()) {
    const SectionBuildFailure failure = sectionFailureFromParser(build_->parser->failure());
    LOG_ERR("SCT", "Failed to finish section parse");
    failBuild(failure);
    return false;
  }
  if (build_->lutAllocationFailed) {
    LOG_ERR("SCT", "Failed to grow final section page LUT");
    failBuild(SectionBuildFailure::LowMemory);
    return false;
  }
  if (build_->pageWriteFailed) {
    LOG_ERR("SCT", "Failed to write final section page");
    failBuild(SectionBuildFailure::Io);
    return false;
  }

  if (!build_->reusedHtml) {
    // Parse succeeded: promote the freshly unzipped HTML to the persistent cache so future
    // rebuilds skip zip inflation. If promotion fails, drop the temp -- the build still succeeded.
    if (!Storage.rename(build_->tmpHtmlPath.c_str(), build_->htmlPath.c_str())) {
      LOG_DBG("SCT", "Failed to promote HTML cache, removing temp");
      Storage.remove(build_->tmpHtmlPath.c_str());
    }
  }

  const bool committed = commitBuildFile(SECTION_FILE_VERSION, 0, 0);
  if (!committed) {
    failBuild(SectionBuildFailure::Io);
    return false;
  }
  if (build_->cssParser) build_->cssParser->clear();
  build_.reset();
  buildComplete_ = true;
  lastBuildFailure_ = SectionBuildFailure::None;
  partial_ = false;
  partialPageCount_ = 0;
  pageCount = builtPageCount_;
  return true;
}

void Section::suspendBuild() {
  if (!build_) return;

  // Only worth persisting if this build produced pages a pre-existing partial doesn't
  // already cover; otherwise keep the older (bigger) partial and just drop the tmp.
  const bool worthKeeping = builtPageCount_ > 0 && (!partial_ || builtPageCount_ > partialPageCount_);

  bool committed = false;
  if (worthKeeping) {
    // Capture the parse watermark and commit before tearing the parser down (the anchor
    // map is read from it). The incomplete trailing page is intentionally not flushed:
    // only fully laid-out pages are persisted, and the rebuild re-derives the rest.
    const uint32_t consumed = static_cast<uint32_t>(build_->parser->parseBytesConsumed());
    committed = commitBuildFile(SECTION_FILE_PARTIAL_VERSION, consumed, build_->totalBytes);
    if (committed) {
      partial_ = true;
      partialPageCount_ = builtPageCount_;
      partialBytesConsumed_ = consumed;
      partialTotalBytes_ = build_->totalBytes;
      LOG_INF("SCT", "Suspended build: %u pages persisted", builtPageCount_);
    }
  }

  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (!committed && file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_.reset();
  buildComplete_ = false;
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

void Section::failBuild(const SectionBuildFailure failure) {
  lastBuildFailure_ = failure;
  if (shouldSuspendFailedSectionBuild(failure, builtPageCount_, partial_)) {
    // CrossInk's important recovery property: an OOM is a suspension point,
    // not corruption. Commit complete pages as a partial generation so a
    // fallback/restart can resume without losing the readable prefix.
    suspendBuild();
    return;
  }
  discardBuild(shouldPreserveSectionCache(failure));
}

void Section::discardBuild(const bool preserveExistingCache) {
  if (!build_) return;
  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
  }
  // A failed rebuild cannot invalidate an older generation that already
  // passed cache validation. Normally keep it and discard only the temporary
  // generation; explicit page-load corruption calls abandonBuild()/clearCache().
  if (!preserveExistingCache && Storage.exists(filePath.c_str())) {
    Storage.remove(filePath.c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_.reset();
  buildComplete_ = false;
  if (!preserveExistingCache) {
    partial_ = false;
    partialPageCount_ = 0;
  }
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

void Section::abandonBuild() { discardBuild(false); }

std::unique_ptr<Page> Section::loadPageDuringBuild(const int page) {
  if (!build_ || page < 0 || page >= static_cast<int>(build_->lutCount) || !file) {
    return nullptr;
  }
  const uint32_t pos = build_->lut[page].fileOffset;
  if (pos == 0) {
    return nullptr;
  }
  // The .bin is open O_RDWR for the build. Read the already-written page, then restore
  // the write cursor so the next onPageComplete keeps appending where it left off.
  const uint32_t writePos = file.position();
  file.seek(pos);
  auto p = Page::deserialize(file);
  file.seek(writePos);
  if (p) {
    p->visibleTextOffset = build_->lut[page].visibleTextOffset;
  }
  return p;
}

// Read a page from the committed file at filePath (finalized section or partial from a
// previous session). Uses a local handle so it is safe while a build holds the member
// `file` open on the tmp .bin.
std::unique_ptr<Page> Section::loadPageAt(const int page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return nullptr;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5);
  uint32_t lutOffset;
  serialization::readPod(f, lutOffset);
  f.seek(lutOffset + sizeof(uint32_t) * page);
  uint32_t pagePos;
  serialization::readPod(f, pagePos);

  // Read this page's visible-codepoint start offset from the visible-offset LUT (last header slot)
  // in the same open handle, so the reader can persist progress without reopening the section file
  // on every page turn (see Page::visibleTextOffset). A malformed/old file leaves it at 0.
  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t visibleLutOffset;
  serialization::readPod(f, visibleLutOffset);
  uint32_t visibleTextOffset = 0;
  const uint32_t visibleEntry = visibleLutOffset + sizeof(uint32_t) * page;
  if (visibleLutOffset >= HEADER_SIZE && visibleEntry + sizeof(uint32_t) <= f.size()) {
    f.seek(visibleEntry);
    serialization::readPod(f, visibleTextOffset);
  }

  f.seek(pagePos);
  auto p = Page::deserialize(f);
  if (p) {
    p->visibleTextOffset = visibleTextOffset;
  }
  return p;
  // No f.close() needed -- DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}

std::unique_ptr<Page> Section::loadPage(const int page) {
  if (page < 0) {
    return nullptr;
  }
  if (build_ && page < static_cast<int>(build_->lutCount)) {
    return loadPageDuringBuild(page);
  }
  // Not (yet) in the active build: serve from the file on disk -- a finalized section,
  // or a partial from a previous session whose pages the rebuild hasn't reached again.
  const int onDisk = partial_ ? partialPageCount_ : (build_ ? 0 : pageCount);
  if (page >= onDisk) {
    return nullptr;
  }
  return loadPageAt(page);
}

std::string Section::getTextFromSectionFile() {
  std::string fullText;
  auto p = loadPage(currentPage);
  if (p) {
    for (const auto& el : p->elements) {
      if (el->getTag() == TAG_PageLine) {
        const auto& line = static_cast<const PageLine&>(*el);
        if (line.getBlock()) {
          const auto& block = *line.getBlock();
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!fullText.empty()) fullText += " ";
            fullText += block.wordText(i);
          }
        }
      }
    }
  }
  return fullText;
}

std::optional<uint16_t> Section::getCachedPageCount() const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (fileSize < HEADER_SIZE) {
    return std::nullopt;
  }

  // Only a finalized section's count is the chapter total; a partial's count is just the
  // suspended build's watermark, which would skew progress mapping. Callers fall back to
  // their own estimates.
  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t));
  uint16_t count;
  serialization::readPod(f, count);
  return count;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 4);
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(anchorMapOffset);
  uint16_t count;
  serialization::readPod(f, count);
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page;
    serialization::readString(f, key);
    serialization::readPod(f, page);
    if (key == anchor) {
      return page;
    }
  }

  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = paragraphLutOffset + sizeof(uint16_t) + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pagePIdx;
    serialization::readPod(f, pagePIdx);
    if (pagePIdx >= pIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0 || page >= count) {
    return std::nullopt;
  }

  const uint32_t entryEnd = paragraphLutOffset + sizeof(uint16_t) + (page + 1) * sizeof(uint16_t);
  if (entryEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset + sizeof(uint16_t) + page * sizeof(uint16_t));
  uint16_t pIdx;
  serialization::readPod(f, pIdx);
  return pIdx;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t liLutOffset;
  serialization::readPod(f, liLutOffset);
  if (liLutOffset == 0 || liLutOffset >= fileSize) {
    return std::nullopt;
  }

  // The li LUT shares count with the paragraph LUT; read count from paragraphLutOffset
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = liLutOffset + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(liLutOffset);
  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pageLiIdx;
    serialization::readPod(f, pageLiIdx);
    if (pageLiIdx >= liIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint32_t> Section::getVisibleTextOffsetForPage(const uint16_t page) const {
  if (build_ && page < build_->lutCount) {
    return build_->lut[page].visibleTextOffset;
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t));
  uint16_t count;
  serialization::readPod(f, count);
  if (page >= count) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t visibleLutOffset;
  serialization::readPod(f, visibleLutOffset);
  const uint32_t entryOffset = visibleLutOffset + static_cast<uint32_t>(page) * sizeof(uint32_t);
  if (visibleLutOffset < HEADER_SIZE || entryOffset + sizeof(uint32_t) > f.size()) {
    return std::nullopt;
  }

  f.seek(entryOffset);
  uint32_t result;
  serialization::readPod(f, result);
  return result;
}

std::optional<uint16_t> Section::getPageForVisibleTextOffset(const uint32_t offset,
                                                             const bool preferFirstAtOffset) const {
  const auto findInEntries = [offset, preferFirstAtOffset](const PageLutEntry* entries,
                                                           const uint16_t count) -> std::optional<uint16_t> {
    if (!entries || count == 0) return std::nullopt;
    uint16_t result = 0;
    for (uint16_t i = 0; i < count; i++) {
      const uint32_t pageStart = entries[i].visibleTextOffset;
      if (preferFirstAtOffset && pageStart == offset) {
        return static_cast<uint16_t>(i);
      }
      if (pageStart > offset) break;
      result = static_cast<uint16_t>(i);
    }
    return result;
  };

  if (build_ && build_->lutCount > 0) {
    // Resolve within the active build's known range. Later offsets may still be
    // covered by an on-disk partial that the resumed build has not reached yet.
    if (offset <= build_->lut[build_->lutCount - 1].visibleTextOffset) {
      return findInEntries(build_->lut.get(), build_->lutCount);
    }
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }
  const bool partial = version == SECTION_FILE_PARTIAL_VERSION;

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t));
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t visibleLutOffset;
  serialization::readPod(f, visibleLutOffset);
  if (visibleLutOffset < HEADER_SIZE || visibleLutOffset + static_cast<uint32_t>(count) * sizeof(uint32_t) > f.size()) {
    return std::nullopt;
  }

  f.seek(visibleLutOffset);
  uint16_t result = 0;
  uint32_t lastPageStart = 0;
  for (uint16_t page = 0; page < count; page++) {
    uint32_t pageStart;
    serialization::readPod(f, pageStart);
    lastPageStart = pageStart;
    if (preferFirstAtOffset && pageStart == offset) {
      return page;
    }
    if (pageStart > offset) break;
    result = page;
  }
  if (partial && offset > lastPageStart) {
    return std::nullopt;
  }
  return result;
}
