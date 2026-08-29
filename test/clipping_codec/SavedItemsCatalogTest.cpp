#include <gtest/gtest.h>

#include "saved_items/SavedItemsCatalog.h"

TEST(SavedItemsCatalogCodec, RoundTripsVietnameseMetadataAndCounts) {
  const std::vector<SavedItemsCatalog::Entry> expected = {
      {"/Sách/Con Chim Trốn Tuyết.epub", "Con Chim Trốn Tuyết", "Tác giả Việt", 3, 2},
      {"/Read/CPRE.epub", "CPRE Foundation", "", 1, 0},
  };
  std::vector<uint8_t> bytes;
  ASSERT_EQ(SavedItemsCatalog::encode(expected, bytes), SavedItemsCatalog::CodecStatus::OK);
  std::vector<SavedItemsCatalog::Entry> actual;
  EXPECT_EQ(SavedItemsCatalog::decode(bytes.data(), bytes.size(), actual), SavedItemsCatalog::CodecStatus::OK);
  EXPECT_EQ(actual, expected);
}

TEST(SavedItemsCatalogCodec, RejectsCorruptionDuplicatesAndEmptyEntries) {
  const std::vector<SavedItemsCatalog::Entry> valid = {{"/Books/a.epub", "A", "", 1, 1}};
  std::vector<uint8_t> bytes;
  ASSERT_EQ(SavedItemsCatalog::encode(valid, bytes), SavedItemsCatalog::CodecStatus::OK);
  bytes.back() ^= 0x40;
  std::vector<SavedItemsCatalog::Entry> decoded;
  EXPECT_EQ(SavedItemsCatalog::decode(bytes.data(), bytes.size(), decoded), SavedItemsCatalog::CodecStatus::BAD_CRC);

  EXPECT_EQ(SavedItemsCatalog::encode({valid[0], valid[0]}, bytes), SavedItemsCatalog::CodecStatus::INVALID);
  EXPECT_EQ(SavedItemsCatalog::encode({{"/Books/empty.epub", "Empty", "", 0, 0}}, bytes),
            SavedItemsCatalog::CodecStatus::INVALID);
}

TEST(SavedItemsCatalogCodec, PreservesFutureVersionSignal) {
  std::vector<uint8_t> bytes;
  ASSERT_EQ(SavedItemsCatalog::encode({{"/Books/a.epub", "A", "", 1, 0}}, bytes), SavedItemsCatalog::CodecStatus::OK);
  bytes[4] = SavedItemsCatalog::VERSION + 1;
  std::vector<SavedItemsCatalog::Entry> decoded;
  EXPECT_EQ(SavedItemsCatalog::decode(bytes.data(), bytes.size(), decoded),
            SavedItemsCatalog::CodecStatus::NEWER_VERSION);
}
