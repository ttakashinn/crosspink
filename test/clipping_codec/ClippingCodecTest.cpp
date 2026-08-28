#include <gtest/gtest.h>

#include "ClippingCodec.h"

TEST(ClippingCodec, RoundTripsVietnameseTextAndAnchors) {
  const std::vector<ClippingCodec::Record> records = {
      {3, 12, 420, 4, 9, "Tiếng Việt giàu thanh điệu và dấu phụ."},
      {5, 2, 99, 0, 2, "Văn Nhân Số"},
  };
  std::vector<uint8_t> encoded;
  ASSERT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::OK);
  std::vector<ClippingCodec::Record> decoded;
  EXPECT_EQ(ClippingCodec::decode(encoded.data(), encoded.size(), decoded), ClippingCodec::Status::OK);
  EXPECT_EQ(decoded, records);
}

TEST(ClippingCodec, RejectsTornAndCorruptPayloads) {
  const std::vector<ClippingCodec::Record> records = {{0, 0, 0, 0, 0, "một"}};
  std::vector<uint8_t> encoded;
  ASSERT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::OK);
  std::vector<ClippingCodec::Record> decoded;
  EXPECT_EQ(ClippingCodec::decode(encoded.data(), encoded.size() - 1, decoded), ClippingCodec::Status::TRUNCATED);
  encoded.back() ^= 0x20;
  EXPECT_EQ(ClippingCodec::decode(encoded.data(), encoded.size(), decoded), ClippingCodec::Status::BAD_CRC);
}

TEST(ClippingCodec, RefusesFutureVersionWithoutOverwritingMeaning) {
  const std::vector<ClippingCodec::Record> records = {{0, 0, 0, 0, 0, "text"}};
  std::vector<uint8_t> encoded;
  ASSERT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::OK);
  encoded[4] = ClippingCodec::VERSION + 1;
  std::vector<ClippingCodec::Record> decoded;
  EXPECT_EQ(ClippingCodec::decode(encoded.data(), encoded.size(), decoded), ClippingCodec::Status::NEWER_VERSION);
}

TEST(ClippingCodec, EnforcesSelectionAndBookLimits) {
  std::vector<ClippingCodec::Record> records(ClippingCodec::MAX_CLIPPINGS_PER_BOOK + 1, {0, 0, 0, 0, 0, "text"});
  std::vector<uint8_t> encoded;
  EXPECT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::LIMIT_EXCEEDED);
  records.resize(1);
  records[0].text.assign(ClippingCodec::MAX_TEXT_BYTES + 1, 'a');
  EXPECT_EQ(ClippingCodec::encode(records, encoded), ClippingCodec::Status::LIMIT_EXCEEDED);
}
