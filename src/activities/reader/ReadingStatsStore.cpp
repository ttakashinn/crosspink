#include "ReadingStatsStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <array>
#include <cstdint>
#include <string>

#include "ReadingStatsCodec.h"

namespace ReadingStatsStore {
namespace {

template <typename Codec, typename Stats>
LoadStatus readOne(const std::string& path, Stats& stats) {
  HalFile file;
  if (!Storage.openFileForRead("RSTAT", path, file)) {
    return Storage.exists(path.c_str()) ? LoadStatus::IO_ERROR : LoadStatus::MISSING;
  }
  typename Codec::Encoded bytes{};
  const size_t size = file.fileSize();
  const size_t toRead = std::min(size, bytes.size());
  if (file.read(bytes.data(), toRead) != static_cast<int>(toRead) || !file.close()) {
    return LoadStatus::IO_ERROR;
  }
  switch (Codec::decode(bytes.data(), size, stats)) {
    case ReadingStatsCodec::DecodeStatus::OK:
      return LoadStatus::LOADED;
    case ReadingStatsCodec::DecodeStatus::NEWER_VERSION:
      return LoadStatus::NEWER_VERSION;
    case ReadingStatsCodec::DecodeStatus::INVALID:
    default:
      return LoadStatus::INVALID;
  }
}

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

template <typename Codec, typename Stats>
LoadStatus loadRecoverable(const std::string& finalPath, Stats& stats) {
  // Canonical is authoritative when present. If publication was interrupted
  // after canonical -> backup, the complete temp generation is newer than the
  // backup and must be preferred; otherwise a reboot silently loses the most
  // recent reading session.
  const std::array<std::string, 3> paths = {finalPath, finalPath + ".tmp", finalPath + ".bak"};
  bool invalidSeen = false;
  bool ioErrorSeen = false;
  bool loaded = false;
  Stats firstValid{};
  for (const auto& path : paths) {
    Stats candidate{};
    const auto status = readOne<Codec>(path, candidate);
    // Inspect every recovery candidate before accepting an older valid copy.
    // Otherwise a v1 canonical file can hide an interrupted v2 publish in
    // .bak/.tmp and a subsequent save would silently downgrade it.
    if (status == LoadStatus::NEWER_VERSION) return status;
    if (status == LoadStatus::LOADED && !loaded) {
      firstValid = candidate;
      loaded = true;
    }
    ioErrorSeen |= status == LoadStatus::IO_ERROR;
    invalidSeen |= status == LoadStatus::INVALID;
  }
  if (ioErrorSeen) return LoadStatus::IO_ERROR;
  if (loaded) {
    stats = firstValid;
    return LoadStatus::LOADED;
  }
  return invalidSeen ? LoadStatus::INVALID : LoadStatus::MISSING;
}

template <typename Codec, typename Stats>
SaveStatus saveAtomic(const std::string& finalPath, const Stats& stats) {
  Stats existing{};
  const auto current = loadRecoverable<Codec>(finalPath, existing);
  if (current == LoadStatus::NEWER_VERSION) return SaveStatus::NEWER_VERSION;
  if (current == LoadStatus::IO_ERROR) return SaveStatus::IO_ERROR;

  const std::string tempPath = finalPath + ".tmp";
  const std::string backupPath = finalPath + ".bak";
  if (!removeIfPresent(tempPath)) return SaveStatus::IO_ERROR;

  const auto bytes = Codec::encode(stats);
  HalFile file;
  if (!Storage.openFileForWrite("RSTAT", tempPath, file)) return SaveStatus::IO_ERROR;
  bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  ok = file.close() && ok;
  Stats verified{};
  ok = ok && readOne<Codec>(tempPath, verified) == LoadStatus::LOADED && verified == stats;
  if (!ok) {
    removeIfPresent(tempPath);
    return SaveStatus::IO_ERROR;
  }

  Stats canonical{};
  const bool finalValid = readOne<Codec>(finalPath, canonical) == LoadStatus::LOADED;
  const bool finalExists = Storage.exists(finalPath.c_str());
  if (finalValid) {
    if (!removeIfPresent(backupPath) || !Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      removeIfPresent(tempPath);
      return SaveStatus::IO_ERROR;
    }
  } else if (finalExists && !Storage.remove(finalPath.c_str())) {
    // Preserve a valid recovery backup when the canonical generation is
    // corrupt. Replacing the backup with corrupt bytes makes a failed publish
    // unrecoverable.
    removeIfPresent(tempPath);
    return SaveStatus::IO_ERROR;
  }
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    if (finalValid && !Storage.exists(finalPath.c_str())) Storage.rename(backupPath.c_str(), finalPath.c_str());
    return SaveStatus::IO_ERROR;
  }
  if (readOne<Codec>(finalPath, verified) != LoadStatus::LOADED || verified != stats) {
    LOG_ERR("RSTAT", "Published reading stats failed verification");
    return SaveStatus::IO_ERROR;
  }
  removeIfPresent(backupPath);
  return SaveStatus::SAVED;
}

std::string legacyBookPath(const std::string& cachePath) {
  return cachePath + (cachePath.empty() || cachePath.back() == '/' ? "" : "/") + BOOK_FILE_NAME;
}

uint64_t fnv1a64(const std::string& value) {
  uint64_t hash = 14695981039346656037ULL;
  for (const char byte : value) {
    hash ^= static_cast<uint8_t>(byte);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string parentPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

std::string durableDirectory(const std::string& legacyCachePath) {
  const std::string root = parentPath(legacyCachePath);
  return root + (root == "/" ? "" : "/") + "reading-stats";
}

std::string durableBookPath(const std::string& sourcePath, const std::string& legacyCachePath,
                            const char* suffix = "") {
  return durableDirectory(legacyCachePath) + "/epub_" + std::to_string(fnv1a64(sourcePath)) + ".bin" + suffix;
}

}  // namespace

LoadStatus loadBook(const std::string& sourcePath, const std::string& legacyCachePath, BookReadingStats& stats) {
  const LoadStatus durable =
      loadRecoverable<ReadingStatsCodec::BookCodec>(durableBookPath(sourcePath, legacyCachePath), stats);
  if (durable != LoadStatus::MISSING) return durable;

  const LoadStatus legacy = loadRecoverable<ReadingStatsCodec::BookCodec>(legacyBookPath(legacyCachePath), stats);
  if (legacy == LoadStatus::LOADED) {
    if (saveBook(sourcePath, legacyCachePath, stats) != SaveStatus::SAVED) {
      LOG_ERR("RSTAT", "Could not migrate per-book statistics out of the generated cache");
      return LoadStatus::IO_ERROR;
    }
    return LoadStatus::LOADED;
  }
  return legacy;
}

SaveStatus saveBook(const std::string& sourcePath, const std::string& legacyCachePath, const BookReadingStats& stats) {
  const std::string directory = durableDirectory(legacyCachePath);
  if (!Storage.exists(directory.c_str()) && !Storage.mkdir(directory.c_str())) return SaveStatus::IO_ERROR;
  return saveAtomic<ReadingStatsCodec::BookCodec>(durableBookPath(sourcePath, legacyCachePath), stats);
}

bool migrateBook(const std::string& oldSourcePath, const std::string& oldCachePath, const std::string& newSourcePath,
                 const std::string& newCachePath) {
  BookReadingStats stats;
  const LoadStatus status =
      loadRecoverable<ReadingStatsCodec::BookCodec>(durableBookPath(oldSourcePath, oldCachePath), stats);
  if (status == LoadStatus::MISSING) return true;
  if (status != LoadStatus::LOADED || saveBook(newSourcePath, newCachePath, stats) != SaveStatus::SAVED) return false;

  bool removed = true;
  for (const char* suffix : {"", ".bak", ".tmp"}) {
    removed = removeIfPresent(durableBookPath(oldSourcePath, oldCachePath, suffix)) && removed;
  }
  return removed;
}

LoadStatus loadGlobal(GlobalReadingStats& stats) {
  return loadRecoverable<ReadingStatsCodec::GlobalCodec>(GLOBAL_FILE_PATH, stats);
}

SaveStatus saveGlobal(const GlobalReadingStats& stats) {
  return saveAtomic<ReadingStatsCodec::GlobalCodec>(GLOBAL_FILE_PATH, stats);
}

}  // namespace ReadingStatsStore
