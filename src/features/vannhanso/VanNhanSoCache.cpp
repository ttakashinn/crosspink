#include "VanNhanSoCache.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "VanNhanSoProfile.h"

namespace vannhanso_cache {

namespace {
constexpr const char* INDEX_PATH = "/.crosspoint/vannhanso-cache/index.txt";
constexpr const char* INDEX_TEMP_PATH = "/.crosspoint/vannhanso-cache/index.tmp";
constexpr const char* INDEX_BACKUP_PATH = "/.crosspoint/vannhanso-cache/index.bak";
constexpr size_t MAX_PROFILE_CACHES = 8;
constexpr size_t CACHE_TOKEN_CAPACITY = 33;
using CacheToken = std::array<char, CACHE_TOKEN_CAPACITY>;

bool safeCacheToken(const char* token, const size_t length) {
  if (!token || length == 0 || length > 32) return false;
  for (size_t i = 0; i < length; ++i) {
    const char ch = token[i];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || ch == 'x' || ch == '-')) return false;
  }
  return true;
}

void removeProfileFiles(const char* token, const size_t tokenLength) {
  if (!safeCacheToken(token, tokenLength)) return;
  for (const char* extension : {"bmp", "bak", "date", "date.bak"}) {
    char path[vannhanso_profile::PATH_MAX_LENGTH];
    const int written = snprintf(path, sizeof(path), "%s/%.*s.%s", vannhanso_profile::CACHE_DIRECTORY,
                                 static_cast<int>(tokenLength), token, extension);
    if (written > 0 && static_cast<size_t>(written) < sizeof(path)) Storage.remove(path);
  }
}

bool cacheTokenEquals(const CacheToken& token, const char* value, const size_t valueLength) {
  return valueLength < token.size() && strlen(token.data()) == valueLength &&
         memcmp(token.data(), value, valueLength) == 0;
}

bool containsCacheToken(const std::array<CacheToken, MAX_PROFILE_CACHES>& entries, const size_t entryCount,
                        const char* token, const size_t tokenLength) {
  for (size_t i = 0; i < entryCount; ++i) {
    if (cacheTokenEquals(entries[i], token, tokenLength)) return true;
  }
  return false;
}

