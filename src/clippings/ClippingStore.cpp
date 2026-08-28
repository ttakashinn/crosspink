#include "ClippingStore.h"

#include <HalStorage.h>

#include <algorithm>
#include <array>

namespace ClippingStore {
namespace {

constexpr char FILE_NAME[] = "clippings-vns.bin";
enum class ReadStatus { OK, MISSING, NEWER_VERSION, INVALID, IO_ERROR };

std::string pathFor(const std::string& cachePath, const char* suffix) {
  return cachePath + (cachePath.empty() || cachePath.back() == '/' ? "" : "/") + FILE_NAME + suffix;
}
bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

ReadStatus read(const std::string& path, std::vector<ClippingCodec::Record>& records) {
  HalFile file;
  if (!Storage.openFileForRead("CLIP", path, file)) {
    return Storage.exists(path.c_str()) ? ReadStatus::IO_ERROR : ReadStatus::MISSING;
  }
  const size_t length = file.fileSize();
  if (length == 0 || length > ClippingCodec::MAX_FILE_BYTES) {
    if (length >= 5) {
      std::array<uint8_t, 5> prefix{};
      if (file.read(prefix.data(), prefix.size()) != static_cast<int>(prefix.size())) return ReadStatus::IO_ERROR;
      if (prefix[0] == 'V' && prefix[1] == 'N' && prefix[2] == 'S' && prefix[3] == 'C' &&
          prefix[4] > ClippingCodec::VERSION) {
        return ReadStatus::NEWER_VERSION;
      }
    }
    return ReadStatus::INVALID;
  }
  std::vector<uint8_t> bytes(length);
  if (file.read(bytes.data(), length) != static_cast<int>(length) || !file.close()) return ReadStatus::IO_ERROR;
  const auto status = ClippingCodec::decode(bytes.data(), bytes.size(), records);
  if (status == ClippingCodec::Status::OK) return ReadStatus::OK;
  if (status == ClippingCodec::Status::NEWER_VERSION) return ReadStatus::NEWER_VERSION;
  return ReadStatus::INVALID;
}

bool fileMatches(const std::string& path, const std::vector<uint8_t>& expected) {
  HalFile file;
  if (!Storage.openFileForRead("CLIP", path, file) || file.fileSize() != expected.size()) return false;
  std::array<uint8_t, 128> chunk{};
  size_t offset = 0;
  while (offset < expected.size()) {
    const size_t count = std::min(chunk.size(), expected.size() - offset);
    if (file.read(chunk.data(), count) != static_cast<int>(count) ||
        !std::equal(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count), expected.begin() + offset)) {
      return false;
    }
    offset += count;
  }
  return file.close();
}

bool writeVerified(const std::string& path, const std::vector<uint8_t>& bytes) {
  HalFile file;
  if (!Storage.openFileForWrite("CLIP", path, file)) return false;
  bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  if (!file.close()) ok = false;
  // Compare in small fixed chunks. Decoding here would temporarily retain the
  // cached records, encoded payload, readback payload and a second record set
  // at once -- an avoidable worst-case heap spike on ESP32-C3.
  return ok && fileMatches(path, bytes);
}

}  // namespace

LoadStatus load(const std::string& cachePath, std::vector<ClippingCodec::Record>& records) {
  const std::array<std::pair<std::string, LoadStatus>, 3> paths = {
      std::pair{pathFor(cachePath, ""), LoadStatus::LOADED},
      std::pair{pathFor(cachePath, ".bak"), LoadStatus::LOADED_BACKUP},
      std::pair{pathFor(cachePath, ".tmp"), LoadStatus::LOADED_TEMP},
  };
  bool invalid = false;
  for (const auto& [path, loaded] : paths) {
    switch (read(path, records)) {
      case ReadStatus::OK:
        return loaded;
      case ReadStatus::NEWER_VERSION:
        return LoadStatus::NEWER_VERSION;
      case ReadStatus::IO_ERROR:
        return LoadStatus::IO_ERROR;
      case ReadStatus::INVALID:
        invalid = true;
        break;
      case ReadStatus::MISSING:
        break;
    }
  }
  records.clear();
  return invalid ? LoadStatus::INVALID : LoadStatus::MISSING;
}

SaveStatus save(const std::string& cachePath, const std::vector<ClippingCodec::Record>& records) {
  std::vector<uint8_t> bytes;
  if (ClippingCodec::encode(records, bytes) != ClippingCodec::Status::OK) return SaveStatus::INVALID;
  const std::string finalPath = pathFor(cachePath, "");
  const std::string backupPath = pathFor(cachePath, ".bak");
  const std::string tempPath = pathFor(cachePath, ".tmp");

  bool finalValid = false;
  for (const std::string* path : {&finalPath, &backupPath, &tempPath}) {
    std::vector<ClippingCodec::Record> ignored;
    const ReadStatus status = read(*path, ignored);
    if (status == ReadStatus::NEWER_VERSION) return SaveStatus::NEWER_VERSION;
    if (status == ReadStatus::IO_ERROR) return SaveStatus::IO_ERROR;
    if (path == &finalPath) finalValid = status == ReadStatus::OK;
  }
  if (!removeIfPresent(tempPath) || !writeVerified(tempPath, bytes)) return SaveStatus::IO_ERROR;
  if (finalValid) {
    if (!removeIfPresent(backupPath) || !Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      removeIfPresent(tempPath);
      return SaveStatus::IO_ERROR;
    }
  } else if (!removeIfPresent(finalPath)) {
    removeIfPresent(tempPath);
    return SaveStatus::IO_ERROR;
  }
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    if (finalValid && !Storage.exists(finalPath.c_str())) Storage.rename(backupPath.c_str(), finalPath.c_str());
    return SaveStatus::IO_ERROR;
  }
  return fileMatches(finalPath, bytes) ? SaveStatus::SAVED : SaveStatus::IO_ERROR;
}

}  // namespace ClippingStore
