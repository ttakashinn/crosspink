#include "SavedItemsCatalog.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <array>
#include <limits>

namespace SavedItemsCatalog {
namespace {

constexpr std::array<uint8_t, 4> MAGIC = {'V', 'N', 'S', 'I'};
constexpr const char* TEMP_PATH = "/.crosspoint/saved-items.bin.tmp";
constexpr const char* BACKUP_PATH = "/.crosspoint/saved-items.bin.bak";

uint16_t get16(const uint8_t* data) { return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8); }

uint32_t get32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void put16(uint8_t* data, const uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t* data, const uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t crc32(const uint8_t* data, const size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

bool removeIfPresent(const char* path) { return !Storage.exists(path) || Storage.remove(path); }

bool hasFutureVersion(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("SAVED", path, file)) return false;
  std::array<uint8_t, 5> prefix{};
  const bool read = file.read(prefix.data(), prefix.size()) == static_cast<int>(prefix.size());
  file.close();
  return read && std::equal(MAGIC.begin(), MAGIC.end(), prefix.begin()) && prefix[4] > VERSION;
}

CodecStatus readFile(const char* path, std::vector<Entry>& entries) {
  HalFile file;
  if (!Storage.openFileForRead("SAVED", path, file)) {
    return Storage.exists(path) ? CodecStatus::INVALID : CodecStatus::TRUNCATED;
  }
  const size_t length = file.fileSize();
  if (length < HEADER_SIZE || length > MAX_FILE_BYTES) {
    file.close();
    return CodecStatus::INVALID;
  }
  std::vector<uint8_t> bytes(length);
  if (file.read(bytes.data(), bytes.size()) != static_cast<int>(bytes.size()) || !file.close()) {
    return CodecStatus::INVALID;
  }
  return decode(bytes.data(), bytes.size(), entries);
}

bool fileMatches(const char* path, const std::vector<uint8_t>& expected) {
  HalFile file;
  if (!Storage.openFileForRead("SAVED", path, file) || file.fileSize() != expected.size()) return false;
  std::array<uint8_t, 128> chunk{};
  size_t offset = 0;
  while (offset < expected.size()) {
    const size_t count = std::min(chunk.size(), expected.size() - offset);
    if (file.read(chunk.data(), count) != static_cast<int>(count) ||
        !std::equal(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count), expected.begin() + offset)) {
      file.close();
      return false;
    }
    offset += count;
  }
  return file.close();
}

bool writeVerified(const char* path, const std::vector<uint8_t>& bytes) {
  HalFile file;
  if (!Storage.openFileForWrite("SAVED", path, file)) return false;
  bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  if (!file.close()) ok = false;
  return ok && fileMatches(path, bytes);
}

std::string fallbackTitle(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name.resize(dot);
  return name.empty() ? path : name;
}

bool loadMutable(std::vector<Entry>& entries) {
  const LoadStatus status = load(entries);
  return status == LoadStatus::LOADED || status == LoadStatus::LOADED_TEMP || status == LoadStatus::LOADED_BACKUP ||
         status == LoadStatus::MISSING;
}

bool update(const std::string& sourcePath, const std::string& title, const std::string& author,
            const size_t* bookmarkCount, const size_t* clippingCount) {
  if (sourcePath.empty()) return false;
  std::vector<Entry> entries;
  if (!loadMutable(entries)) return false;

  auto it =
      std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) { return entry.sourcePath == sourcePath; });
  if (it == entries.end()) {
    if (entries.size() >= MAX_BOOKS) {
      LOG_ERR("SAVED", "Saved-items catalog is full");
      return false;
    }
    entries.insert(entries.begin(), Entry{sourcePath, title.empty() ? fallbackTitle(sourcePath) : title, author});
    it = entries.begin();
  }

  Entry changed = *it;
  if (!title.empty()) changed.title = title;
  if (!author.empty()) changed.author = author;
  if (changed.title.empty()) changed.title = fallbackTitle(sourcePath);
  if (bookmarkCount) {
    changed.bookmarkCount = static_cast<uint16_t>(std::min(*bookmarkCount, static_cast<size_t>(UINT16_MAX)));
  }
  if (clippingCount) {
    changed.clippingCount = static_cast<uint16_t>(std::min(*clippingCount, static_cast<size_t>(UINT16_MAX)));
  }

  if (changed.bookmarkCount == 0 && changed.clippingCount == 0) {
    entries.erase(it);
  } else if (changed == *it) {
    return true;
  } else {
    entries.erase(it);
    entries.insert(entries.begin(), std::move(changed));
  }
  return save(entries) == SaveStatus::SAVED;
}

}  // namespace

