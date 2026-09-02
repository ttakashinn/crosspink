#pragma once

#include <cstddef>
#include <string_view>

namespace WebFileResponsePolicy {

inline char asciiLower(const char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

inline bool hasExtension(const std::string_view path, const std::string_view extension) {
  if (path.size() < extension.size()) return false;

  const size_t offset = path.size() - extension.size();
  for (size_t index = 0; index < extension.size(); ++index) {
    if (asciiLower(path[offset + index]) != asciiLower(extension[index])) return false;
  }
  return true;
}

inline bool isPreviewableImage(const std::string_view path) {
  return hasExtension(path, ".jpg") || hasExtension(path, ".jpeg") || hasExtension(path, ".png") ||
         hasExtension(path, ".bmp") || hasExtension(path, ".gif") || hasExtension(path, ".webp");
}

inline const char* contentTypeForPath(const std::string_view path) {
  if (hasExtension(path, ".jpg") || hasExtension(path, ".jpeg")) return "image/jpeg";
  if (hasExtension(path, ".png")) return "image/png";
  if (hasExtension(path, ".bmp")) return "image/bmp";
  if (hasExtension(path, ".gif")) return "image/gif";
  if (hasExtension(path, ".webp")) return "image/webp";
  if (hasExtension(path, ".epub")) return "application/epub+zip";
  return "application/octet-stream";
}

inline bool shouldServeInline(const std::string_view path, const bool previewRequested) {
  return previewRequested && isPreviewableImage(path);
}

}  // namespace WebFileResponsePolicy