void touchProfileIndex(const int screenWidth, const int screenHeight) {
  char token[40];
  const int tokenLength =
      snprintf(token, sizeof(token), "%dx%d-%08lx", screenWidth, screenHeight,
               static_cast<unsigned long>(vannhanso_profile::identityHash(screenWidth, screenHeight)));
  if (tokenLength <= 0 || static_cast<size_t>(tokenLength) >= sizeof(token)) return;

  if (!Storage.exists(INDEX_PATH) && Storage.exists(INDEX_BACKUP_PATH)) {
    Storage.rename(INDEX_BACKUP_PATH, INDEX_PATH);
  }

  // This runs just after an HTTPS download, when the C3 heap is most
  // fragmented. Keep the bounded LRU entirely on the stack.
  std::array<CacheToken, MAX_PROFILE_CACHES> entries{};
  size_t entryCount = 0;
  char body[512] = {};
  const size_t length = Storage.readFileToBuffer(INDEX_PATH, body, sizeof(body));
  size_t start = 0;
  while (start < length) {
    size_t end = start;
    while (end < length && body[end] != '\n' && body[end] != '\r') ++end;
    const size_t entryLength = end - start;
    if (safeCacheToken(body + start, entryLength) &&
        !(entryLength == static_cast<size_t>(tokenLength) && memcmp(body + start, token, entryLength) == 0)) {
      // A duplicate becomes the newest occurrence, preserving LRU semantics.
      size_t duplicate = entryCount;
      for (size_t i = 0; i < entryCount; ++i) {
        if (cacheTokenEquals(entries[i], body + start, entryLength)) {
          duplicate = i;
          break;
        }
      }
      if (duplicate < entryCount) {
        for (size_t i = duplicate; i + 1 < entryCount; ++i) entries[i] = entries[i + 1];
        --entryCount;
      }
      // Reserve the final slot for the profile being installed now.
      if (entryCount == MAX_PROFILE_CACHES - 1) {
        for (size_t i = 0; i + 1 < entryCount; ++i) entries[i] = entries[i + 1];
        --entryCount;
      }
      memcpy(entries[entryCount].data(), body + start, entryLength);
      entries[entryCount][entryLength] = '\0';
      ++entryCount;
    }
    while (end < length && (body[end] == '\n' || body[end] == '\r')) ++end;
    start = end;
  }
  memcpy(entries[entryCount].data(), token, static_cast<size_t>(tokenLength) + 1);
  ++entryCount;

  Storage.remove(INDEX_TEMP_PATH);
  HalFile file;
  if (!Storage.openFileForWrite("VNS", INDEX_TEMP_PATH, file)) return;
  bool writeOk = true;
  for (size_t i = 0; i < entryCount; ++i) {
    const size_t entryLength = strlen(entries[i].data());
    writeOk = writeOk && file.write(reinterpret_cast<const uint8_t*>(entries[i].data()), entryLength) == entryLength &&
              file.write(reinterpret_cast<const uint8_t*>("\n"), 1) == 1;
  }
  if (!file.close()) writeOk = false;
  if (!writeOk) {
    Storage.remove(INDEX_TEMP_PATH);
    return;
  }
  const bool hadIndex = Storage.exists(INDEX_PATH);
  if (hadIndex) {
    Storage.remove(INDEX_BACKUP_PATH);
    if (!Storage.rename(INDEX_PATH, INDEX_BACKUP_PATH)) {
      Storage.remove(INDEX_TEMP_PATH);
      return;
    }
  }
  if (!Storage.rename(INDEX_TEMP_PATH, INDEX_PATH)) {
    Storage.remove(INDEX_TEMP_PATH);
    if (hadIndex && !Storage.rename(INDEX_BACKUP_PATH, INDEX_PATH)) {
      LOG_ERR("VNS", "Could not restore profile cache index");
    }
    return;
  }
  Storage.remove(INDEX_BACKUP_PATH);
  // Delete old assets only after the durable index no longer references them.
  // Re-scan the old body to avoid retaining a second eviction collection.
  start = 0;
  while (start < length) {
    size_t end = start;
    while (end < length && body[end] != '\n' && body[end] != '\r') ++end;
    const size_t oldLength = end - start;
    if (safeCacheToken(body + start, oldLength) && !containsCacheToken(entries, entryCount, body + start, oldLength)) {
      removeProfileFiles(body + start, oldLength);
    }
    while (end < length && (body[end] == '\n' || body[end] == '\r')) ++end;
    start = end;
  }
}

bool isLeapYear(const uint32_t year) { return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U; }

uint32_t daysInMonth(const uint32_t year, const uint32_t month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeapYear(year) ? 29U : DAYS[month - 1];
}

void recoverCacheState(const char* cachePath, const char* backupPath, const int screenWidth, const int screenHeight) {
  const bool hasCache = Storage.exists(cachePath);
  const bool hasBackup = Storage.exists(backupPath);

  if (!hasCache && hasBackup) {
    if (Storage.rename(backupPath, cachePath)) {
      LOG_INF("VNS", "Recovered sleep screen after an interrupted install");
    } else {
      LOG_ERR("VNS", "Could not recover sleep-screen backup");
    }
  } else if (hasCache && hasBackup) {
    if (validateImage(cachePath, screenWidth, screenHeight)) {
      Storage.remove(backupPath);
    } else if (validateImage(backupPath, screenWidth, screenHeight)) {
      Storage.remove(cachePath);
      if (!Storage.rename(backupPath, cachePath)) {
        LOG_ERR("VNS", "Could not restore valid sleep-screen backup");
      }
    }
  }
}

bool findIndexedFallbackIn(const char* indexPath, const int screenWidth, const int screenHeight, char* output,
                           const size_t outputSize) {
  char body[512] = {};
  const size_t length = Storage.readFileToBuffer(indexPath, body, sizeof(body));
  if (length == 0) return false;

  std::array<std::array<char, 33>, MAX_PROFILE_CACHES> entries{};
  size_t entryCount = 0;
  size_t start = 0;
  while (start < length && entryCount < entries.size()) {
    size_t end = start;
    while (end < length && body[end] != '\n' && body[end] != '\r') ++end;
    const size_t tokenLength = end - start;
    if (safeCacheToken(body + start, tokenLength)) {
      memcpy(entries[entryCount].data(), body + start, tokenLength);
      entries[entryCount][tokenLength] = '\0';
      ++entryCount;
    }
    while (end < length && (body[end] == '\n' || body[end] == '\r')) ++end;
    start = end;
  }

  // The index is oldest -> newest. A profile change should keep showing the
  // most recently installed image of the same panel size while the new profile
  // is pending, rather than falling back to the CrossPoint "Sleeping" logo.
  while (entryCount > 0) {
    --entryCount;
    const int written =
        snprintf(output, outputSize, "%s/%s.bmp", vannhanso_profile::CACHE_DIRECTORY, entries[entryCount].data());
    if (written > 0 && static_cast<size_t>(written) < outputSize && validateImage(output, screenWidth, screenHeight)) {
      return true;
    }
  }
  return false;
}

