#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ClippingCodec {

constexpr size_t MAX_CLIPPINGS_PER_BOOK = 64;
constexpr size_t MAX_TEXT_BYTES = 512;
constexpr size_t MAX_SEGMENTS_PER_CLIPPING = 4;
constexpr size_t HEADER_SIZE = 16;
constexpr size_t LEGACY_RECORD_HEADER_SIZE = 16;
constexpr size_t V2_RECORD_HEADER_SIZE = 20;
constexpr size_t RECORD_HEADER_SIZE = 16;
constexpr size_t SEGMENT_HEADER_SIZE = 16;
constexpr size_t MAX_FILE_BYTES =
    HEADER_SIZE +
    MAX_CLIPPINGS_PER_BOOK * (RECORD_HEADER_SIZE + MAX_SEGMENTS_PER_CLIPPING * SEGMENT_HEADER_SIZE + MAX_TEXT_BYTES);
constexpr uint8_t VERSION = 3;

struct Segment {
  uint16_t pageHint = 0;
  uint32_t pageVisibleOffset = 0;
  uint16_t startWordIndex = 0;
  uint16_t endWordIndex = 0;
  uint16_t textOffset = 0;
  uint16_t textLength = 0;

  bool operator==(const Segment&) const = default;
};

struct Record {
  uint16_t spineIndex = 0;
  uint16_t pageHint = 0;
  uint32_t pageVisibleOffset = 0;
  uint16_t startWordIndex = 0;
  uint16_t endWordIndex = 0;
  std::string text;
  uint32_t id = 0;
  // Zero is the compact in-memory representation of a legacy/single-page
  // clipping. segmentAt() synthesizes that first segment from the fields above.
  uint8_t segmentCount = 0;
  std::array<Segment, MAX_SEGMENTS_PER_CLIPPING> segments{};

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
size_t segmentCount(const Record& record);
Segment segmentAt(const Record& record, size_t index);
bool validRecord(const Record& record, bool requireId = true);
uint32_t makeStableId(const Record& record, const std::vector<Record>& existing);
Status encode(const std::vector<Record>& records, std::vector<uint8_t>& out);
Status decode(const uint8_t* bytes, size_t length, std::vector<Record>& out);

}  // namespace ClippingCodec
