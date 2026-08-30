#include <gtest/gtest.h>

#include <algorithm>

#include "ClippingCodec.h"

namespace {

ClippingCodec::Record multiPageRecord(const uint32_t id = 2001) {
  ClippingCodec::Record record{3, 12, 420, 4, 9, "first page next page", id};
  record.segmentCount = 2;
  record.segments[0] = {12, 420, 4, 9, 0, 10};
  record.segments[1] = {13, 510, 0, 1, 11, 9};
  return record;
}

}  // namespace

TEST(ClippingCodec, RoundTripsVietnameseTextAndAnchors) {
  const std::vector<ClippingCodec::Record> records = {
      {3, 12, 420, 4, 9, "Tiếng Việt giàu thanh điệu và dấu phụ.", 1001},
      {5, 2, 99, 0, 2, "Văn Nhân Số", 1002},
  };
  std::vector<uint8_t> encoded;
  ASSERT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::OK);
  std::vector<ClippingCodec::Record> decoded;
  EXPECT_EQ(ClippingCodec::decode(encoded.data(), encoded.size(), decoded), ClippingCodec::Status::OK);
  EXPECT_EQ(decoded, records);
}

TEST(ClippingCodec, RejectsTornAndCorruptPayloads) {
  const std::vector<ClippingCodec::Record> records = {{0, 0, 0, 0, 0, "một", 1}};
  std::vector<uint8_t> encoded;
  ASSERT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::OK);
  std::vector<ClippingCodec::Record> decoded;
  EXPECT_EQ(ClippingCodec::decode(encoded.data(), encoded.size() - 1, decoded), ClippingCodec::Status::TRUNCATED);
  encoded.back() ^= 0x20;
  EXPECT_EQ(ClippingCodec::decode(encoded.data(), encoded.size(), decoded), ClippingCodec::Status::BAD_CRC);
}

TEST(ClippingCodec, RefusesFutureVersionWithoutOverwritingMeaning) {
  const std::vector<ClippingCodec::Record> records = {{0, 0, 0, 0, 0, "text", 1}};
  std::vector<uint8_t> encoded;
  ASSERT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::OK);
  encoded[4] = ClippingCodec::VERSION + 1;
  std::vector<ClippingCodec::Record> decoded;
  EXPECT_EQ(ClippingCodec::decode(encoded.data(), encoded.size(), decoded), ClippingCodec::Status::NEWER_VERSION);
}

TEST(ClippingCodec, EnforcesSelectionAndBookLimits) {
  std::vector<ClippingCodec::Record> records(ClippingCodec::MAX_CLIPPINGS_PER_BOOK + 1, {0, 0, 0, 0, 0, "text", 1});
  std::vector<uint8_t> encoded;
  EXPECT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::LIMIT_EXCEEDED);
  records.resize(1);
  records[0].text.assign(ClippingCodec::MAX_TEXT_BYTES + 1, 'a');
  EXPECT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::LIMIT_EXCEEDED);
}

TEST(ClippingCodec, MigratesLegacyRecordsToStableNonzeroIds) {
  constexpr char text[] = "Văn Nhân Số";
  const size_t textLength = sizeof(text) - 1;
  std::vector<uint8_t> legacy(ClippingCodec::HEADER_SIZE + ClippingCodec::LEGACY_RECORD_HEADER_SIZE + textLength, 0);
  legacy[0] = 'V';
  legacy[1] = 'N';
  legacy[2] = 'S';
  legacy[3] = 'C';
  legacy[4] = 1;
  const auto writeU16 = [&](const size_t offset, const uint16_t value) {
    legacy[offset] = static_cast<uint8_t>(value);
    legacy[offset + 1] = static_cast<uint8_t>(value >> 8);
  };
  const auto writeU32 = [&](const size_t offset, const uint32_t value) {
    writeU16(offset, static_cast<uint16_t>(value));
    writeU16(offset + 2, static_cast<uint16_t>(value >> 16));
  };
  writeU16(6, 1);
  writeU32(8, static_cast<uint32_t>(legacy.size() - ClippingCodec::HEADER_SIZE));
  const size_t record = ClippingCodec::HEADER_SIZE;
  writeU16(record, 2);
  writeU16(record + 2, 7);
  writeU32(record + 4, 123);
  writeU16(record + 8, 1);
  writeU16(record + 10, 3);
  writeU16(record + 12, static_cast<uint16_t>(textLength));
  std::copy(text, text + textLength, legacy.begin() + static_cast<std::ptrdiff_t>(record + 16));
  writeU32(
      12, ClippingCodec::crc32(legacy.data() + ClippingCodec::HEADER_SIZE, legacy.size() - ClippingCodec::HEADER_SIZE));

  std::vector<ClippingCodec::Record> decoded;
  ASSERT_EQ(ClippingCodec::decode(legacy.data(), legacy.size(), decoded), ClippingCodec::Status::OK);
  ASSERT_EQ(decoded.size(), 1u);
  EXPECT_NE(decoded[0].id, 0u);

  std::vector<uint8_t> upgraded;
  ASSERT_EQ(ClippingCodec::encode(decoded, upgraded), ClippingCodec::Status::OK);
  EXPECT_EQ(upgraded[4], ClippingCodec::VERSION);
  std::vector<ClippingCodec::Record> roundTrip;
  EXPECT_EQ(ClippingCodec::decode(upgraded.data(), upgraded.size(), roundTrip), ClippingCodec::Status::OK);
  EXPECT_EQ(roundTrip, decoded);
}

