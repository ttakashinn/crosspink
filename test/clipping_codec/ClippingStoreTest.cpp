#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "ClippingStore.h"

namespace fs = std::filesystem;

class ClippingStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    root_ = fs::temp_directory_path() / "crosspoint_clipping_store_test" / info->name();
    cache_ = root_ / "epub_legacy";
    fs::remove_all(root_);
    fs::create_directories(cache_);
  }

  void TearDown() override { fs::remove_all(root_); }

  static uint64_t fnv1a64(const std::string& value) {
    uint64_t hash = 14695981039346656037ULL;
    for (const char byte : value) {
      hash ^= static_cast<uint8_t>(byte);
      hash *= 1099511628211ULL;
    }
    return hash;
  }

  fs::path durableFor(const std::string& source, const char* suffix = "") const {
    return root_ / "clippings" / ("epub_" + std::to_string(fnv1a64(source)) + ".bin" + suffix);
  }
  fs::path canonical() const { return durableFor(sourcePath_); }
  fs::path backup() const { return durableFor(sourcePath_, ".bak"); }
  fs::path temporary() const { return durableFor(sourcePath_, ".tmp"); }
  fs::path legacy() const { return cache_ / "clippings-vns.bin"; }

  void writeRecords(const fs::path& path, const std::vector<ClippingCodec::Record>& value) const {
    std::vector<uint8_t> bytes;
    ASSERT_EQ(ClippingCodec::encode(value, bytes), ClippingCodec::Status::OK);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  void writeRecords(const fs::path& path) const { writeRecords(path, records()); }

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

  fs::path root_;
  fs::path cache_;
  const std::string sourcePath_ = "/Books/Tieng Viet.epub";
};

TEST_F(ClippingStoreTest, RoundTripsCanonicalGeneration) {
  ASSERT_EQ(ClippingStore::save(sourcePath_, cache_.string(), records()), ClippingStore::SaveStatus::SAVED);
  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(sourcePath_, cache_.string(), loaded), ClippingStore::LoadStatus::LOADED);
  EXPECT_EQ(loaded, records());
}

TEST_F(ClippingStoreTest, FutureBackupBlocksLoadAndSaveEvenWhenCanonicalIsValid) {
  ASSERT_EQ(ClippingStore::save(sourcePath_, cache_.string(), records()), ClippingStore::SaveStatus::SAVED);
  writeFuture(backup());

  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(sourcePath_, cache_.string(), loaded), ClippingStore::LoadStatus::NEWER_VERSION);
  EXPECT_EQ(ClippingStore::save(sourcePath_, cache_.string(), records()), ClippingStore::SaveStatus::NEWER_VERSION);
  EXPECT_TRUE(fs::exists(canonical()));
  EXPECT_TRUE(fs::exists(backup()));
}

TEST_F(ClippingStoreTest, ValidTempRecoversWhenCanonicalIsMissing) {
  ASSERT_EQ(ClippingStore::save(sourcePath_, cache_.string(), records()), ClippingStore::SaveStatus::SAVED);
  fs::rename(canonical(), temporary());

  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(sourcePath_, cache_.string(), loaded), ClippingStore::LoadStatus::LOADED_TEMP);
  EXPECT_EQ(loaded, records());
}

TEST_F(ClippingStoreTest, NewTempWinsOverOldBackupAfterInterruptedPublish) {
  ASSERT_EQ(ClippingStore::save(sourcePath_, cache_.string(), records()), ClippingStore::SaveStatus::SAVED);
  fs::rename(canonical(), backup());
  const std::vector<ClippingCodec::Record> newer = {{2, 4, 90, 1, 3, "Bản mới", 84}};
  writeRecords(temporary(), newer);

  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(sourcePath_, cache_.string(), loaded), ClippingStore::LoadStatus::LOADED_TEMP);
  EXPECT_EQ(loaded, newer);
}

TEST_F(ClippingStoreTest, MigratesLegacyCacheLocalGeneration) {
  writeRecords(legacy());
  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(sourcePath_, cache_.string(), loaded), ClippingStore::LoadStatus::LOADED);
  EXPECT_EQ(loaded, records());
  EXPECT_TRUE(fs::exists(canonical()));
}

TEST_F(ClippingStoreTest, SurvivesGeneratedCacheDeletion) {
  ASSERT_EQ(ClippingStore::save(sourcePath_, cache_.string(), records()), ClippingStore::SaveStatus::SAVED);
  fs::remove_all(cache_);
  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(sourcePath_, cache_.string(), loaded), ClippingStore::LoadStatus::LOADED);
  EXPECT_EQ(loaded, records());
}

TEST_F(ClippingStoreTest, RekeysDataWhenBookMoves) {
  ASSERT_EQ(ClippingStore::save(sourcePath_, cache_.string(), records()), ClippingStore::SaveStatus::SAVED);
  const std::string movedSource = "/Read/Tieng Viet.epub";
  const fs::path movedCache = root_ / "epub_moved";
  ASSERT_TRUE(ClippingStore::migrate(sourcePath_, cache_.string(), movedSource, movedCache.string()));
  EXPECT_FALSE(fs::exists(canonical()));
  EXPECT_TRUE(fs::exists(durableFor(movedSource)));
  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(movedSource, movedCache.string(), loaded), ClippingStore::LoadStatus::LOADED);
  EXPECT_EQ(loaded, records());
}

TEST_F(ClippingStoreTest, StreamingInspectionAcceptsMultiPageGeneration) {
  ClippingCodec::Record record{1, 4, 20, 3, 8, "first page second page", 91};
  record.segmentCount = 2;
  record.segments[0] = {4, 20, 3, 8, 0, 10};
  record.segments[1] = {5, 80, 0, 1, 11, 11};
  const std::vector<ClippingCodec::Record> expected = {record};

  ASSERT_EQ(ClippingStore::save(sourcePath_, cache_.string(), expected), ClippingStore::SaveStatus::SAVED);
  std::vector<ClippingCodec::Record> loaded;
  EXPECT_EQ(ClippingStore::load(sourcePath_, cache_.string(), loaded), ClippingStore::LoadStatus::LOADED);
  EXPECT_EQ(loaded, expected);
}