bool findIndexedFallback(const int screenWidth, const int screenHeight, char* output, const size_t outputSize) {
  // Prefer the canonical LRU index, but tolerate an interrupted index publish
  // by checking its verified predecessor as well.
  return findIndexedFallbackIn(INDEX_PATH, screenWidth, screenHeight, output, outputSize) ||
         findIndexedFallbackIn(INDEX_BACKUP_PATH, screenWidth, screenHeight, output, outputSize);
}
}  // namespace

bool validateImage(const char* path, const int screenWidth, const int screenHeight) {
  HalFile file;
  if (!Storage.openFileForRead("VNS", path, file)) return false;

  Bitmap bitmap(file, true);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("VNS", "Invalid BMP %s: %s", path, Bitmap::errorToString(parseResult));
    return false;
  }

  if ((bitmap.getBpp() != 1 && bitmap.getBpp() != 2) || bitmap.getWidth() != screenWidth ||
      bitmap.getHeight() != screenHeight) {
    LOG_ERR("VNS", "Wrong BMP format or dimensions for %s: %dx%d %ubpp", path, bitmap.getWidth(), bitmap.getHeight(),
            bitmap.getBpp());
    return false;
  }

  const size_t expectedSize =
      file.position() + static_cast<size_t>(bitmap.getRowBytes()) * static_cast<size_t>(bitmap.getHeight());
  if (file.fileSize() != expectedSize) {
    LOG_ERR("VNS", "Incomplete BMP %s: expected=%u actual=%u", path, static_cast<unsigned>(expectedSize),
            static_cast<unsigned>(file.fileSize()));
    return false;
  }
  return true;
}

bool hasCurrentProfileImage(const int screenWidth, const int screenHeight) {
  char path[vannhanso_profile::PATH_MAX_LENGTH];
  return vannhanso_profile::buildImagePath(screenWidth, screenHeight, path, sizeof(path)) &&
         validateImage(path, screenWidth, screenHeight);
}

void recoverInterruptedInstall(const int screenWidth, const int screenHeight) {
  char cachePath[vannhanso_profile::PATH_MAX_LENGTH];
  char backupPath[vannhanso_profile::PATH_MAX_LENGTH];
  if (vannhanso_profile::buildImagePath(screenWidth, screenHeight, cachePath, sizeof(cachePath)) &&
      vannhanso_profile::buildBackupPath(screenWidth, screenHeight, backupPath, sizeof(backupPath))) {
    recoverCacheState(cachePath, backupPath, screenWidth, screenHeight);
  }
  recoverCacheState(CACHE_PATH, BACKUP_PATH, screenWidth, screenHeight);
  // A temp image is never rendered, so it cannot be useful after a reboot.
  Storage.remove(TEMP_PATH);
}

bool installDownloadedImage(const int screenWidth, const int screenHeight) {
  if (!validateImage(TEMP_PATH, screenWidth, screenHeight)) return false;

  if (!Storage.exists(vannhanso_profile::CACHE_DIRECTORY) && !Storage.mkdir(vannhanso_profile::CACHE_DIRECTORY)) {
    LOG_ERR("VNS", "Could not create profile cache directory");
    return false;
  }

  char cachePath[vannhanso_profile::PATH_MAX_LENGTH];
  char backupPath[vannhanso_profile::PATH_MAX_LENGTH];
  if (!vannhanso_profile::buildImagePath(screenWidth, screenHeight, cachePath, sizeof(cachePath)) ||
      !vannhanso_profile::buildBackupPath(screenWidth, screenHeight, backupPath, sizeof(backupPath))) {
    return false;
  }

  // Normalize power-loss leftovers before replacing the current cache.
  recoverCacheState(cachePath, backupPath, screenWidth, screenHeight);
  // If both files remain, neither passed validation. The backup is not a
  // rollback candidate and must not block the atomic rename sequence.
  if (Storage.exists(cachePath) && Storage.exists(backupPath)) Storage.remove(backupPath);

  const bool hadCache = Storage.exists(cachePath);
  if (hadCache && !Storage.rename(cachePath, backupPath)) {
    LOG_ERR("VNS", "Could not back up the current sleep screen");
    return false;
  }

  if (!Storage.rename(TEMP_PATH, cachePath)) {
    LOG_ERR("VNS", "Could not install the downloaded sleep screen");
    if (hadCache) Storage.rename(backupPath, cachePath);
    return false;
  }

  Storage.remove(backupPath);
  touchProfileIndex(screenWidth, screenHeight);
  return true;
}

