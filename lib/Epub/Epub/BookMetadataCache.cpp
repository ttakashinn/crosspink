#include "BookMetadataCache.h"

#include <BufferedFile.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <array>
#include <deque>
#include <limits>

#include "FsHelpers.h"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 11;  // v11: source fingerprint + bounded validation
constexpr char bookBinFile[] = "/book.bin";
constexpr char bookBinTempFile[] = "/book.bin.tmp";
constexpr char bookBinBackupFile[] = "/book.bin.bak";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
// Buffer size for the buildBookBin streams. 3 buffers x 4KB, transient (freed on
// return); 4KB = 8 SD sectors per transfer, enough to stop the sector-cache thrash.
constexpr size_t BUILD_IO_BUFFER_SIZE = 4096;
constexpr uint16_t MAX_SPINE_COUNT = 4096;
constexpr uint16_t MAX_TOC_COUNT = 8192;
constexpr size_t MAX_METADATA_TEXT = 1024;
constexpr size_t MAX_HREF_TEXT = 4096;

struct SourceFingerprint {
  uint64_t fileSize = 0;
  uint32_t centralDirectorySize = 0;
  uint32_t centralDirectoryHash = 0;

  bool operator==(const SourceFingerprint&) const = default;
};

uint16_t readLe16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

bool computeSourceFingerprint(const std::string& path, SourceFingerprint& fingerprint) {
  HalFile file;
  if (!Storage.openFileForRead("BMC", path, file)) return false;
  const uint64_t fileSize = file.fileSize64();
  if (fileSize < 22 || fileSize > UINT32_MAX) return false;

  // EOCD is within the final 65,557 bytes (22-byte record + 65,535-byte ZIP
  // comment). Scan backwards in fixed blocks to avoid a tail-sized allocation.
  constexpr uint64_t MAX_EOCD_SEARCH = 22U + UINT16_MAX;
  constexpr size_t SEARCH_BLOCK = 512;
  const uint64_t searchStart = fileSize > MAX_EOCD_SEARCH ? fileSize - MAX_EOCD_SEARCH : 0;
  uint64_t searchEnd = fileSize;
  uint64_t eocdOffset = UINT64_MAX;
  uint32_t centralSize = 0;
  uint32_t centralOffset = 0;
  std::array<uint8_t, SEARCH_BLOCK> block{};
  std::array<uint8_t, 22> eocd{};
  while (searchEnd > searchStart) {
    const uint64_t blockStart = searchEnd - searchStart > SEARCH_BLOCK ? searchEnd - SEARCH_BLOCK : searchStart;
    const size_t count = static_cast<size_t>(searchEnd - blockStart);
    if (!file.seek64(blockStart) || file.read(block.data(), count) != static_cast<int>(count)) return false;
    for (int i = static_cast<int>(count) - 4; i >= 0; --i) {
      if (block[static_cast<size_t>(i)] == 0x50 && block[static_cast<size_t>(i) + 1] == 0x4B &&
          block[static_cast<size_t>(i) + 2] == 0x05 && block[static_cast<size_t>(i) + 3] == 0x06) {
        const uint64_t candidate = blockStart + static_cast<uint64_t>(i);
        if (candidate + eocd.size() > fileSize || !file.seek64(candidate) ||
            file.read(eocd.data(), eocd.size()) != static_cast<int>(eocd.size())) {
          continue;
        }
        const uint16_t commentLength = readLe16(eocd.data() + 20);
        const uint32_t candidateCentralSize = readLe32(eocd.data() + 12);
        const uint32_t candidateCentralOffset = readLe32(eocd.data() + 16);
        if (readLe16(eocd.data() + 4) != 0 || readLe16(eocd.data() + 6) != 0 || candidateCentralSize == UINT32_MAX ||
            candidateCentralOffset == UINT32_MAX || candidate + eocd.size() + commentLength != fileSize ||
            static_cast<uint64_t>(candidateCentralOffset) + candidateCentralSize > candidate) {
          continue;
        }
        eocdOffset = candidate;
        centralSize = candidateCentralSize;
        centralOffset = candidateCentralOffset;
        break;
      }
    }
    if (eocdOffset != UINT64_MAX || blockStart == searchStart) break;
    searchEnd = blockStart + 3;  // overlap the four-byte signature
  }
  if (eocdOffset == UINT64_MAX) return false;

  uint32_t hash = 2166136261U;
  uint32_t remaining = centralSize;
  if (!file.seek64(centralOffset)) return false;
  while (remaining > 0) {
    const size_t count = std::min<size_t>(block.size(), remaining);
    if (file.read(block.data(), count) != static_cast<int>(count)) return false;
    for (size_t i = 0; i < count; ++i) {
      hash ^= block[i];
      hash *= 16777619U;
    }
    remaining -= static_cast<uint32_t>(count);
  }
  if (!file.close()) return false;
  fingerprint = {fileSize, centralSize, hash};
  return true;
}

