#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ClippingCodec {

constexpr size_t MAX_CLIPPINGS_PER_BOOK = 64;
constexpr size_t MAX_TEXT_BYTES = 512;
constexpr size_t HEADER_SIZE = 16;
constexpr size_t LEGACY_RECORD_HEADER_SIZE = 16;
constexpr size_t RECORD_HEADER_SIZE = 20;
constexpr size_t MAX_FILE_BYTES = HEADER_SIZE + MAX_CLIPPINGS_PER_BOOK * (RECORD_HEADER_SIZE + MAX_TEXT_BYTES);
constexpr uint8_t VERSION = 2;

struct Record {
  uint16_t spineIndex = 0;
  uint16_t pageHint = 0;
  uint32_t pageVisibleOffset = 0;
  uint16_t startWordIndex = 0;
  uint16_t endWordIndex = 0;
  std::string text;
  uint32_t id = 0;

  bool operator==(const Record&) const = default;
};

enum class Status : uint8_t {
  OK,
  TRUNCATED,
  BAD_MAGIC,
  NEWER_VERSION,
  UNSUPPORTED_VERSION,
  CORRUPT,
  BAD_CRC,
  LIMIT_EXCEEDED,
  INVALID_UTF8,
};

uint32_t crc32(const uint8_t* bytes, size_t length);
uint32_t makeStableId(const Record& record, const std::vector<Record>& existing);
Status encode(const std::vector<Record>& records, std::vector<uint8_t>& out);
Status decode(const uint8_t* bytes, size_t length, std::vector<Record>& out);

}  // namespace ClippingCodec
