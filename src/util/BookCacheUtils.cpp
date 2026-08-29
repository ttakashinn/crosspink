#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}

bool getBookCachePath(const std::string& path, std::string& cachePath) {
  if (FsHelpers::hasEpubExtension(path)) {
    cachePath = Epub(path, "/.crosspoint").getCachePath();
  } else if (FsHelpers::hasXtcExtension(path)) {
    cachePath = Xtc(path, "/.crosspoint").getCachePath();
  } else if (FsHelpers::hasTxtExtension(path)) {
    cachePath = Txt(path, "/.crosspoint").getCachePath();
  } else {
    cachePath.clear();
    return false;
  }
  return true;
}

bool moveBookWithCache(const std::string& sourcePath, const std::string& destinationPath) {
  std::string oldCachePath;
  std::string newCachePath;
  const bool hasBookCache =
      getBookCachePath(sourcePath, oldCachePath) && getBookCachePath(destinationPath, newCachePath);
  const bool cacheMoves = hasBookCache && oldCachePath != newCachePath && Storage.exists(oldCachePath.c_str());

  // A cache at a path whose book does not yet exist is stale or orphaned, but it
  // may still contain recoverable user state. Refuse to overwrite it silently.
  if (hasBookCache && oldCachePath != newCachePath && Storage.exists(newCachePath.c_str())) {
    LOG_ERR("BookCache", "Destination cache already exists: %s", newCachePath.c_str());
    return false;
  }

  if (!Storage.rename(sourcePath.c_str(), destinationPath.c_str())) return false;
  if (!cacheMoves || Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) return true;

  LOG_ERR("BookCache", "Cache move failed, rolling back book: %s -> %s", oldCachePath.c_str(), newCachePath.c_str());
  if (!Storage.rename(destinationPath.c_str(), sourcePath.c_str())) {
    LOG_ERR("BookCache", "Book rollback failed after cache move failure: %s", destinationPath.c_str());
  }
  return false;
}

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasTxtExtension(path)) {
    Txt(path, "/.crosspoint").clearCache();
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}