// Entry (de)serializers, templated so they run over HalFile and the Buffered*
// wrappers alike (two instantiations each -- a few hundred bytes of flash, in
// exchange for the build path streaming at SD speed instead of per-pod).
template <typename F>
uint32_t writeSpineEntryTo(F& file, const BookMetadataCache::SpineEntry& entry) {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.href);
  serialization::writePod(file, entry.cumulativeSize);
  serialization::writePod(file, entry.tocIndex);
  return pos;
}

template <typename F>
uint32_t writeTocEntryTo(F& file, const BookMetadataCache::TocEntry& entry) {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.title);
  serialization::writeString(file, entry.href);
  serialization::writeString(file, entry.anchor);
  serialization::writePod(file, entry.level);
  serialization::writePod(file, entry.spineIndex);
  return pos;
}

template <typename F>
BookMetadataCache::SpineEntry readSpineEntryFrom(F& file) {
  BookMetadataCache::SpineEntry entry;
  serialization::readString(file, entry.href);
  serialization::readPod(file, entry.cumulativeSize);
  serialization::readPod(file, entry.tocIndex);
  return entry;
}

template <typename F>
BookMetadataCache::TocEntry readTocEntryFrom(F& file) {
  BookMetadataCache::TocEntry entry;
  serialization::readString(file, entry.title);
  serialization::readString(file, entry.href);
  serialization::readString(file, entry.anchor);
  serialization::readPod(file, entry.level);
  serialization::readPod(file, entry.spineIndex);
  return entry;
}

bool tryReadSpineEntry(HalFile& file, BookMetadataCache::SpineEntry& entry) {
  return serialization::tryReadString(file, entry.href, MAX_HREF_TEXT) &&
         serialization::tryReadPod(file, entry.cumulativeSize) && serialization::tryReadPod(file, entry.tocIndex);
}

bool tryReadTocEntry(HalFile& file, BookMetadataCache::TocEntry& entry) {
  return serialization::tryReadString(file, entry.title, MAX_METADATA_TEXT) &&
         serialization::tryReadString(file, entry.href, MAX_HREF_TEXT) &&
         serialization::tryReadString(file, entry.anchor, MAX_HREF_TEXT) &&
         serialization::tryReadPod(file, entry.level) && serialization::tryReadPod(file, entry.spineIndex);
}
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

