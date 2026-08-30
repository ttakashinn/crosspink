#include "DictionaryHistoryStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

#include "DictionaryQuery.h"

namespace {

constexpr char HEADER[] = "VNS_DICT_HISTORY_V1\n";
constexpr char HEADER_PREFIX[] = "VNS_DICT_HISTORY_V";

enum class ReadStatus { OK, MISSING, INVALID, NEWER_VERSION, IO_ERROR };

std::string parentPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

ReadStatus readFile(const std::string& path, std::vector<std::string>& entries) {
  HalFile file;
  if (!Storage.openFileForRead("DHIST", path, file)) {
    return Storage.exists(path.c_str()) ? ReadStatus::IO_ERROR : ReadStatus::MISSING;
  }
  const size_t size = file.fileSize();
  if (size == 0 || size > DictionaryHistoryStore::MAX_FILE_BYTES) {
    file.close();
    return ReadStatus::INVALID;
  }
  std::string data(size, '\0');
  if (file.read(data.data(), size) != static_cast<int>(size) || !file.close()) return ReadStatus::IO_ERROR;
  switch (DictionaryHistoryStore::decode(reinterpret_cast<const uint8_t*>(data.data()), data.size(), entries)) {
    case DictionaryHistoryStore::CodecStatus::OK:
      return ReadStatus::OK;
    case DictionaryHistoryStore::CodecStatus::NEWER_VERSION:
      return ReadStatus::NEWER_VERSION;
    case DictionaryHistoryStore::CodecStatus::INVALID:
    case DictionaryHistoryStore::CodecStatus::LIMIT_EXCEEDED:
      return ReadStatus::INVALID;
  }
  return ReadStatus::INVALID;
}

bool fileMatches(const std::string& path, const std::string& expected) {
  HalFile file;
  if (!Storage.openFileForRead("DHIST", path, file) || file.fileSize() != expected.size()) return false;
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

bool writeVerified(const std::string& path, const std::string& data) {
  HalFile file;
  if (!Storage.openFileForWrite("DHIST", path, file)) return false;
  bool ok = file.write(data.data(), data.size()) == data.size();
  file.flush();
  if (!file.close()) ok = false;
  return ok && fileMatches(path, data);
}

}  // namespace

DictionaryHistoryStore& DictionaryHistoryStore::getInstance() {
  static DictionaryHistoryStore instance;
  return instance;
}

DictionaryHistoryStore::CodecStatus DictionaryHistoryStore::encode(const std::vector<std::string>& entries,
                                                                   std::string& data) {
  data.clear();
  if (entries.size() > MAX_ENTRIES) return CodecStatus::LIMIT_EXCEEDED;
  data = HEADER;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    if (entry.empty() || entry.size() > MAX_QUERY_BYTES || !utf8IsValid(entry) ||
        DictionaryQuery::clean(entry) != entry ||
        std::find(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(i), entry) !=
            entries.begin() + static_cast<std::ptrdiff_t>(i)) {
      data.clear();
      return CodecStatus::INVALID;
    }
    if (data.size() + entry.size() + 1 > MAX_FILE_BYTES) {
      data.clear();
      return CodecStatus::LIMIT_EXCEEDED;
    }
    data += entry;
    data.push_back('\n');
  }
  return CodecStatus::OK;
}

DictionaryHistoryStore::CodecStatus DictionaryHistoryStore::decode(const uint8_t* bytes, const size_t size,
                                                                   std::vector<std::string>& entries) {
  entries.clear();
  if (!bytes || size < sizeof(HEADER) - 1 || size > MAX_FILE_BYTES) return CodecStatus::INVALID;
  if (size >= sizeof(HEADER_PREFIX) - 1 && memcmp(bytes, HEADER_PREFIX, sizeof(HEADER_PREFIX) - 1) == 0 &&
      memcmp(bytes, HEADER, sizeof(HEADER) - 1) != 0) {
    return CodecStatus::NEWER_VERSION;
  }
  if (memcmp(bytes, HEADER, sizeof(HEADER) - 1) != 0) return CodecStatus::INVALID;

  size_t start = sizeof(HEADER) - 1;
  while (start < size) {
    size_t end = start;
    while (end < size && bytes[end] != '\n') ++end;
    if (end == start || end - start > MAX_QUERY_BYTES || entries.size() >= MAX_ENTRIES) {
      entries.clear();
      return CodecStatus::INVALID;
    }
    std::string entry(reinterpret_cast<const char*>(bytes + start), end - start);
    if (!utf8IsValid(entry) || DictionaryQuery::clean(entry) != entry ||
        std::find(entries.begin(), entries.end(), entry) != entries.end()) {
      entries.clear();
      return CodecStatus::INVALID;
    }
    entries.push_back(std::move(entry));
    start = end + (end < size ? 1 : 0);
  }
  return CodecStatus::OK;
}