CodecStatus encode(const std::vector<Entry>& entries, std::vector<uint8_t>& bytes) {
  bytes.clear();
  if (entries.size() > MAX_BOOKS) return CodecStatus::LIMIT_EXCEEDED;
  size_t payloadSize = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    if (entry.sourcePath.empty() || entry.sourcePath.size() > MAX_PATH_BYTES || entry.title.size() > MAX_TITLE_BYTES ||
        entry.author.size() > MAX_AUTHOR_BYTES || !utf8IsValid(entry.sourcePath) || !utf8IsValid(entry.title) ||
        !utf8IsValid(entry.author) || entry.bookmarkCount + entry.clippingCount == 0) {
      return CodecStatus::INVALID;
    }
    if (std::any_of(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(i),
                    [&](const Entry& previous) { return previous.sourcePath == entry.sourcePath; })) {
      return CodecStatus::INVALID;
    }
    payloadSize += RECORD_HEADER_SIZE + entry.sourcePath.size() + entry.title.size() + entry.author.size();
  }
  if (HEADER_SIZE + payloadSize > MAX_FILE_BYTES || payloadSize > std::numeric_limits<uint32_t>::max()) {
    return CodecStatus::LIMIT_EXCEEDED;
  }

  bytes.assign(HEADER_SIZE + payloadSize, 0);
  std::copy(MAGIC.begin(), MAGIC.end(), bytes.begin());
  bytes[4] = VERSION;
  bytes[5] = 0;
  put16(bytes.data() + 6, static_cast<uint16_t>(entries.size()));
  put32(bytes.data() + 8, static_cast<uint32_t>(payloadSize));
  size_t cursor = HEADER_SIZE;
  for (const auto& entry : entries) {
    put16(bytes.data() + cursor, static_cast<uint16_t>(entry.sourcePath.size()));
    put16(bytes.data() + cursor + 2, static_cast<uint16_t>(entry.title.size()));
    put16(bytes.data() + cursor + 4, static_cast<uint16_t>(entry.author.size()));
    put16(bytes.data() + cursor + 6, entry.bookmarkCount);
    put16(bytes.data() + cursor + 8, entry.clippingCount);
    put16(bytes.data() + cursor + 10, 0);
    cursor += RECORD_HEADER_SIZE;
    for (const std::string* value : {&entry.sourcePath, &entry.title, &entry.author}) {
      std::copy(value->begin(), value->end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor));
      cursor += value->size();
    }
  }
  put32(bytes.data() + 12, crc32(bytes.data() + HEADER_SIZE, payloadSize));
  return CodecStatus::OK;
}

CodecStatus decode(const uint8_t* bytes, const size_t length, std::vector<Entry>& entries) {
  entries.clear();
  if (!bytes || length < HEADER_SIZE) return CodecStatus::TRUNCATED;
  if (!std::equal(MAGIC.begin(), MAGIC.end(), bytes)) return CodecStatus::INVALID;
  if (bytes[4] > VERSION) return CodecStatus::NEWER_VERSION;
  if (bytes[4] != VERSION || bytes[5] != 0) return CodecStatus::INVALID;
  const uint16_t count = get16(bytes + 6);
  const uint32_t payloadSize = get32(bytes + 8);
  if (count > MAX_BOOKS || payloadSize > MAX_FILE_BYTES - HEADER_SIZE || length != HEADER_SIZE + payloadSize) {
    return CodecStatus::INVALID;
  }
  if (crc32(bytes + HEADER_SIZE, payloadSize) != get32(bytes + 12)) return CodecStatus::BAD_CRC;

  entries.reserve(count);
  size_t cursor = HEADER_SIZE;
  for (uint16_t i = 0; i < count; ++i) {
    if (length - cursor < RECORD_HEADER_SIZE) {
      entries.clear();
      return CodecStatus::TRUNCATED;
    }
    const uint16_t pathLength = get16(bytes + cursor);
    const uint16_t titleLength = get16(bytes + cursor + 2);
    const uint16_t authorLength = get16(bytes + cursor + 4);
    const uint16_t bookmarks = get16(bytes + cursor + 6);
    const uint16_t clippings = get16(bytes + cursor + 8);
    const uint16_t reserved = get16(bytes + cursor + 10);
    cursor += RECORD_HEADER_SIZE;
    const size_t stringsLength = static_cast<size_t>(pathLength) + titleLength + authorLength;
    if (pathLength == 0 || pathLength > MAX_PATH_BYTES || titleLength > MAX_TITLE_BYTES ||
        authorLength > MAX_AUTHOR_BYTES || bookmarks + clippings == 0 || reserved != 0 ||
        stringsLength > length - cursor) {
      entries.clear();
      return CodecStatus::INVALID;
    }
    Entry entry;
    entry.sourcePath.assign(reinterpret_cast<const char*>(bytes + cursor), pathLength);
    cursor += pathLength;
    entry.title.assign(reinterpret_cast<const char*>(bytes + cursor), titleLength);
    cursor += titleLength;
    entry.author.assign(reinterpret_cast<const char*>(bytes + cursor), authorLength);
    cursor += authorLength;
    entry.bookmarkCount = bookmarks;
    entry.clippingCount = clippings;
    if (!utf8IsValid(entry.sourcePath) || !utf8IsValid(entry.title) || !utf8IsValid(entry.author) ||
        std::any_of(entries.begin(), entries.end(),
                    [&](const Entry& previous) { return previous.sourcePath == entry.sourcePath; })) {
      entries.clear();
      return CodecStatus::INVALID;
    }
    entries.push_back(std::move(entry));
  }
  if (cursor != length) {
    entries.clear();
    return CodecStatus::INVALID;
  }
  return CodecStatus::OK;
}