bool BookMetadataCache::beginWrite() {
  buildMode = true;
  spineCount = 0;
  tocCount = 0;
  LOG_DBG("BMC", "Entering write mode");
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG("BMC", "Beginning content opf pass");

  // Open spine file for writing
  if (!Storage.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  // Wrapper OOM is fine: createSpineEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(spineFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endContentOpfPass() {
  const bool flushed = !passOut || passOut->flush();
  passOut.reset();
  // Explicit close() required: member variable persists beyond function scope
  spineFile.close();
  if (!flushed) {
    LOG_ERR("BMC", "Failed writing spine tmp file");
  }
  return flushed;
}

bool BookMetadataCache::beginTocPass() {
  LOG_DBG("BMC", "Beginning toc pass");

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  if (!Storage.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variable persists beyond function scope
    spineFile.close();
    return false;
  }

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    spineHrefIndex.clear();
    spineHrefIndex.resize(spineCount);
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto entry = readSpineEntry(spineFile);
      SpineHrefIndexEntry idx;
      idx.hrefHash = fnvHash64(entry.href);
      idx.hrefLen = static_cast<uint16_t>(entry.href.size());
      idx.spineIndex = static_cast<int16_t>(i);
      spineHrefIndex[i] = idx;
    }
    std::sort(spineHrefIndex.begin(), spineHrefIndex.end(),
              [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
              });
    spineFile.seek(0);
    useSpineHrefIndex = true;
    LOG_DBG("BMC", "Using fast index for %d spine items", spineCount);
  } else {
    useSpineHrefIndex = false;
  }

  // Wrapper OOM is fine: createTocEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(tocFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endTocPass() {
  const bool flushed = !passOut || passOut->flush();
  passOut.reset();
  if (!flushed) {
    LOG_ERR("BMC", "Failed writing toc tmp file");
  }
  // Explicit close() required: member variables persist beyond function scope
  tocFile.close();
  spineFile.close();

  spineHrefIndex.clear();
  spineHrefIndex.shrink_to_fit();
  useSpineHrefIndex = false;

  return flushed;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    LOG_DBG("BMC", "endWrite called but not in build mode");
    return false;
  }

  buildMode = false;
  LOG_DBG("BMC", "Wrote %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  if (spineCount > MAX_SPINE_COUNT || tocCount > MAX_TOC_COUNT || metadata.title.size() > MAX_METADATA_TEXT ||
      metadata.author.size() > MAX_METADATA_TEXT || metadata.language.size() > MAX_METADATA_TEXT ||
      metadata.coverItemHref.size() > MAX_HREF_TEXT || metadata.textReferenceHref.size() > MAX_HREF_TEXT) {
    LOG_ERR("BMC", "Refusing oversized book metadata cache");
    return false;
  }

  SourceFingerprint sourceFingerprint;
  if (!computeSourceFingerprint(epubPath, sourceFingerprint)) {
    LOG_ERR("BMC", "Could not fingerprint EPUB source");
    return false;
  }

  // Open all three files, writing to meta, reading from spine and toc
  const std::string tempBookPath = cachePath + bookBinTempFile;
  Storage.remove(tempBookPath.c_str());
  if (!Storage.openFileForWrite("BMC", tempBookPath, bookFile)) {
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variables persist beyond function scope
    bookFile.close();
    spineFile.close();
    return false;
  }

  // Buffered streams for the whole build: every access below is sequential per
  // file, but interleaved ACROSS files, which thrashes SdFat's single shared
  // sector cache when unbuffered (one 512B SD transaction per 4-byte pod --
  // measured 31s for a 1,732-spine omnibus). Three 4KB buffers, freed on return.
  serialization::BufferedFileWriter bookOut(bookFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader spineIn(spineFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader tocIn(tocFile, BUILD_IO_BUFFER_SIZE);

  constexpr uint32_t headerASize = sizeof(BOOK_CACHE_VERSION) + /* LUT Offset */ sizeof(uint32_t) + sizeof(spineCount) +
                                   sizeof(tocCount) + sizeof(sourceFingerprint.fileSize) +
                                   sizeof(sourceFingerprint.centralDirectorySize) +
                                   sizeof(sourceFingerprint.centralDirectoryHash);
  const uint64_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5ULL;
  const uint64_t lutSize64 = sizeof(uint32_t) * (static_cast<uint64_t>(spineCount) + tocCount);
  if (headerASize + metadataSize + lutSize64 > UINT32_MAX) {
    LOG_ERR("BMC", "Book metadata cache offsets exceed 32-bit format");
    bookFile.close();
    spineFile.close();
    tocFile.close();
    Storage.remove(tempBookPath.c_str());
    return false;
  }
  const uint32_t lutSize = static_cast<uint32_t>(lutSize64);
  const uint32_t lutOffset = static_cast<uint32_t>(headerASize + metadataSize);

  // Header A
  serialization::writePod(bookOut, BOOK_CACHE_VERSION);
  serialization::writePod(bookOut, lutOffset);
  serialization::writePod(bookOut, spineCount);
  serialization::writePod(bookOut, tocCount);
  serialization::writePod(bookOut, sourceFingerprint.fileSize);
  serialization::writePod(bookOut, sourceFingerprint.centralDirectorySize);
  serialization::writePod(bookOut, sourceFingerprint.centralDirectoryHash);
  // Metadata
  serialization::writeString(bookOut, metadata.title);
  serialization::writeString(bookOut, metadata.author);
  serialization::writeString(bookOut, metadata.language);
  serialization::writeString(bookOut, metadata.coverItemHref);
  serialization::writeString(bookOut, metadata.textReferenceHref);

  // Loop through spine entries, writing LUT positions
  spineIn.seek(0);
  for (int i = 0; i < spineCount; i++) {
    const uint32_t pos = spineIn.position();
    readSpineEntryFrom(spineIn);
    serialization::writePod(bookOut, pos + lutOffset + lutSize);
  }
  // Total size of the spine tmp file: entries land in book.bin after the toc LUT
  // and the full spine block, so toc LUT positions are offset by it.
  const auto spineBytes = static_cast<uint32_t>(spineIn.position());

  // Loop through toc entries, writing LUT positions
  tocIn.seek(0);
  for (int i = 0; i < tocCount; i++) {
    const uint32_t pos = tocIn.position();
    readTocEntryFrom(tocIn);
    serialization::writePod(bookOut, pos + lutOffset + lutSize + spineBytes);
  }

  // LUTs complete
  // Loop through spines from spine file matching up TOC indexes, calculating cumulative size and writing to book.bin

  // Build spineIndex->tocIndex mapping in one pass (O(n) instead of O(n*m))
  std::deque<int16_t> spineToTocIndex(spineCount, -1);
  tocIn.seek(0);
  for (int j = 0; j < tocCount; j++) {
    auto tocEntry = readTocEntryFrom(tocIn);
    if (tocEntry.spineIndex >= 0 && tocEntry.spineIndex < spineCount) {
      if (spineToTocIndex[tocEntry.spineIndex] == -1) {
        spineToTocIndex[tocEntry.spineIndex] = static_cast<int16_t>(j);
      }
    }
  }

  ZipFile zip(epubPath);
  // Pre-open zip file to speed up size calculations
  if (!zip.open()) {
    LOG_ERR("BMC", "Could not open EPUB zip for size calculations");
    // Explicit close() required: member variables persist beyond function scope
    bookFile.close();
    spineFile.close();
    tocFile.close();
    return false;
  }
  // NOTE: We intentionally skip calling loadAllFileStatSlims() here.
  // For large EPUBs (2000+ chapters), pre-loading all ZIP central directory entries
  // into memory causes OOM crashes on ESP32-C3's limited ~380KB RAM.
  // Instead, for large books we use a one-pass batch lookup that scans the ZIP
  // central directory once and matches against spine targets using hash comparison.
  // This is O(n*log(m)) instead of O(n*m) while avoiding memory exhaustion.
  // See: https://github.com/crosspoint-reader/crosspoint-reader/issues/134

  std::deque<uint32_t> spineSizes;
  bool useBatchSizes = false;

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    LOG_DBG("BMC", "Using batch size lookup for %d spine items", spineCount);

    std::deque<ZipFile::SizeTarget> targets;
    targets.resize(spineCount);

    spineIn.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto entry = readSpineEntryFrom(spineIn);
      std::string path = FsHelpers::normalisePath(entry.href);

      ZipFile::SizeTarget t;
      t.hash = ZipFile::fnvHash64(path.c_str(), path.size());
      t.len = static_cast<uint16_t>(path.size());
      t.index = static_cast<uint16_t>(i);
      targets[i] = t;
    }

    std::sort(targets.begin(), targets.end(), [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
      return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
    });

    spineSizes.resize(spineCount, 0);
    int matched = zip.fillUncompressedSizes(targets, spineSizes);
    LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", matched, spineCount);

    targets.clear();
    targets.shrink_to_fit();

    useBatchSizes = true;
  }

  uint32_t cumSize = 0;
  spineIn.seek(0);
  int lastSpineTocIndex = -1;
  for (int i = 0; i < spineCount; i++) {
    auto spineEntry = readSpineEntryFrom(spineIn);

    spineEntry.tocIndex = spineToTocIndex[i];

    // Not a huge deal if we don't fine a TOC entry for the spine entry, this is expected behaviour for EPUBs
    // Logging here is for debugging
    if (spineEntry.tocIndex == -1) {
      LOG_DBG("BMC", "Warning: Could not find TOC entry for spine item %d: %s, using title from last section", i,
              spineEntry.href.c_str());
      spineEntry.tocIndex = lastSpineTocIndex;
    }
    lastSpineTocIndex = spineEntry.tocIndex;

    size_t itemSize = 0;
    if (useBatchSizes) {
      itemSize = spineSizes[i];
      if (itemSize == 0) {
        const std::string path = FsHelpers::normalisePath(spineEntry.href);
        if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
          LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", path.c_str());
        }
      }
    } else {
      const std::string path = FsHelpers::normalisePath(spineEntry.href);
      if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
        LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", path.c_str());
      }
    }

    cumSize += itemSize;
    spineEntry.cumulativeSize = cumSize;

    // Write out spine data to book.bin
    writeSpineEntryTo(bookOut, spineEntry);
  }
  // Close opened zip file
  zip.close();

  // Loop through toc entries from toc file writing to book.bin
  tocIn.seek(0);
  for (int i = 0; i < tocCount; i++) {
    auto tocEntry = readTocEntryFrom(tocIn);
    writeTocEntryTo(bookOut, tocEntry);
  }

  const bool written = bookOut.flush();

  // Explicit close() required: member variables persist beyond function scope
  const bool closed = bookFile.close();
  spineFile.close();
  tocFile.close();

  if (!written || !closed) {
    LOG_ERR("BMC", "Failed writing temporary book.bin");
    Storage.remove(tempBookPath.c_str());
    return false;
  }

  const std::string finalBookPath = cachePath + bookBinFile;
  const std::string backupBookPath = cachePath + bookBinBackupFile;
  const bool hadFinal = Storage.exists(finalBookPath.c_str());
  if (hadFinal) {
    Storage.remove(backupBookPath.c_str());
    if (!Storage.rename(finalBookPath.c_str(), backupBookPath.c_str())) {
      LOG_ERR("BMC", "Could not back up existing book.bin");
      Storage.remove(tempBookPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(tempBookPath.c_str(), finalBookPath.c_str())) {
    LOG_ERR("BMC", "Could not publish temporary book.bin");
    if (hadFinal && !Storage.rename(backupBookPath.c_str(), finalBookPath.c_str())) {
      LOG_ERR("BMC", "Could not restore previous book.bin");
    }
    Storage.remove(tempBookPath.c_str());
    return false;
  }
  Storage.remove(backupBookPath.c_str());

  LOG_DBG("BMC", "Successfully built book.bin");
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  const auto spineBinFile = cachePath + tmpSpineBinFile;
  if (Storage.exists(spineBinFile.c_str())) {
    Storage.remove(spineBinFile.c_str());
  }
  const auto tocBinFile = cachePath + tmpTocBinFile;
  if (Storage.exists(tocBinFile.c_str())) {
    Storage.remove(tocBinFile.c_str());
  }
  Storage.remove((cachePath + bookBinTempFile).c_str());
  return true;
}

uint32_t BookMetadataCache::writeSpineEntry(HalFile& file, const SpineEntry& entry) const {
  return writeSpineEntryTo(file, entry);
}

uint32_t BookMetadataCache::writeTocEntry(HalFile& file, const TocEntry& entry) const {
  return writeTocEntryTo(file, entry);
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    LOG_DBG("BMC", "createSpineEntry called but not in build mode");
    return;
  }

  const SpineEntry entry(href, 0, -1);
  if (passOut) {
    writeSpineEntryTo(*passOut, entry);
  } else {
    writeSpineEntry(spineFile, entry);
  }
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile || !spineFile) {
    LOG_DBG("BMC", "createTocEntry called but not in build mode");
    return;
  }

  int16_t spineIndex = -1;

  if (useSpineHrefIndex) {
    uint64_t targetHash = fnvHash64(href);
    uint16_t targetLen = static_cast<uint16_t>(href.size());

    auto it =
        std::lower_bound(spineHrefIndex.begin(), spineHrefIndex.end(), SpineHrefIndexEntry{targetHash, targetLen, 0},
                         [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                           return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
                         });

    while (it != spineHrefIndex.end() && it->hrefHash == targetHash && it->hrefLen == targetLen) {
      spineIndex = it->spineIndex;
      break;
    }

    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  } else {
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto spineEntry = readSpineEntry(spineFile);
      if (spineEntry.href == href) {
        spineIndex = static_cast<int16_t>(i);
        break;
      }
    }
    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  }

  // Compose the title to NFC at index time so the cache stores precomposed glyphs;
  // device fonts have no combining-mark positioning, so NFD titles render broken.
  const TocEntry entry(utf8ComposeNfc(title), href, anchor, level, spineIndex);
  if (passOut) {
    writeTocEntryTo(*passOut, entry);
  } else {
    writeTocEntry(tocFile, entry);
  }
  tocCount++;
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  loaded = false;
  sourceMismatch = false;
  cumulativeSizes.clear();
  bookFile.close();

  const std::string finalPath = cachePath + bookBinFile;
  const std::string backupPath = cachePath + bookBinBackupFile;
  if (!Storage.openFileForRead("BMC", finalPath, bookFile)) {
    // A power loss between final->backup and tmp->final leaves a valid backup
    // and no canonical file. Restore that state before deciding to rebuild.
    if (!Storage.exists(backupPath.c_str()) || !Storage.rename(backupPath.c_str(), finalPath.c_str()) ||
        !Storage.openFileForRead("BMC", finalPath, bookFile)) {
      return false;
    }
  }

  const auto fail = [&]() {
    bookFile.close();
    cumulativeSizes.clear();
    loaded = false;
    return false;
  };

  uint8_t version = 0;
  SourceFingerprint cachedFingerprint;
  if (!serialization::tryReadPod(bookFile, version)) return fail();
  if (version != BOOK_CACHE_VERSION) {
    LOG_DBG("BMC", "Cache version mismatch: expected %d, got %d", BOOK_CACHE_VERSION, version);
    return fail();
  }
  if (!serialization::tryReadPod(bookFile, lutOffset) || !serialization::tryReadPod(bookFile, spineCount) ||
      !serialization::tryReadPod(bookFile, tocCount) ||
      !serialization::tryReadPod(bookFile, cachedFingerprint.fileSize) ||
      !serialization::tryReadPod(bookFile, cachedFingerprint.centralDirectorySize) ||
      !serialization::tryReadPod(bookFile, cachedFingerprint.centralDirectoryHash) || spineCount > MAX_SPINE_COUNT ||
      tocCount > MAX_TOC_COUNT || !serialization::tryReadString(bookFile, coreMetadata.title, MAX_METADATA_TEXT) ||
      !serialization::tryReadString(bookFile, coreMetadata.author, MAX_METADATA_TEXT) ||
      !serialization::tryReadString(bookFile, coreMetadata.language, MAX_METADATA_TEXT) ||
      !serialization::tryReadString(bookFile, coreMetadata.coverItemHref, MAX_HREF_TEXT) ||
      !serialization::tryReadString(bookFile, coreMetadata.textReferenceHref, MAX_HREF_TEXT)) {
    LOG_ERR("BMC", "Invalid book metadata cache header");
    return fail();
  }

  SourceFingerprint actualFingerprint;
  if (!computeSourceFingerprint(sourcePath, actualFingerprint)) {
    LOG_ERR("BMC", "Could not fingerprint EPUB while loading cache");
    return fail();
  }
  if (!(actualFingerprint == cachedFingerprint)) {
    LOG_INF("BMC", "EPUB source changed at the same path; invalidating cache");
    sourceMismatch = true;
    return fail();
  }

  const uint64_t fileSize = bookFile.fileSize64();
  const uint64_t lutCount = static_cast<uint64_t>(spineCount) + tocCount;
  const uint64_t lutBytes = lutCount * sizeof(uint32_t);
  const uint64_t dataStart = static_cast<uint64_t>(lutOffset) + lutBytes;
  if (bookFile.position() != lutOffset || dataStart > fileSize || !bookFile.seek(lutOffset)) {
    LOG_ERR("BMC", "Invalid book metadata cache offsets");
    return fail();
  }

  uint32_t previousOffset = 0;
  for (uint64_t i = 0; i < lutCount; ++i) {
    uint32_t offset = 0;
    if (!serialization::tryReadPod(bookFile, offset) || offset < dataStart || offset >= fileSize ||
        (i == 0 ? offset != dataStart : offset <= previousOffset)) {
      LOG_ERR("BMC", "Invalid book metadata cache LUT");
      return fail();
    }
    previousOffset = offset;
  }

  if (!bookFile.seek(static_cast<size_t>(dataStart))) return fail();
  cumulativeSizes.reserve(spineCount);
  uint32_t previousCumulativeSize = 0;
  for (uint16_t i = 0; i < spineCount; ++i) {
    SpineEntry entry;
    if (!tryReadSpineEntry(bookFile, entry) || entry.tocIndex < -1 || entry.tocIndex >= tocCount ||
        entry.cumulativeSize < previousCumulativeSize) {
      LOG_ERR("BMC", "Invalid spine entry in book metadata cache");
      return fail();
    }
    previousCumulativeSize = entry.cumulativeSize;
    cumulativeSizes.push_back(entry.cumulativeSize);
  }
  for (uint16_t i = 0; i < tocCount; ++i) {
    TocEntry entry;
    if (!tryReadTocEntry(bookFile, entry) || entry.spineIndex < -1 || entry.spineIndex >= spineCount) {
      LOG_ERR("BMC", "Invalid TOC entry in book metadata cache");
      return fail();
    }
  }
  if (bookFile.position() != fileSize) {
    LOG_ERR("BMC", "Book metadata cache has trailing or missing data");
    return fail();
  }

  loaded = true;
  LOG_DBG("BMC", "Loaded cache data: %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

uint32_t BookMetadataCache::getCumulativeSize(const int index) const {
  if (index < 0 || index >= static_cast<int>(cumulativeSizes.size())) {
    return 0;
  }
  return cumulativeSizes[index];
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getSpineEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    LOG_ERR("BMC", "getSpineEntry index %d out of range", index);
    return {};
  }

  // Seek to spine LUT item, read from LUT and get out data
  const uint64_t fileSize = bookFile.fileSize64();
  const size_t lutPosition = lutOffset + sizeof(uint32_t) * static_cast<size_t>(index);
  uint32_t spineEntryPos = 0;
  SpineEntry entry;
  if (!bookFile.seek(lutPosition) || !serialization::tryReadPod(bookFile, spineEntryPos) || spineEntryPos >= fileSize ||
      !bookFile.seek(spineEntryPos) || !tryReadSpineEntry(bookFile, entry)) {
    LOG_ERR("BMC", "Failed reading spine entry %d", index);
    return {};
  }
  return entry;
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getTocEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    LOG_ERR("BMC", "getTocEntry index %d out of range", index);
    return {};
  }

  // Seek to TOC LUT item, read from LUT and get out data
  const uint64_t fileSize = bookFile.fileSize64();
  const size_t lutPosition =
      lutOffset + sizeof(uint32_t) * static_cast<size_t>(spineCount) + sizeof(uint32_t) * static_cast<size_t>(index);
  uint32_t tocEntryPos = 0;
  TocEntry entry;
  if (!bookFile.seek(lutPosition) || !serialization::tryReadPod(bookFile, tocEntryPos) || tocEntryPos >= fileSize ||
      !bookFile.seek(tocEntryPos) || !tryReadTocEntry(bookFile, entry)) {
    LOG_ERR("BMC", "Failed reading TOC entry %d", index);
    return {};
  }
  return entry;
}

BookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry(HalFile& file) const {
  return readSpineEntryFrom(file);
}

BookMetadataCache::TocEntry BookMetadataCache::readTocEntry(HalFile& file) const { return readTocEntryFrom(file); }
