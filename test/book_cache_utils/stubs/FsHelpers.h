#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace FsHelpers {
inline bool hasExtension(std::string path, const char* extension) {
  std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
  const std::string suffix = extension;
  return path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}
inline bool hasEpubExtension(const std::string& path) { return hasExtension(path, ".epub"); }
inline bool hasTxtExtension(const std::string& path) { return hasExtension(path, ".txt"); }
inline bool hasXtcExtension(const std::string& path) { return hasExtension(path, ".xtc"); }
}  // namespace FsHelpers