LoadStatus load(std::vector<Entry>& entries) {
  entries.clear();
  struct Candidate {
    const char* path;
    LoadStatus loadedStatus;
  };
  constexpr std::array<Candidate, 3> candidates = {{{FILE_PATH, LoadStatus::LOADED},
                                                    {TEMP_PATH, LoadStatus::LOADED_TEMP},
                                                    {BACKUP_PATH, LoadStatus::LOADED_BACKUP}}};
  if (std::any_of(candidates.begin(), candidates.end(), [](const Candidate& candidate) {
        return Storage.exists(candidate.path) && hasFutureVersion(candidate.path);
      }))
    return LoadStatus::NEWER_VERSION;
  bool invalid = false;
  for (const auto& candidate : candidates) {
    if (!Storage.exists(candidate.path)) continue;
    std::vector<Entry> decoded;
    switch (readFile(candidate.path, decoded)) {
      case CodecStatus::OK:
        entries = std::move(decoded);
        return candidate.loadedStatus;
      case CodecStatus::NEWER_VERSION:
        return LoadStatus::NEWER_VERSION;
      case CodecStatus::TRUNCATED:
      case CodecStatus::INVALID:
      case CodecStatus::BAD_CRC:
      case CodecStatus::LIMIT_EXCEEDED:
        invalid = true;
        break;
    }
  }
  return invalid ? LoadStatus::INVALID : LoadStatus::MISSING;
}

SaveStatus save(const std::vector<Entry>& entries) {
  std::vector<uint8_t> bytes;
  const CodecStatus encoded = encode(entries, bytes);
  if (encoded != CodecStatus::OK) return SaveStatus::INVALID;

  constexpr std::array<const char*, 3> candidates = {FILE_PATH, TEMP_PATH, BACKUP_PATH};
  if (std::any_of(candidates.begin(), candidates.end(),
                  [](const char* path) { return Storage.exists(path) && hasFutureVersion(path); }))
    return SaveStatus::NEWER_VERSION;
  if (!Storage.exists("/.crosspoint") && !Storage.mkdir("/.crosspoint")) return SaveStatus::IO_ERROR;
  if (!removeIfPresent(TEMP_PATH) || !writeVerified(TEMP_PATH, bytes)) return SaveStatus::IO_ERROR;

  bool rotatedValidFinal = false;
  if (Storage.exists(FILE_PATH)) {
    std::vector<Entry> existing;
    if (readFile(FILE_PATH, existing) == CodecStatus::OK) {
      if (!removeIfPresent(BACKUP_PATH) || !Storage.rename(FILE_PATH, BACKUP_PATH)) {
        removeIfPresent(TEMP_PATH);
        return SaveStatus::IO_ERROR;
      }
      rotatedValidFinal = true;
    } else if (!Storage.remove(FILE_PATH)) {
      removeIfPresent(TEMP_PATH);
      return SaveStatus::IO_ERROR;
    }
  }
  if (!Storage.rename(TEMP_PATH, FILE_PATH)) {
    if (rotatedValidFinal && !Storage.exists(FILE_PATH)) Storage.rename(BACKUP_PATH, FILE_PATH);
    return SaveStatus::IO_ERROR;
  }
  if (fileMatches(FILE_PATH, bytes)) return SaveStatus::SAVED;

  // Never let a failed publication hide the last valid generation. If the
  // canonical file was already corrupt, its existing backup is deliberately
  // kept in place instead of being overwritten by bad data.
  removeIfPresent(FILE_PATH);
  if (rotatedValidFinal) Storage.rename(BACKUP_PATH, FILE_PATH);
  return SaveStatus::IO_ERROR;
}

bool syncBook(const std::string& sourcePath, const std::string& title, const std::string& author,
              const size_t bookmarkCount, const size_t clippingCount) {
  return update(sourcePath, title, author, &bookmarkCount, &clippingCount);
}

bool updateBookmarks(const std::string& sourcePath, const std::string& title, const std::string& author,
                     const size_t bookmarkCount) {
  return update(sourcePath, title, author, &bookmarkCount, nullptr);
}

bool updateClippings(const std::string& sourcePath, const std::string& title, const std::string& author,
                     const size_t clippingCount) {
  return update(sourcePath, title, author, nullptr, &clippingCount);
}

bool migrateBook(const std::string& oldSourcePath, const std::string& newSourcePath) {
  if (oldSourcePath == newSourcePath) return true;
  std::vector<Entry> entries;
  if (!loadMutable(entries)) return false;
  auto oldEntry = std::find_if(entries.begin(), entries.end(),
                               [&](const Entry& entry) { return entry.sourcePath == oldSourcePath; });
  if (oldEntry == entries.end()) return true;
  if (std::any_of(entries.begin(), entries.end(),
                  [&](const Entry& entry) { return entry.sourcePath == newSourcePath; })) {
    return false;
  }
  oldEntry->sourcePath = newSourcePath;
  return save(entries) == SaveStatus::SAVED;
}

}  // namespace SavedItemsCatalog
