#pragma once

#include <SdCardFontRegistry.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace web_font_catalog_json {

using ChunkSink = bool (*)(void* context, const char* data, size_t size);
using FileSizeLookup = unsigned long (*)(void* context, const char* path);

// Keeps response serialization independent of the catalog size. The old
// ArduinoJson + String path held the complete object tree and a second complete
// serialized copy at the same time, which could exhaust the fragmented C3 heap
// while Wi-Fi AP mode was active.
inline constexpr size_t SCRATCH_BYTES = 96;

class Writer {
 public:
  Writer(ChunkSink sink, void* context) : sink_(sink), context_(context) {}

  bool literal(const std::string_view value) const {
    return value.empty() || sink_(context_, value.data(), value.size());
  }

  bool number(const unsigned long value) const {
    char buffer[24];
    const int length = snprintf(buffer, sizeof(buffer), "%lu", value);
    return length > 0 && static_cast<size_t>(length) < sizeof(buffer) &&
           sink_(context_, buffer, static_cast<size_t>(length));
  }

  bool quoted(const std::string_view value) const {
    if (!literal("\"")) return false;

    char scratch[SCRATCH_BYTES];
    size_t used = 0;
    const auto flush = [&]() {
      if (used == 0) return true;
      const bool ok = sink_(context_, scratch, used);
      used = 0;
      return ok;
    };
    const auto append = [&](const char* data, const size_t size) {
      if (used + size > sizeof(scratch) && !flush()) return false;
      memcpy(scratch + used, data, size);
      used += size;
      return true;
    };

    for (const unsigned char valueByte : value) {
      const char byte = static_cast<char>(valueByte);
      switch (byte) {
        case '\"':
          if (!append("\\\"", 2)) return false;
          break;
        case '\\':
          if (!append("\\\\", 2)) return false;
          break;
        case '\b':
          if (!append("\\b", 2)) return false;
          break;
        case '\f':
          if (!append("\\f", 2)) return false;
          break;
        case '\n':
          if (!append("\\n", 2)) return false;
          break;
        case '\r':
          if (!append("\\r", 2)) return false;
          break;
        case '\t':
          if (!append("\\t", 2)) return false;
          break;
        default:
          if (valueByte < 0x20) {
            char escaped[7];
            const int length = snprintf(escaped, sizeof(escaped), "\\u%04x", valueByte);
            if (length != 6 || !append(escaped, 6)) return false;
          } else if (!append(&byte, 1)) {
            return false;
          }
          break;
      }
    }

    return flush() && literal("\"");
  }

 private:
  ChunkSink sink_;
  void* context_;
};

inline bool stream(const std::vector<SdCardFontFamilyInfo>& families, const unsigned long maxFamilies,
                   const ChunkSink sink, void* sinkContext, const FileSizeLookup sizeLookup, void* sizeLookupContext) {
  if (!sink) return false;
  const Writer out(sink, sinkContext);
  if (!out.literal("{\"families\":[")) return false;

  bool firstFamily = true;
  for (const auto& family : families) {
    if (!firstFamily && !out.literal(",")) return false;
    firstFamily = false;
    if (!out.literal("{\"name\":") || !out.quoted(family.name) || !out.literal(",\"sizes\":[")) return false;

    // Point sizes are uint8_t. A 256-bit stack bitmap keeps them unique and
    // sorted without allocating the temporary vector returned by
    // SdCardFontFamilyInfo::availableSizes().
    std::array<uint8_t, 32> sizeBits{};
    for (const auto& file : family.files) {
      sizeBits[file.pointSize >> 3] |= static_cast<uint8_t>(1U << (file.pointSize & 7));
    }

    bool firstSize = true;
    for (unsigned int pointSize = 1; pointSize <= 255; ++pointSize) {
      if ((sizeBits[pointSize >> 3] & static_cast<uint8_t>(1U << (pointSize & 7))) == 0) continue;
      if (!firstSize && !out.literal(",")) return false;
      firstSize = false;
      if (!out.number(pointSize)) return false;
    }

    if (!out.literal("],\"files\":[")) return false;
    bool firstFile = true;
    for (const auto& file : family.files) {
      if (!firstFile && !out.literal(",")) return false;
      firstFile = false;
      const size_t slash = file.path.find_last_of('/');
      const std::string_view filename =
          slash == std::string::npos ? std::string_view(file.path) : std::string_view(file.path).substr(slash + 1);
      const unsigned long fileSize = sizeLookup ? sizeLookup(sizeLookupContext, file.path.c_str()) : 0;
      if (!out.literal("{\"name\":") || !out.quoted(filename) || !out.literal(",\"size\":") || !out.number(fileSize) ||
          !out.literal("}")) {
        return false;
      }
    }
    if (!out.literal("]}")) return false;
  }

  return out.literal("],\"maxFamilies\":") && out.number(maxFamilies) && out.literal("}");
}

}  // namespace web_font_catalog_json
