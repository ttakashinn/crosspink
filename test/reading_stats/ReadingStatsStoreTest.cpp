#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "activities/reader/ReadingStatsCodec.h"
#include "activities/reader/ReadingStatsStore.h"

namespace fs = std::filesystem;

class ReadingStatsStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    root_ = fs::temp_directory_path() / "crosspoint_reading_stats_test" / info->name();
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
    return root_ / "reading-stats" / ("epub_" + std::to_string(fnv1a64(source)) + ".bin" + suffix);
  }
  fs::path canonical() const { return durableFor(sourcePath_); }
  fs::path backup() const { return canonical().string() + ".bak"; }
  fs::path temporary() const { return canonical().string() + ".tmp"; }
  fs::path legacy() const { return cache_ / ReadingStatsStore::BOOK_FILE_NAME; }

  static void writeBytes(const fs::path& path, const ReadingStatsCodec::BookCodec::Encoded& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  fs::path root_;
  fs::path cache_;
  const std::string sourcePath_ = "/Books/Tieng Viet.epub";
};

TEST_F(ReadingStatsStoreTest, RoundTripsCanonicalFile) {
  const BookReadingStats expected{7200, 245, 8, true, 845};
  ASSERT_EQ(ReadingStatsStore::saveBook(sourcePath_, cache_.string(), expected), ReadingStatsStore::SaveStatus::SAVED);

  BookReadingStats actual;
  EXPECT_EQ(ReadingStatsStore::loadBook(sourcePath_, cache_.string(), actual), ReadingStatsStore::LoadStatus::LOADED);
  EXPECT_EQ(actual, expected);
  EXPECT_FALSE(fs::exists(backup()));
  EXPECT_FALSE(fs::exists(temporary()));
}

TEST_F(ReadingStatsStoreTest, RecoversValidTemporaryGeneration) {
  const BookReadingStats expected{3600, 120, 3, false, 420};
  ASSERT_EQ(ReadingStatsStore::saveBook(sourcePath_, cache_.string(), expected), ReadingStatsStore::SaveStatus::SAVED);
  fs::rename(canonical(), temporary());

  BookReadingStats actual;
  EXPECT_EQ(ReadingStatsStore::loadBook(sourcePath_, cache_.string(), actual), ReadingStatsStore::LoadStatus::LOADED);
  EXPECT_EQ(actual, expected);
}

TEST_F(ReadingStatsStoreTest, NewTempWinsOverOldBackupAfterInterruptedPublish) {
  const BookReadingStats older{1200, 30, 2, false, 250};
  const BookReadingStats newer{1800, 55, 3, false, 400};
  ASSERT_EQ(ReadingStatsStore::saveBook(sourcePath_, cache_.string(), older), ReadingStatsStore::SaveStatus::SAVED);
  fs::rename(canonical(), backup());
  writeBytes(temporary(), ReadingStatsCodec::BookCodec::encode(newer));

  BookReadingStats actual;
  EXPECT_EQ(ReadingStatsStore::loadBook(sourcePath_, cache_.string(), actual), ReadingStatsStore::LoadStatus::LOADED);
  EXPECT_EQ(actual, newer);
}

TEST_F(ReadingStatsStoreTest, FutureBackupBlocksLoadAndSave) {
  const BookReadingStats expected{600, 20, 1, false, 120};
  ASSERT_EQ(ReadingStatsStore::saveBook(sourcePath_, cache_.string(), expected), ReadingStatsStore::SaveStatus::SAVED);
  auto future = ReadingStatsCodec::BookCodec::encode(expected);
  future[4]++;
  writeBytes(backup(), future);

  BookReadingStats actual;
  EXPECT_EQ(ReadingStatsStore::loadBook(sourcePath_, cache_.string(), actual),
            ReadingStatsStore::LoadStatus::NEWER_VERSION);
  EXPECT_EQ(ReadingStatsStore::saveBook(sourcePath_, cache_.string(), expected),
            ReadingStatsStore::SaveStatus::NEWER_VERSION);
  EXPECT_TRUE(fs::exists(canonical()));
  EXPECT_TRUE(fs::exists(backup()));
}

TEST_F(ReadingStatsStoreTest, FallsBackFromCorruptCanonicalToVerifiedBackup) {
  const BookReadingStats expected{1800, 55, 2, false, 275};
  const auto valid = ReadingStatsCodec::BookCodec::encode(expected);
  writeBytes(backup(), valid);
  auto corrupt = valid;
  corrupt[8] ^= 0x55;
  writeBytes(canonical(), corrupt);

  BookReadingStats actual;
  EXPECT_EQ(ReadingStatsStore::loadBook(sourcePath_, cache_.string(), actual), ReadingStatsStore::LoadStatus::LOADED);
  EXPECT_EQ(actual, expected);
}

TEST_F(ReadingStatsStoreTest, MigratesLegacyCacheLocalGeneration) {
  const BookReadingStats expected{1800, 55, 2, false, 275};
  writeBytes(legacy(), ReadingStatsCodec::BookCodec::encode(expected));
  BookReadingStats actual;
  EXPECT_EQ(ReadingStatsStore::loadBook(sourcePath_, cache_.string(), actual), ReadingStatsStore::LoadStatus::LOADED);
  EXPECT_EQ(actual, expected);
  EXPECT_TRUE(fs::exists(canonical()));
}

TEST_F(ReadingStatsStoreTest, SurvivesGeneratedCacheDeletion) {
  const BookReadingStats expected{1800, 55, 2, false, 275};
  ASSERT_EQ(ReadingStatsStore::saveBook(sourcePath_, cache_.string(), expected), ReadingStatsStore::SaveStatus::SAVED);
  fs::remove_all(cache_);
  BookReadingStats actual;
  EXPECT_EQ(ReadingStatsStore::loadBook(sourcePath_, cache_.string(), actual), ReadingStatsStore::LoadStatus::LOADED);
  EXPECT_EQ(actual, expected);
}

TEST_F(ReadingStatsStoreTest, RekeysDataWhenBookMoves) {
  const BookReadingStats expected{1800, 55, 2, false, 275};
  ASSERT_EQ(ReadingStatsStore::saveBook(sourcePath_, cache_.string(), expected), ReadingStatsStore::SaveStatus::SAVED);
  const std::string movedSource = "/Read/Tieng Viet.epub";
  const fs::path movedCache = root_ / "epub_moved";
  ASSERT_TRUE(ReadingStatsStore::migrateBook(sourcePath_, cache_.string(), movedSource, movedCache.string()));
  EXPECT_FALSE(fs::exists(canonical()));
  EXPECT_TRUE(fs::exists(durableFor(movedSource)));
  BookReadingStats actual;
  EXPECT_EQ(ReadingStatsStore::loadBook(movedSource, movedCache.string(), actual),
            ReadingStatsStore::LoadStatus::LOADED);
  EXPECT_EQ(actual, expected);
}