const char* findRenderableImage(const int screenWidth, const int screenHeight) {
  static char profilePath[vannhanso_profile::PATH_MAX_LENGTH];
  static char fallbackPath[vannhanso_profile::PATH_MAX_LENGTH];
  if (vannhanso_profile::buildImagePath(screenWidth, screenHeight, profilePath, sizeof(profilePath)) &&
      validateImage(profilePath, screenWidth, screenHeight)) {
    return profilePath;
  }
  if (findIndexedFallback(screenWidth, screenHeight, fallbackPath, sizeof(fallbackPath))) return fallbackPath;
  if (validateImage(CACHE_PATH, screenWidth, screenHeight)) return CACHE_PATH;
  if (validateImage(BACKUP_PATH, screenWidth, screenHeight)) return BACKUP_PATH;
  return nullptr;
}

bool readCurrentDate(const int screenWidth, const int screenHeight, uint32_t& dateKey) {
  char path[vannhanso_profile::PATH_MAX_LENGTH];
  if (!vannhanso_profile::buildDatePath(screenWidth, screenHeight, path, sizeof(path))) return false;
  char backupPath[vannhanso_profile::PATH_MAX_LENGTH];
  const int backupWritten = snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
  if (backupWritten <= 0 || static_cast<size_t>(backupWritten) >= sizeof(backupPath)) return false;
  if (!Storage.exists(path) && Storage.exists(backupPath)) Storage.rename(backupPath, path);
  HalFile file;
  if (!Storage.openFileForRead("VNS", path, file)) return false;
  char value[9] = {};
  if (file.read(value, 8) != 8) return false;
  uint32_t parsed = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    parsed = parsed * 10U + static_cast<uint32_t>(value[i] - '0');
  }
  const uint32_t year = parsed / 10000U;
  const uint32_t month = (parsed / 100U) % 100U;
  const uint32_t day = parsed % 100U;
  if (year < 2024 || year > 2100 || day < 1 || day > daysInMonth(year, month)) return false;
  dateKey = parsed;
  return true;
}

bool writeCurrentDate(const int screenWidth, const int screenHeight, const uint32_t dateKey) {
  if (!Storage.exists(vannhanso_profile::CACHE_DIRECTORY) && !Storage.mkdir(vannhanso_profile::CACHE_DIRECTORY)) {
    return false;
  }
  char path[vannhanso_profile::PATH_MAX_LENGTH];
  char tempPath[vannhanso_profile::PATH_MAX_LENGTH];
  char backupPath[vannhanso_profile::PATH_MAX_LENGTH];
  if (!vannhanso_profile::buildDatePath(screenWidth, screenHeight, path, sizeof(path))) return false;
  const int written = snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(tempPath)) return false;
  const int backupWritten = snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
  if (backupWritten <= 0 || static_cast<size_t>(backupWritten) >= sizeof(backupPath)) return false;
  char value[9];
  snprintf(value, sizeof(value), "%08lu", static_cast<unsigned long>(dateKey));
  Storage.remove(tempPath);
  HalFile file;
  if (!Storage.openFileForWrite("VNS", tempPath, file)) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(value), 8) != 8) {
    file.close();
    Storage.remove(tempPath);
    return false;
  }
  if (!file.close()) {
    Storage.remove(tempPath);
    return false;
  }
  const bool hadDate = Storage.exists(path);
  if (hadDate) {
    Storage.remove(backupPath);
    if (!Storage.rename(path, backupPath)) {
      Storage.remove(tempPath);
      return false;
    }
  }
  if (!Storage.rename(tempPath, path)) {
    Storage.remove(tempPath);
    if (hadDate && !Storage.rename(backupPath, path)) {
      LOG_ERR("VNS", "Could not restore current-date marker");
    }
    return false;
  }
  Storage.remove(backupPath);
  return true;
}

}  // namespace vannhanso_cache
