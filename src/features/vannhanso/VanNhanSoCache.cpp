#include "VanNhanSoCache.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

namespace vannhanso_cache {

namespace {
void recoverCacheState(const int screenWidth, const int screenHeight) {
  const bool hasCache = Storage.exists(CACHE_PATH);
  const bool hasBackup = Storage.exists(BACKUP_PATH);

  if (!hasCache && hasBackup) {
    if (Storage.rename(BACKUP_PATH, CACHE_PATH)) {
      LOG_INF("VNS", "Recovered sleep screen after an interrupted install");
    } else {
      LOG_ERR("VNS", "Could not recover sleep-screen backup");
    }
  } else if (hasCache && hasBackup) {
    if (validateImage(CACHE_PATH, screenWidth, screenHeight)) {
      Storage.remove(BACKUP_PATH);
    } else if (validateImage(BACKUP_PATH, screenWidth, screenHeight)) {
      Storage.remove(CACHE_PATH);
      if (!Storage.rename(BACKUP_PATH, CACHE_PATH)) {
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
  recoverCacheState(screenWidth, screenHeight);
  // A temp image is never rendered, so it cannot be useful after a reboot.
  Storage.remove(TEMP_PATH);
}

bool installDownloadedImage(const int screenWidth, const int screenHeight) {
  if (!validateImage(TEMP_PATH, screenWidth, screenHeight)) return false;

  // Normalize power-loss leftovers before replacing the current cache.
  recoverCacheState(screenWidth, screenHeight);
  // If both files remain, neither passed validation. The backup is not a
  // rollback candidate and must not block the atomic rename sequence.
  if (Storage.exists(CACHE_PATH) && Storage.exists(BACKUP_PATH)) Storage.remove(BACKUP_PATH);

  const bool hadCache = Storage.exists(CACHE_PATH);
  if (hadCache && !Storage.rename(CACHE_PATH, BACKUP_PATH)) {
    LOG_ERR("VNS", "Could not back up the current sleep screen");
    return false;
  }

  if (!Storage.rename(TEMP_PATH, CACHE_PATH)) {
    LOG_ERR("VNS", "Could not install the downloaded sleep screen");
    if (hadCache) Storage.rename(BACKUP_PATH, CACHE_PATH);
    return false;
  }

  Storage.remove(BACKUP_PATH);
  return true;
}

const char* findRenderableImage(const int screenWidth, const int screenHeight) {
  if (validateImage(CACHE_PATH, screenWidth, screenHeight)) return CACHE_PATH;
  if (validateImage(BACKUP_PATH, screenWidth, screenHeight)) return BACKUP_PATH;
  return nullptr;
}

}  // namespace vannhanso_cache
