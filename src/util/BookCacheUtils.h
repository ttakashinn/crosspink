#pragma once

#include <string>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT). Does nothing for other file types.
void clearBookCache(const std::string& path);

// Rename/move a file and its path-keyed reading cache as one recoverable
// operation. The source file is restored when cache migration fails. Returns
// false without changing either side when the destination cache already exists.
bool moveBookWithCache(const std::string& sourcePath, const std::string& destinationPath);

// Resolve the cache directory used by a supported book type.
bool getBookCachePath(const std::string& path, std::string& cachePath);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);
