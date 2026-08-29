#include <HalStorage.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "util/BookCacheUtils.h"

namespace fs = std::filesystem;

class BookCacheUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    directory_ = fs::temp_directory_path() / "crosspoint_book_cache_test" / info->name();
    fs::remove_all(directory_);
    fs::create_directories(directory_);
    Storage.clearFailure();
  }

  void TearDown() override {
    Storage.clearFailure();
    fs::remove_all(directory_);
  }

  std::string path(const char* name) const { return (directory_ / name).string(); }

  void createBook(const std::string& bookPath) const {
    std::ofstream output(bookPath, std::ios::binary);
    output << "book";
  }

  void createCache(const std::string& bookPath) const {
    std::string cachePath;
    ASSERT_TRUE(getBookCachePath(bookPath, cachePath));
    fs::create_directories(cachePath);
    std::ofstream(cachePath + "/progress.bin") << "state";
  }

  fs::path directory_;
};

TEST_F(BookCacheUtilsTest, MovesBookAndItsCacheTogether) {
  const std::string source = path("source.epub");
  const std::string destination = path("destination.epub");
  createBook(source);
  createCache(source);
  std::string sourceCache;
  std::string destinationCache;
  ASSERT_TRUE(getBookCachePath(source, sourceCache));
  ASSERT_TRUE(getBookCachePath(destination, destinationCache));

  EXPECT_TRUE(moveBookWithCache(source, destination));
  EXPECT_FALSE(fs::exists(source));
  EXPECT_TRUE(fs::exists(destination));
  EXPECT_FALSE(fs::exists(sourceCache));
  EXPECT_TRUE(fs::exists(destinationCache + "/progress.bin"));
}

TEST_F(BookCacheUtilsTest, CacheMoveFailureRollsBackBookPath) {
  const std::string source = path("source.epub");
  const std::string destination = path("destination.epub");
  createBook(source);
  createCache(source);
  std::string sourceCache;
  std::string destinationCache;
  ASSERT_TRUE(getBookCachePath(source, sourceCache));
  ASSERT_TRUE(getBookCachePath(destination, destinationCache));
  Storage.failNextRename(sourceCache, destinationCache);

  EXPECT_FALSE(moveBookWithCache(source, destination));
  EXPECT_TRUE(fs::exists(source));
  EXPECT_FALSE(fs::exists(destination));
  EXPECT_TRUE(fs::exists(sourceCache + "/progress.bin"));
  EXPECT_FALSE(fs::exists(destinationCache));
}

TEST_F(BookCacheUtilsTest, DestinationCacheConflictLeavesBothSidesUntouched) {
  const std::string source = path("source.epub");
  const std::string destination = path("destination.epub");
  createBook(source);
  createCache(source);
  createCache(destination);

  EXPECT_FALSE(moveBookWithCache(source, destination));
  EXPECT_TRUE(fs::exists(source));
  EXPECT_FALSE(fs::exists(destination));
  std::string sourceCache;
  std::string destinationCache;
  ASSERT_TRUE(getBookCachePath(source, sourceCache));
  ASSERT_TRUE(getBookCachePath(destination, destinationCache));
  EXPECT_TRUE(fs::exists(sourceCache + "/progress.bin"));
  EXPECT_TRUE(fs::exists(destinationCache + "/progress.bin"));
}
