#include "ClippingCodec.h"

#include <Utf8.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

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

void hashByte(uint32_t& hash, const uint8_t byte) {
  hash ^= byte;
  hash *= 16777619U;
}

void hashU16(uint32_t& hash, const uint16_t value) {
  hashByte(hash, static_cast<uint8_t>(value));
  hashByte(hash, static_cast<uint8_t>(value >> 8));
}

void hashU32(uint32_t& hash, const uint32_t value) {
  hashU16(hash, static_cast<uint16_t>(value));
  hashU16(hash, static_cast<uint16_t>(value >> 16));
}

bool fieldsMatchFirstSegment(const Record& record, const Segment& segment) {
  return record.pageHint == segment.pageHint && record.pageVisibleOffset == segment.pageVisibleOffset &&
         record.startWordIndex == segment.startWordIndex && record.endWordIndex == segment.endWordIndex;
}

Status validate(const Record& record, const bool requireId) {
  if (requireId && record.id == 0) return Status::CORRUPT;
  if (record.text.empty() || record.startWordIndex > record.endWordIndex) return Status::CORRUPT;
  if (record.text.size() > MAX_TEXT_BYTES) return Status::LIMIT_EXCEEDED;
  if (!utf8IsValid(record.text)) return Status::INVALID_UTF8;
  if (record.segmentCount > MAX_SEGMENTS_PER_CLIPPING) return Status::LIMIT_EXCEEDED;

  const size_t count = segmentCount(record);
  size_t expectedOffset = 0;
  uint16_t previousPage = 0;
  for (size_t i = 0; i < count; ++i) {
    const Segment segment = segmentAt(record, i);
    if (segment.startWordIndex > segment.endWordIndex || segment.textLength == 0 ||
        segment.textOffset != expectedOffset || segment.textOffset > record.text.size() ||
        segment.textLength > record.text.size() - segment.textOffset ||
        !utf8IsValid(std::string_view(record.text).substr(segment.textOffset, segment.textLength))) {
      return Status::CORRUPT;
    }
    if (i == 0) {
      if (!fieldsMatchFirstSegment(record, segment)) return Status::CORRUPT;
    } else if (segment.pageHint <= previousPage || segment.textOffset == 0 ||
               record.text[segment.textOffset - 1] != ' ') {
      return Status::CORRUPT;
    }
    previousPage = segment.pageHint;
    expectedOffset = static_cast<size_t>(segment.textOffset) + segment.textLength;
    if (i + 1 < count) ++expectedOffset;
  }
  return expectedOffset == record.text.size() ? Status::OK : Status::CORRUPT;
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

size_t segmentCount(const Record& record) { return record.segmentCount == 0 ? 1 : record.segmentCount; }

Segment segmentAt(const Record& record, const size_t index) {
  if (record.segmentCount != 0) {
    if (index < record.segmentCount) return record.segments[index];
    return {};
  }
  if (index != 0 || record.text.size() > std::numeric_limits<uint16_t>::max()) return {};
  return {record.pageHint,
          record.pageVisibleOffset,
          record.startWordIndex,
          record.endWordIndex,
          0,
          static_cast<uint16_t>(record.text.size())};
}

bool validRecord(const Record& record, const bool requireId) { return validate(record, requireId) == Status::OK; }

uint32_t makeStableId(const Record& record, const std::vector<Record>& existing) {
  uint32_t id = 2166136261U;
  hashU16(id, record.spineIndex);
  const size_t count = segmentCount(record);
  for (size_t i = 0; i < count; ++i) {
    const Segment segment = segmentAt(record, i);
    hashU16(id, segment.pageHint);
    hashU32(id, segment.pageVisibleOffset);
    hashU16(id, segment.startWordIndex);
    hashU16(id, segment.endWordIndex);
    hashU16(id, segment.textOffset);
    hashU16(id, segment.textLength);
  }
  for (const char byte : record.text) hashByte(id, static_cast<uint8_t>(byte));
  if (id == 0) id = 1;
  const auto inUse = [&existing](const uint32_t candidate) {
    return std::any_of(existing.begin(), existing.end(),
                       [candidate](const Record& current) { return current.id == candidate; });
  };
  while (inUse(id)) {
    ++id;
    if (id == 0) id = 1;
  }
  return id;
}

Status encode(const std::vector<Record>& records, std::vector<uint8_t>& out) {
  out.clear();
  if (records.size() > MAX_CLIPPINGS_PER_BOOK) return Status::LIMIT_EXCEEDED;
  size_t payloadSize = 0;
  for (size_t i = 0; i < records.size(); ++i) {
    const auto& record = records[i];
    const Status status = validate(record, true);
    if (status != Status::OK) return status;
    if (std::any_of(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(i),
                    [&record](const Record& previous) { return previous.id == record.id; })) {
      return Status::CORRUPT;
    }
    const size_t bytes = RECORD_HEADER_SIZE + segmentCount(record) * SEGMENT_HEADER_SIZE + record.text.size();
    if (payloadSize > MAX_FILE_BYTES - HEADER_SIZE - bytes) return Status::LIMIT_EXCEEDED;
    payloadSize += bytes;
  }

  out.assign(HEADER_SIZE + payloadSize, 0);
  std::copy(MAGIC.begin(), MAGIC.end(), out.begin());
  out[4] = VERSION;
  writeU16(out.data() + 6, static_cast<uint16_t>(records.size()));
  writeU32(out.data() + 8, static_cast<uint32_t>(payloadSize));

  size_t cursor = HEADER_SIZE;
  for (const auto& record : records) {
    const size_t count = segmentCount(record);
    writeU32(out.data() + cursor, record.id);
    writeU16(out.data() + cursor + 4, record.spineIndex);
    out[cursor + 6] = static_cast<uint8_t>(count);
    writeU16(out.data() + cursor + 8, static_cast<uint16_t>(record.text.size()));
    cursor += RECORD_HEADER_SIZE;
    for (size_t i = 0; i < count; ++i) {
      const Segment segment = segmentAt(record, i);
      writeU16(out.data() + cursor, segment.pageHint);
      writeU16(out.data() + cursor + 2, segment.startWordIndex);
      writeU16(out.data() + cursor + 4, segment.endWordIndex);
      writeU16(out.data() + cursor + 6, segment.textOffset);
      writeU16(out.data() + cursor + 8, segment.textLength);
      writeU32(out.data() + cursor + 12, segment.pageVisibleOffset);
      cursor += SEGMENT_HEADER_SIZE;
    }
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
  const uint8_t version = bytes[4];
  if (version > VERSION) return Status::NEWER_VERSION;
  if (version != 1 && version != 2 && version != VERSION) return Status::UNSUPPORTED_VERSION;
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
    Record record;
    uint16_t textLength = 0;
    if (version == VERSION) {
      if (length - cursor < RECORD_HEADER_SIZE) return Status::TRUNCATED;
      record.id = readU32(bytes + cursor);
      record.spineIndex = readU16(bytes + cursor + 4);
      const uint8_t segmentTotal = bytes[cursor + 6];
      textLength = readU16(bytes + cursor + 8);
      if (record.id == 0 || segmentTotal == 0 || segmentTotal > MAX_SEGMENTS_PER_CLIPPING || textLength == 0 ||
          textLength > MAX_TEXT_BYTES || bytes[cursor + 7] != 0 ||
          std::any_of(bytes + cursor + 10, bytes + cursor + RECORD_HEADER_SIZE,
                      [](const uint8_t value) { return value != 0; })) {
        return segmentTotal > MAX_SEGMENTS_PER_CLIPPING || textLength > MAX_TEXT_BYTES ? Status::LIMIT_EXCEEDED
                                                                                       : Status::CORRUPT;
      }
      cursor += RECORD_HEADER_SIZE;
      if (static_cast<size_t>(segmentTotal) * SEGMENT_HEADER_SIZE > length - cursor) return Status::TRUNCATED;
      record.segmentCount = segmentTotal;
      for (uint8_t segmentIndex = 0; segmentIndex < segmentTotal; ++segmentIndex) {
        Segment& segment = record.segments[segmentIndex];
        segment.pageHint = readU16(bytes + cursor);
        segment.startWordIndex = readU16(bytes + cursor + 2);
        segment.endWordIndex = readU16(bytes + cursor + 4);
        segment.textOffset = readU16(bytes + cursor + 6);
        segment.textLength = readU16(bytes + cursor + 8);
        segment.pageVisibleOffset = readU32(bytes + cursor + 12);
        if (bytes[cursor + 10] != 0 || bytes[cursor + 11] != 0) return Status::CORRUPT;
        cursor += SEGMENT_HEADER_SIZE;
      }
      const Segment& first = record.segments[0];
      record.pageHint = first.pageHint;
      record.pageVisibleOffset = first.pageVisibleOffset;
      record.startWordIndex = first.startWordIndex;
      record.endWordIndex = first.endWordIndex;
    } else {
      const bool legacy = version == 1;
      const size_t recordHeaderSize = legacy ? LEGACY_RECORD_HEADER_SIZE : V2_RECORD_HEADER_SIZE;
      if (length - cursor < recordHeaderSize) return Status::TRUNCATED;
      if (legacy) {
        record.spineIndex = readU16(bytes + cursor);
        record.pageHint = readU16(bytes + cursor + 2);
        record.pageVisibleOffset = readU32(bytes + cursor + 4);
        record.startWordIndex = readU16(bytes + cursor + 8);
        record.endWordIndex = readU16(bytes + cursor + 10);
        textLength = readU16(bytes + cursor + 12);
      } else {
        record.id = readU32(bytes + cursor);
        record.spineIndex = readU16(bytes + cursor + 4);
        record.pageHint = readU16(bytes + cursor + 6);
        record.pageVisibleOffset = readU32(bytes + cursor + 8);
        record.startWordIndex = readU16(bytes + cursor + 12);
        record.endWordIndex = readU16(bytes + cursor + 14);
        textLength = readU16(bytes + cursor + 16);
      }
      const size_t reservedOffset = legacy ? 14 : 18;
      if (bytes[cursor + reservedOffset] != 0 || bytes[cursor + reservedOffset + 1] != 0 || textLength == 0 ||
          textLength > MAX_TEXT_BYTES || (!legacy && record.id == 0)) {
        return textLength > MAX_TEXT_BYTES ? Status::LIMIT_EXCEEDED : Status::CORRUPT;
      }
      cursor += recordHeaderSize;
    }

    if (textLength > length - cursor) return Status::TRUNCATED;
    record.text.assign(reinterpret_cast<const char*>(bytes + cursor), textLength);
    cursor += textLength;
    const Status recordStatus = validate(record, version != 1);
    if (recordStatus != Status::OK) return recordStatus;
    if (version == 1) {
      record.id = makeStableId(record, decoded);
    } else if (std::any_of(decoded.begin(), decoded.end(),
                           [&record](const Record& previous) { return previous.id == record.id; })) {
      return Status::CORRUPT;
    }
    if (record.segmentCount == 1) {
      record.segmentCount = 0;
      record.segments = {};
    }
    decoded.push_back(std::move(record));
  }
  if (cursor != length) return Status::CORRUPT;
  out = std::move(decoded);
  return Status::OK;
}

}  // namespace ClippingCodec
