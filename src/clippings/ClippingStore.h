#pragma once

#include <string>
#include <vector>

#include "ClippingCodec.h"

namespace ClippingStore {

enum class LoadStatus { LOADED, LOADED_BACKUP, LOADED_TEMP, MISSING, NEWER_VERSION, INVALID, IO_ERROR };
enum class SaveStatus { SAVED, NEWER_VERSION, INVALID, IO_ERROR };

// Clippings are user data, not a generated book cache. New generations live
// under the cache root's durable `clippings/` directory and are keyed by the
// source path. `legacyCachePath` is still accepted so vns.1 per-cache files can
// be migrated on first open without losing highlights.
LoadStatus load(const std::string& sourcePath, const std::string& legacyCachePath,
                std::vector<ClippingCodec::Record>& records);
SaveStatus save(const std::string& sourcePath, const std::string& legacyCachePath,
                const std::vector<ClippingCodec::Record>& records);

// Re-key durable data when the reader moves a completed book to /Read.
bool migrate(const std::string& oldSourcePath, const std::string& oldCachePath, const std::string& newSourcePath,
             const std::string& newCachePath);

}  // namespace ClippingStore