TEST(ClippingCodec, StableIdAvoidsExistingCollisionsAndDuplicateWireIds) {
  ClippingCodec::Record first{1, 2, 3, 4, 5, "trùng lặp"};
  first.id = ClippingCodec::makeStableId(first, {});
  ClippingCodec::Record second = first;
  second.id = ClippingCodec::makeStableId(second, {first});
  EXPECT_NE(first.id, second.id);

  second.id = first.id;
  std::vector<uint8_t> encoded;
  EXPECT_EQ(ClippingCodec::encode({first, second}, encoded), ClippingCodec::Status::CORRUPT);
}

TEST(ClippingCodec, RoundTripsBoundedMultiPageSegments) {
  const auto record = multiPageRecord();
  ASSERT_TRUE(ClippingCodec::validRecord(record));
  std::vector<uint8_t> encoded;
  ASSERT_EQ(ClippingCodec::encode({record}, encoded), ClippingCodec::Status::OK);
  EXPECT_EQ(encoded[4], 3);
  std::vector<ClippingCodec::Record> decoded;
  ASSERT_EQ(ClippingCodec::decode(encoded.data(), encoded.size(), decoded), ClippingCodec::Status::OK);
  ASSERT_EQ(decoded.size(), 1u);
  EXPECT_EQ(decoded[0], record);
  EXPECT_EQ(ClippingCodec::segmentCount(decoded[0]), 2u);
}

TEST(ClippingCodec, RejectsGappedOverlappingAndOutOfOrderSegments) {
  auto record = multiPageRecord();
  record.segments[1].textOffset = 12;
  EXPECT_FALSE(ClippingCodec::validRecord(record));
  std::vector<uint8_t> encoded;
  EXPECT_EQ(ClippingCodec::encode({record}, encoded), ClippingCodec::Status::CORRUPT);

  record = multiPageRecord();
  record.segments[1].pageHint = record.segments[0].pageHint;
  EXPECT_EQ(ClippingCodec::encode({record}, encoded), ClippingCodec::Status::CORRUPT);

  record = multiPageRecord();
  record.segmentCount = ClippingCodec::MAX_SEGMENTS_PER_CLIPPING + 1;
  EXPECT_EQ(ClippingCodec::encode({record}, encoded), ClippingCodec::Status::LIMIT_EXCEEDED);
}

TEST(ClippingCodec, DecodesVersionTwoRecordsBeforeUpgradingOnNextSave) {
  constexpr char text[] = "v2 anchor";
  constexpr size_t textLength = sizeof(text) - 1;
  std::vector<uint8_t> bytes(ClippingCodec::HEADER_SIZE + ClippingCodec::V2_RECORD_HEADER_SIZE + textLength, 0);
  bytes[0] = 'V';
  bytes[1] = 'N';
  bytes[2] = 'S';
  bytes[3] = 'C';
  bytes[4] = 2;
  const auto writeU16 = [&](const size_t offset, const uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
  };
  const auto writeU32 = [&](const size_t offset, const uint32_t value) {
    writeU16(offset, static_cast<uint16_t>(value));
    writeU16(offset + 2, static_cast<uint16_t>(value >> 16));
  };
  writeU16(6, 1);
  writeU32(8, static_cast<uint32_t>(bytes.size() - ClippingCodec::HEADER_SIZE));
  const size_t record = ClippingCodec::HEADER_SIZE;
  writeU32(record, 77);
  writeU16(record + 4, 2);
  writeU16(record + 6, 7);
  writeU32(record + 8, 123);
  writeU16(record + 12, 1);
  writeU16(record + 14, 2);
  writeU16(record + 16, static_cast<uint16_t>(textLength));
  std::copy(text, text + textLength, bytes.begin() + static_cast<std::ptrdiff_t>(record + 20));
  writeU32(12,
           ClippingCodec::crc32(bytes.data() + ClippingCodec::HEADER_SIZE, bytes.size() - ClippingCodec::HEADER_SIZE));

  std::vector<ClippingCodec::Record> decoded;
  ASSERT_EQ(ClippingCodec::decode(bytes.data(), bytes.size(), decoded), ClippingCodec::Status::OK);
  ASSERT_EQ(decoded.size(), 1u);
  EXPECT_EQ(decoded[0], (ClippingCodec::Record{2, 7, 123, 1, 2, text, 77}));
}
