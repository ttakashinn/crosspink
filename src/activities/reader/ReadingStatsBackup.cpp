#include "ReadingStatsBackup.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ReadingStatsClock.h"
#include "ReadingStatsCodec.h"
#include "ReadingStatsStore.h"

namespace ReadingStatsBackup {
namespace {

constexpr const char* LOG_TAG = "RSBACK";

bool isBackupName(const char* name) {
  if (name == nullptr || strncmp(name, "stats_", 6) != 0) return false;
  const size_t length = strlen(name);
  return length > 10 && strcmp(name + length - 4, ".bin") == 0;
}

bool copyText(const char* source, char* target, const size_t targetLength) {
  if (target == nullptr || targetLength == 0) return false;
  const int written = snprintf(target, targetLength, "%s", source == nullptr ? "" : source);
  return written >= 0 && static_cast<size_t>(written) < targetLength;
}

bool parseSequence(const char* name, uint32_t& sequence) {
  constexpr const char* prefix = "stats_backup_";
  constexpr size_t prefixLength = 13;
  if (name == nullptr || strncmp(name, prefix, prefixLength) != 0) return false;
  const char* digits = name + prefixLength;
  const char* suffix = strstr(digits, ".bin");
  if (suffix == nullptr || suffix == digits || suffix[4] != '\0') return false;
  uint32_t value = 0;
  for (const char* cursor = digits; cursor < suffix; ++cursor) {
    if (!std::isdigit(static_cast<unsigned char>(*cursor))) return false;
    value = value * 10U + static_cast<uint32_t>(*cursor - '0');
  }
  sequence = value;
  return value > 0;
}

bool chooseName(char* out, const size_t outLength) {
  ReadingStatsLocalDateTime now;
  if (ReadingStatsClock::currentLocalDateTime(now)) {
    uint32_t year;
    uint8_t month;
    uint8_t day;
    if (ReadingStatsMath::splitDateKey(now.dateKey, year, month, day)) {
      const unsigned hour = static_cast<unsigned>(now.secondOfDay / 3600U);
      const unsigned minute = static_cast<unsigned>((now.secondOfDay / 60U) % 60U);
      char stem[48];
      const int stemLength =
          snprintf(stem, sizeof(stem), "stats_%04lu-%02u-%02u_%02u%02u", static_cast<unsigned long>(year),
                   static_cast<unsigned>(month), static_cast<unsigned>(day), hour, minute);
      if (stemLength <= 0 || static_cast<size_t>(stemLength) >= sizeof(stem)) return false;
      for (unsigned suffix = 0; suffix < 100; ++suffix) {
        const int written = suffix == 0 ? snprintf(out, outLength, "%s.bin", stem)
                                        : snprintf(out, outLength, "%s_%02u.bin", stem, suffix);
        if (written <= 0 || static_cast<size_t>(written) >= outLength) return false;
        const std::string candidate = std::string(BACKUP_DIRECTORY) + "/" + out;
        if (!Storage.exists(candidate.c_str())) return true;
      }
      return false;
    }
  }

  uint32_t maximum = 0;
  HalFile directory = Storage.open(BACKUP_DIRECTORY);
  if (directory && directory.isDirectory()) {
    char name[96];
    for (HalFile file = directory.openNextFile(); file; file = directory.openNextFile()) {
      const bool isDirectory = file.isDirectory();
      const size_t length = file.getName(name, sizeof(name));
      file.close();
      if (isDirectory || length == 0) continue;
      uint32_t sequence = 0;
      if (parseSequence(name, sequence)) maximum = std::max(maximum, sequence);
    }
    directory.close();
  }
  const int written = snprintf(out, outLength, "stats_backup_%03lu.bin", static_cast<unsigned long>(maximum + 1U));
  return written > 0 && static_cast<size_t>(written) < outLength;
}

bool writeVerified(const std::string& finalPath, const ReadingStatsCodec::GlobalCodec::Encoded& bytes,
                   const GlobalReadingStats& expected) {
  const std::string temporaryPath = finalPath + ".tmp";
  if (Storage.exists(temporaryPath.c_str()) && !Storage.remove(temporaryPath.c_str())) return false;

  HalFile file;
  if (!Storage.openFileForWrite(LOG_TAG, temporaryPath, file)) return false;
  bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  ok = file.close() && ok;
  if (!ok) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  if (Storage.exists(finalPath.c_str()) && !Storage.remove(finalPath.c_str())) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  if (!Storage.rename(temporaryPath.c_str(), finalPath.c_str())) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  HalFile verifyFile;
  if (!Storage.openFileForRead(LOG_TAG, finalPath, verifyFile)) return false;
  ReadingStatsCodec::GlobalCodec::Encoded verifiedBytes{};
  const size_t size = verifyFile.fileSize();
  ok = size == verifiedBytes.size() &&
       verifyFile.read(verifiedBytes.data(), verifiedBytes.size()) == static_cast<int>(verifiedBytes.size());
  ok = verifyFile.close() && ok;
  GlobalReadingStats verified;
  return ok &&
         ReadingStatsCodec::GlobalCodec::decode(verifiedBytes.data(), size, verified) ==
             ReadingStatsCodec::DecodeStatus::OK &&
         verified == expected;
}

}  // namespace

bool create(char* outFileName, const size_t outFileNameLength) {
  GlobalReadingStats stats;
  if (ReadingStatsStore::loadGlobal(stats) != ReadingStatsStore::LoadStatus::LOADED) {
    LOG_ERR(LOG_TAG, "No valid all-time statistics to back up");
    return false;
  }
  if (!Storage.ensureDirectoryExists(BACKUP_DIRECTORY)) return false;

  char fileName[64];
  if (!chooseName(fileName, sizeof(fileName))) return false;
  const std::string path = std::string(BACKUP_DIRECTORY) + "/" + fileName;
  if (!writeVerified(path, ReadingStatsCodec::GlobalCodec::encode(stats), stats)) return false;

  prune();
  if (outFileName != nullptr && outFileNameLength > 0) copyText(fileName, outFileName, outFileNameLength);
  LOG_DBG(LOG_TAG, "Wrote %s", path.c_str());
  return true;
}

int prune(const int keep) {
  if (keep < 0) return 0;
  HalFile directory = Storage.open(BACKUP_DIRECTORY);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return 0;
  }

  std::vector<std::string> names;
  char name[96];
  for (HalFile file = directory.openNextFile(); file; file = directory.openNextFile()) {
    const bool isDirectory = file.isDirectory();
    const size_t length = file.getName(name, sizeof(name));
    file.close();
    if (!isDirectory && length > 0 && isBackupName(name)) names.emplace_back(name);
  }
  directory.close();
  if (static_cast<int>(names.size()) <= keep) return 0;

  std::sort(names.begin(), names.end());
  const int removeCount = static_cast<int>(names.size()) - keep;
  int removed = 0;
  for (int i = 0; i < removeCount; ++i) {
    const std::string path = std::string(BACKUP_DIRECTORY) + "/" + names[static_cast<size_t>(i)];
    if (Storage.remove(path.c_str())) ++removed;
  }
  return removed;
}

}  // namespace ReadingStatsBackup
