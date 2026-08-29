#include "PerBookReaderSettingsStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <array>
#include <string>

#include "PerBookReaderSettingsCodec.h"

namespace PerBookReaderSettingsStore {
namespace {

enum class ReadStatus { OK, MISSING, NEWER_VERSION, INVALID, IO_ERROR };

std::string pathFor(const std::string& cachePath, const char* suffix) {
  return cachePath + (cachePath.empty() || cachePath.back() == '/' ? "" : "/") + FILE_NAME + suffix;
}

ReadStatus read(const std::string& path, PerBookReaderSettings& settings) {
  HalFile file;
  if (!Storage.openFileForRead("PBRS", path, file)) {
    return Storage.exists(path.c_str()) ? ReadStatus::IO_ERROR : ReadStatus::MISSING;
  }
  const size_t size = file.fileSize();
  PerBookReaderSettingsCodec::Encoded bytes{};
  if (size > bytes.size()) {
    std::array<uint8_t, PerBookReaderSettingsCodec::VERSION_OFFSET + 1> prefix{};
    if (file.read(prefix.data(), prefix.size()) != static_cast<int>(prefix.size())) return ReadStatus::IO_ERROR;
    return std::equal(PerBookReaderSettingsCodec::MAGIC.begin(), PerBookReaderSettingsCodec::MAGIC.end(),
                      prefix.begin()) &&
                   prefix[PerBookReaderSettingsCodec::VERSION_OFFSET] > PerBookReaderSettingsCodec::VERSION
               ? ReadStatus::NEWER_VERSION
               : ReadStatus::INVALID;
  }
  if (file.read(bytes.data(), size) != static_cast<int>(size) || !file.close()) return ReadStatus::IO_ERROR;
  const auto status = PerBookReaderSettingsCodec::decode(bytes.data(), size, settings);
  if (status == PerBookReaderSettingsCodec::DecodeStatus::OK) return ReadStatus::OK;
  if (status == PerBookReaderSettingsCodec::DecodeStatus::NEWER_VERSION) return ReadStatus::NEWER_VERSION;
  return ReadStatus::INVALID;
}

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

bool writeVerified(const std::string& path, const PerBookReaderSettingsCodec::Encoded& bytes,
                   const PerBookReaderSettings& expected) {
  HalFile file;
  if (!Storage.openFileForWrite("PBRS", path, file)) return false;
  bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  if (!file.close()) ok = false;
  PerBookReaderSettings actual;
  return ok && read(path, actual) == ReadStatus::OK && actual == expected;
}

}  // namespace

LoadStatus load(const std::string& cachePath, PerBookReaderSettings& settings) {
  const std::array<std::pair<std::string, LoadStatus>, 3> candidates = {
      std::pair{pathFor(cachePath, ""), LoadStatus::LOADED},
      std::pair{pathFor(cachePath, ".bak"), LoadStatus::LOADED_BACKUP},
      std::pair{pathFor(cachePath, ".tmp"), LoadStatus::LOADED_TEMP},
  };
  std::array<PerBookReaderSettings, 3> decoded{};
  std::array<ReadStatus, 3> statuses{};
  bool anyInvalid = false;
  for (size_t i = 0; i < candidates.size(); ++i) {
    statuses[i] = read(candidates[i].first, decoded[i]);
    switch (statuses[i]) {
      case ReadStatus::OK:
        break;
      case ReadStatus::NEWER_VERSION:
        return LoadStatus::NEWER_VERSION;
      case ReadStatus::IO_ERROR:
        return LoadStatus::IO_ERROR;
      case ReadStatus::INVALID:
        anyInvalid = true;
        break;
      case ReadStatus::MISSING:
        break;
    }
  }
  // Inspect every recoverable generation before selecting one. Otherwise an
  // older canonical file could hide a newer backup/temp format that save()
  // would later refuse to overwrite.
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (statuses[i] == ReadStatus::OK) {
      settings = decoded[i];
      return candidates[i].second;
    }
  }
  return anyInvalid ? LoadStatus::INVALID : LoadStatus::MISSING;
}

SaveStatus save(const std::string& cachePath, const PerBookReaderSettings& settings) {
  PerBookReaderSettingsCodec::Encoded bytes;
  if (!PerBookReaderSettingsCodec::encode(settings, bytes)) return SaveStatus::INVALID_SETTINGS;

  const std::string finalPath = pathFor(cachePath, "");
  const std::string backupPath = pathFor(cachePath, ".bak");
  const std::string tempPath = pathFor(cachePath, ".tmp");
  bool finalValid = false;
  for (const std::string* path : {&finalPath, &backupPath, &tempPath}) {
    PerBookReaderSettings ignored;
    const ReadStatus status = read(*path, ignored);
    if (status == ReadStatus::NEWER_VERSION) return SaveStatus::NEWER_VERSION;
    if (status == ReadStatus::IO_ERROR) return SaveStatus::IO_ERROR;
    if (path == &finalPath) finalValid = status == ReadStatus::OK;
  }

  if (!removeIfPresent(tempPath) || !writeVerified(tempPath, bytes, settings)) return SaveStatus::IO_ERROR;
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
  PerBookReaderSettings verified;
  if (read(finalPath, verified) != ReadStatus::OK || verified != settings) {
    LOG_ERR("PBRS", "Published per-book settings failed verification");
    return SaveStatus::IO_ERROR;
  }
  return SaveStatus::SAVED;
}

}  // namespace PerBookReaderSettingsStore
