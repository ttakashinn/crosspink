#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "ClippingStore.h"

namespace fs = std::filesystem;

class ClippingStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    directory_ = fs::temp_directory_path() / "crosspoint_clipping_store_test" / info->name();
    fs::remove_all(directory_);
    fs::create_directories(directory_);
  }

  void TearDown() override { fs::remove_all(directory_); }

  fs::path canonical() const { return directory_ / "clippings-vns.bin"; }
  fs::path backup() const { return directory_ / "clippings-vns.bin.bak"; }
  fs::path temporary() const { return directory_ / "clippings-vns.bin.tmp"; }

  void writeFuture(const fs::path& path) const {
    std::array<uint8_t, ClippingCodec::HEADER_SIZE> header{};
    header[0] = 'V';
    header[1] = 'N';
    header[2] = 'S';
    header[3] = 'C';
    header[4] = ClippingCodec::VERSION + 1;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
  }

  std::vector<ClippingCodec::Record> records() const { return {{1, 2, 30, 4, 6, "Tiếng Việt", 42}}; }

  fs::path directory_;
};

TEST_F(ClippingStoreTest, RoundTripsCanonicalGeneration) {
  ASSERT_EQ(ClippingStore::save(directory_.string(), records()), ClippingStore::SaveStatus::SAVED);
  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(directory_.string(), loaded), ClippingStore::LoadStatus::LOADED);
  EXPECT_EQ(loaded, records());
}

TEST_F(ClippingStoreTest, FutureBackupBlocksLoadAndSaveEvenWhenCanonicalIsValid) {
  ASSERT_EQ(ClippingStore::save(directory_.string(), records()), ClippingStore::SaveStatus::SAVED);
  writeFuture(backup());

  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(directory_.string(), loaded), ClippingStore::LoadStatus::NEWER_VERSION);
  EXPECT_EQ(ClippingStore::save(directory_.string(), records()), ClippingStore::SaveStatus::NEWER_VERSION);
  EXPECT_TRUE(fs::exists(canonical()));
  EXPECT_TRUE(fs::exists(backup()));
}

TEST_F(ClippingStoreTest, ValidTempRecoversWhenCanonicalIsMissing) {
  ASSERT_EQ(ClippingStore::save(directory_.string(), records()), ClippingStore::SaveStatus::SAVED);
  fs::rename(canonical(), temporary());

  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(directory_.string(), loaded), ClippingStore::LoadStatus::LOADED_TEMP);
  EXPECT_EQ(loaded, records());
}
