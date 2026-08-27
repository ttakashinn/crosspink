#include "VanNhanSoCache.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "VanNhanSoProfile.h"

namespace vannhanso_cache {

namespace {
constexpr const char* INDEX_PATH = "/.crosspoint/vannhanso-cache/index.txt";
constexpr const char* INDEX_TEMP_PATH = "/.crosspoint/vannhanso-cache/index.tmp";
constexpr size_t MAX_PROFILE_CACHES = 8;

bool safeCacheToken(const std::string& token) {
  if (token.empty() || token.size() > 32) return false;
  for (const char ch : token) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || ch == 'x' || ch == '-')) return false;
  }
  return true;
}

void removeProfileFiles(const std::string& token) {
  if (!safeCacheToken(token)) return;
  for (const char* extension : {"bmp", "bak", "date"}) {
    const std::string path = std::string(vannhanso_profile::CACHE_DIRECTORY) + "/" + token + "." + extension;
    Storage.remove(path.c_str());
  }
}

void touchProfileIndex(const int screenWidth, const int screenHeight) {
  char token[40];
  const int tokenLength = snprintf(token, sizeof(token), "%dx%d-%08lx", screenWidth, screenHeight,
                                   static_cast<unsigned long>(vannhanso_profile::identityHash(screenWidth, screenHeight)));
  if (tokenLength <= 0 || static_cast<size_t>(tokenLength) >= sizeof(token)) return;

  std::vector<std::string> entries;
  char body[512] = {};
  const size_t length = Storage.readFileToBuffer(INDEX_PATH, body, sizeof(body));
  size_t start = 0;
  while (start < length) {
    size_t end = start;
    while (end < length && body[end] != '\n' && body[end] != '\r') ++end;
    const std::string entry(body + start, end - start);
    if (safeCacheToken(entry) && entry != token) entries.push_back(entry);
    while (end < length && (body[end] == '\n' || body[end] == '\r')) ++end;
    start = end;
  }
  entries.emplace_back(token);
  while (entries.size() > MAX_PROFILE_CACHES) {
    removeProfileFiles(entries.front());
    entries.erase(entries.begin());
  }

  Storage.remove(INDEX_TEMP_PATH);
  HalFile file;
  if (!Storage.openFileForWrite("VNS", INDEX_TEMP_PATH, file)) return;
  bool writeOk = true;
  for (const auto& entry : entries) {
    writeOk = writeOk && file.write(reinterpret_cast<const uint8_t*>(entry.data()), entry.size()) == entry.size() &&
              file.write(reinterpret_cast<const uint8_t*>("\n"), 1) == 1;
  }
  file.close();
  if (!writeOk) {
    Storage.remove(INDEX_TEMP_PATH);
    return;
  }
  Storage.remove(INDEX_PATH);
  if (!Storage.rename(INDEX_TEMP_PATH, INDEX_PATH)) Storage.remove(INDEX_TEMP_PATH);
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
  if (vannhanso_profile::buildImagePath(screenWidth, screenHeight, profilePath, sizeof(profilePath)) &&
      validateImage(profilePath, screenWidth, screenHeight)) {
    return profilePath;
  }
  if (validateImage(CACHE_PATH, screenWidth, screenHeight)) return CACHE_PATH;
  if (validateImage(BACKUP_PATH, screenWidth, screenHeight)) return BACKUP_PATH;
  return nullptr;
}

bool readCurrentDate(const int screenWidth, const int screenHeight, uint32_t& dateKey) {
  char path[vannhanso_profile::PATH_MAX_LENGTH];
  if (!vannhanso_profile::buildDatePath(screenWidth, screenHeight, path, sizeof(path))) return false;
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
  if (year < 2024 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) return false;
  dateKey = parsed;
  return true;
}

bool writeCurrentDate(const int screenWidth, const int screenHeight, const uint32_t dateKey) {
  if (!Storage.exists(vannhanso_profile::CACHE_DIRECTORY) && !Storage.mkdir(vannhanso_profile::CACHE_DIRECTORY)) {
    return false;
  }
  char path[vannhanso_profile::PATH_MAX_LENGTH];
  char tempPath[vannhanso_profile::PATH_MAX_LENGTH];
  if (!vannhanso_profile::buildDatePath(screenWidth, screenHeight, path, sizeof(path))) return false;
  const int written = snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(tempPath)) return false;
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
  file.close();
  Storage.remove(path);
  if (!Storage.rename(tempPath, path)) {
    Storage.remove(tempPath);
    return false;
  }
  return true;
}

}  // namespace vannhanso_cache
