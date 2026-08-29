#pragma once

#include <string>

#include "ReadingStats.h"

namespace ReadingStatsStore {

enum class LoadStatus { LOADED, MISSING, INVALID, NEWER_VERSION, IO_ERROR };
enum class SaveStatus { SAVED, NEWER_VERSION, IO_ERROR };

inline constexpr const char* BOOK_FILE_NAME = "reading-stats.bin";
inline constexpr const char* GLOBAL_FILE_PATH = "/.crosspoint/reading-stats.bin";

// Per-book statistics are durable user data. They live outside generated book
// caches and are keyed by source path; legacyCachePath is used to locate and
// migrate the vns.1 cache-local generation.
LoadStatus loadBook(const std::string& sourcePath, const std::string& legacyCachePath, BookReadingStats& stats);
SaveStatus saveBook(const std::string& sourcePath, const std::string& legacyCachePath, const BookReadingStats& stats);
bool migrateBook(const std::string& oldSourcePath, const std::string& oldCachePath, const std::string& newSourcePath,
                 const std::string& newCachePath);
LoadStatus loadGlobal(GlobalReadingStats& stats);
SaveStatus saveGlobal(const GlobalReadingStats& stats);

}  // namespace ReadingStatsStore
