#include "ClippingCodec.h"

#include <Utf8.h>

#include <algorithm>
#include <array>
#include <limits>

namespace ClippingCodec {
namespace {

constexpr std::array<uint8_t, 4> MAGIC = {'V', 'N', 'S', 'C'};

uint16_t readU16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}
uint32_t readU32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}
void writeU16(uint8_t* bytes, const uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}
void writeU32(uint8_t* bytes, const uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

bool valid(const Record& record) {
  return record.startWordIndex <= record.endWordIndex && !record.text.empty() && record.text.size() <= MAX_TEXT_BYTES &&
         utf8IsValid(record.text);
}

}  // namespace

uint32_t crc32(const uint8_t* bytes, const size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

Status encode(const std::vector<Record>& records, std::vector<uint8_t>& out) {
  out.clear();
  if (records.size() > MAX_CLIPPINGS_PER_BOOK) return Status::LIMIT_EXCEEDED;
  size_t payloadSize = 0;
  for (const auto& record : records) {
    if (record.text.size() > MAX_TEXT_BYTES) return Status::LIMIT_EXCEEDED;
    if (record.startWordIndex > record.endWordIndex || record.text.empty()) return Status::CORRUPT;
    if (!utf8IsValid(record.text)) return Status::INVALID_UTF8;
    if (payloadSize > MAX_FILE_BYTES - RECORD_HEADER_SIZE - record.text.size()) return Status::LIMIT_EXCEEDED;
    payloadSize += RECORD_HEADER_SIZE + record.text.size();
  }
  out.assign(HEADER_SIZE + payloadSize, 0);
  std::copy(MAGIC.begin(), MAGIC.end(), out.begin());
  out[4] = VERSION;
  writeU16(out.data() + 6, static_cast<uint16_t>(records.size()));
  writeU32(out.data() + 8, static_cast<uint32_t>(payloadSize));

  size_t cursor = HEADER_SIZE;
  for (const auto& record : records) {
    writeU16(out.data() + cursor, record.spineIndex);
    writeU16(out.data() + cursor + 2, record.pageHint);
    writeU32(out.data() + cursor + 4, record.pageVisibleOffset);
    writeU16(out.data() + cursor + 8, record.startWordIndex);
    writeU16(out.data() + cursor + 10, record.endWordIndex);
    writeU16(out.data() + cursor + 12, static_cast<uint16_t>(record.text.size()));
    cursor += RECORD_HEADER_SIZE;
    std::copy(record.text.begin(), record.text.end(), out.begin() + static_cast<std::ptrdiff_t>(cursor));
    cursor += record.text.size();
  }
  writeU32(out.data() + 12, crc32(out.data() + HEADER_SIZE, payloadSize));
  return Status::OK;
}

Status decode(const uint8_t* bytes, const size_t length, std::vector<Record>& out) {
  out.clear();
  if (!bytes || length < HEADER_SIZE) return Status::TRUNCATED;
  if (!std::equal(MAGIC.begin(), MAGIC.end(), bytes)) return Status::BAD_MAGIC;
  if (bytes[4] > VERSION) return Status::NEWER_VERSION;
  if (bytes[4] != VERSION) return Status::UNSUPPORTED_VERSION;
  if (bytes[5] != 0) return Status::CORRUPT;
  const uint16_t count = readU16(bytes + 6);
  const uint32_t payloadSize = readU32(bytes + 8);
  if (count > MAX_CLIPPINGS_PER_BOOK || payloadSize > MAX_FILE_BYTES - HEADER_SIZE) return Status::LIMIT_EXCEEDED;
  if (payloadSize > length - HEADER_SIZE) return Status::TRUNCATED;
  if (length != HEADER_SIZE + payloadSize) return Status::CORRUPT;
  if (readU32(bytes + 12) != crc32(bytes + HEADER_SIZE, payloadSize)) return Status::BAD_CRC;

  std::vector<Record> decoded;
  decoded.reserve(count);
  size_t cursor = HEADER_SIZE;
  for (uint16_t i = 0; i < count; ++i) {
    if (cursor > length || length - cursor < RECORD_HEADER_SIZE) return Status::TRUNCATED;
    Record record;
    record.spineIndex = readU16(bytes + cursor);
    record.pageHint = readU16(bytes + cursor + 2);
    record.pageVisibleOffset = readU32(bytes + cursor + 4);
    record.startWordIndex = readU16(bytes + cursor + 8);
    record.endWordIndex = readU16(bytes + cursor + 10);
    const uint16_t textLength = readU16(bytes + cursor + 12);
    if (bytes[cursor + 14] != 0 || bytes[cursor + 15] != 0 || textLength == 0 || textLength > MAX_TEXT_BYTES) {
      return textLength > MAX_TEXT_BYTES ? Status::LIMIT_EXCEEDED : Status::CORRUPT;
    }
    cursor += RECORD_HEADER_SIZE;
    if (textLength > length - cursor) return Status::TRUNCATED;
    record.text.assign(reinterpret_cast<const char*>(bytes + cursor), textLength);
    cursor += textLength;
    if (!utf8IsValid(record.text)) return Status::INVALID_UTF8;
    if (!valid(record)) return Status::CORRUPT;
    decoded.push_back(std::move(record));
  }
  if (cursor != length) return Status::CORRUPT;
  out = std::move(decoded);
  return Status::OK;
}

}  // namespace ClippingCodec
