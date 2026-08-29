#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SavedItemsCatalog {

struct Entry {
  std::string sourcePath;
  std::string title;
  std::string author;
  uint16_t bookmarkCount = 0;
  uint16_t clippingCount = 0;

  bool operator==(const Entry&) const = default;
};

enum class CodecStatus { OK, TRUNCATED, INVALID, BAD_CRC, NEWER_VERSION, LIMIT_EXCEEDED };
enum class LoadStatus { LOADED, LOADED_TEMP, LOADED_BACKUP, MISSING, INVALID, NEWER_VERSION, IO_ERROR };
enum class SaveStatus { SAVED, INVALID, NEWER_VERSION, IO_ERROR };

constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_SIZE = 16;
constexpr size_t RECORD_HEADER_SIZE = 12;
constexpr size_t MAX_BOOKS = 128;
constexpr size_t MAX_PATH_BYTES = 512;
constexpr size_t MAX_TITLE_BYTES = 256;
constexpr size_t MAX_AUTHOR_BYTES = 256;
constexpr size_t MAX_FILE_BYTES = 96 * 1024;
constexpr const char* FILE_PATH = "/.crosspoint/saved-items.bin";

CodecStatus encode(const std::vector<Entry>& entries, std::vector<uint8_t>& bytes);
CodecStatus decode(const uint8_t* bytes, size_t length, std::vector<Entry>& entries);

LoadStatus load(std::vector<Entry>& entries);
SaveStatus save(const std::vector<Entry>& entries);

// Synchronize both counters after opening a book, or one counter immediately
// after its store changes. Metadata is retained when an empty title/author is
// supplied. Empty books are removed from the catalog.
bool syncBook(const std::string& sourcePath, const std::string& title, const std::string& author, size_t bookmarkCount,
              size_t clippingCount);
bool updateBookmarks(const std::string& sourcePath, const std::string& title, const std::string& author,
                     size_t bookmarkCount);
bool updateClippings(const std::string& sourcePath, const std::string& title, const std::string& author,
                     size_t clippingCount);
bool migrateBook(const std::string& oldSourcePath, const std::string& newSourcePath);

}  // namespace SavedItemsCatalog