std::string DictionaryHistoryStore::candidatePath(const char* suffix) const { return path_ + suffix; }

bool DictionaryHistoryStore::load() {
  if (loaded_) return writable_;
  loaded_ = true;
  writable_ = true;
  entries_.clear();

  constexpr std::array<const char*, 3> suffixes = {"", ".tmp", ".bak"};
  std::array<ReadStatus, 3> statuses{};
  std::array<std::vector<std::string>, 3> decoded{};
  for (size_t i = 0; i < suffixes.size(); ++i) statuses[i] = readFile(candidatePath(suffixes[i]), decoded[i]);

  const bool hardFailure = std::any_of(statuses.begin(), statuses.end(), [](const ReadStatus status) {
    return status == ReadStatus::NEWER_VERSION || status == ReadStatus::IO_ERROR;
  });
  int chosen = -1;
  for (size_t i = 0; i < statuses.size(); ++i) {
    if (statuses[i] == ReadStatus::OK) {
      chosen = static_cast<int>(i);
      break;
    }
  }
  if (chosen >= 0) entries_ = std::move(decoded[chosen]);

  // A malformed primary is preserved read-only. An invalid .tmp beside a
  // valid primary/backup is only evidence of an interrupted write and may be
  // replaced on the next flush; this is the recovery path atomic writes need.
  const bool invalidPrimary = statuses[0] == ReadStatus::INVALID;
  const bool invalidWithoutRecovery =
      chosen < 0 &&
      std::any_of(
          statuses.begin(), statuses.end(), [](const ReadStatus status) { return status == ReadStatus::INVALID; });
  writable_ = !hardFailure && !invalidPrimary && !invalidWithoutRecovery;
  if (!writable_) LOG_ERR("DHIST", "History has an invalid or unreadable generation; preserving it read-only");
  return writable_;
}

const std::vector<std::string>& DictionaryHistoryStore::entries() {
  load();
  return entries_;
}

void DictionaryHistoryStore::record(const std::string& query) {
  load();
  if (!writable_) return;
  const std::string cleaned = DictionaryQuery::clean(query);
  if (cleaned.empty() || cleaned.size() > MAX_QUERY_BYTES) return;
  if (!entries_.empty() && entries_.front() == cleaned) return;
  entries_.erase(std::remove(entries_.begin(), entries_.end(), cleaned), entries_.end());
  entries_.insert(entries_.begin(), cleaned);
  if (entries_.size() > MAX_ENTRIES) entries_.resize(MAX_ENTRIES);
  dirty_ = true;
}

bool DictionaryHistoryStore::flush() {
  load();
  if (!writable_) return false;
  if (!dirty_) return true;

  std::string data;
  if (encode(entries_, data) != CodecStatus::OK) return false;
  const std::string tempPath = candidatePath(".tmp");
  const std::string backupPath = candidatePath(".bak");
  if (!Storage.exists(parentPath(path_).c_str()) && !Storage.mkdir(parentPath(path_).c_str())) return false;
  if (!removeIfPresent(tempPath) || !writeVerified(tempPath, data)) return false;

  bool rotatedFinal = false;
  if (Storage.exists(path_.c_str())) {
    std::vector<std::string> existing;
    if (readFile(path_, existing) != ReadStatus::OK) {
      // load() would normally have made this read-only. Re-check protects
      // against the SD contents changing while the firmware is running.
      removeIfPresent(tempPath);
      writable_ = false;
      return false;
    }
    if (!removeIfPresent(backupPath) || !Storage.rename(path_.c_str(), backupPath.c_str())) {
      removeIfPresent(tempPath);
      return false;
    }
    rotatedFinal = true;
  }
  if (!Storage.rename(tempPath.c_str(), path_.c_str())) {
    if (rotatedFinal && !Storage.exists(path_.c_str())) Storage.rename(backupPath.c_str(), path_.c_str());
    return false;
  }
  if (!fileMatches(path_, data)) return false;
  dirty_ = false;
  return true;
}

bool DictionaryHistoryStore::clear() {
  load();
  if (!writable_) return false;
  auto previous = std::move(entries_);
  const bool previousDirty = dirty_;
  entries_.clear();
  dirty_ = true;
  if (flush()) return true;
  entries_ = std::move(previous);
  dirty_ = previousDirty;
  return false;
}
