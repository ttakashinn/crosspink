#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "DictionaryHistoryStore.h"
#include "HalStorage.h"

namespace {

class DictionaryHistoryStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("vns-dictionary-history-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
    path_ = (root_ / "history.txt").string();
    Storage.resetFaults();
    DICTIONARY_HISTORY.resetForTests(path_);
  }

  void TearDown() override {
    DICTIONARY_HISTORY.resetForTests();
    Storage.resetFaults();
    std::filesystem::remove_all(root_);
  }

  std::filesystem::path root_;
  std::string path_;
};

}  // namespace

TEST_F(DictionaryHistoryStoreTest, CodecRoundTripsAndRejectsNonCanonicalOrFutureData) {
  const std::vector<std::string> expected = {"tiếng", "sách"};
  std::string data;
  ASSERT_EQ(DictionaryHistoryStore::encode(expected, data), DictionaryHistoryStore::CodecStatus::OK);
  std::vector<std::string> decoded;
  EXPECT_EQ(DictionaryHistoryStore::decode(reinterpret_cast<const uint8_t*>(data.data()), data.size(), decoded),
            DictionaryHistoryStore::CodecStatus::OK);
  EXPECT_EQ(decoded, expected);

  EXPECT_EQ(DictionaryHistoryStore::encode({"sách", "sách"}, data), DictionaryHistoryStore::CodecStatus::INVALID);
  EXPECT_EQ(DictionaryHistoryStore::encode({"SÁCH"}, data), DictionaryHistoryStore::CodecStatus::INVALID);
  const std::string future = "VNS_DICT_HISTORY_V2\ntiếng\n";
  EXPECT_EQ(DictionaryHistoryStore::decode(reinterpret_cast<const uint8_t*>(future.data()), future.size(), decoded),
            DictionaryHistoryStore::CodecStatus::NEWER_VERSION);
}

TEST_F(DictionaryHistoryStoreTest, PersistsNewestFirstDeduplicatesAndCapsAtFifteen) {
  for (int i = 0; i < 17; ++i) {
    DICTIONARY_HISTORY.record("word" + std::to_string(i));
    ASSERT_TRUE(DICTIONARY_HISTORY.flush());
  }
  DICTIONARY_HISTORY.record("WORD10");
  ASSERT_TRUE(DICTIONARY_HISTORY.flush());
  ASSERT_EQ(DICTIONARY_HISTORY.entries().size(), DictionaryHistoryStore::MAX_ENTRIES);
  EXPECT_EQ(DICTIONARY_HISTORY.entries().front(), "word10");

  DICTIONARY_HISTORY.resetForTests(path_);
  ASSERT_TRUE(DICTIONARY_HISTORY.load());
  ASSERT_EQ(DICTIONARY_HISTORY.entries().size(), DictionaryHistoryStore::MAX_ENTRIES);
  EXPECT_EQ(DICTIONARY_HISTORY.entries().front(), "word10");
  EXPECT_TRUE(std::filesystem::exists(path_ + ".bak"));
}

TEST_F(DictionaryHistoryStoreTest, PartialTempWriteKeepsLastVerifiedGeneration) {
  DICTIONARY_HISTORY.record("alpha");
  ASSERT_TRUE(DICTIONARY_HISTORY.flush());
  DICTIONARY_HISTORY.record("beta");
  Storage.failNextWrite();
  EXPECT_FALSE(DICTIONARY_HISTORY.flush());

  DICTIONARY_HISTORY.resetForTests(path_);
  ASSERT_TRUE(DICTIONARY_HISTORY.load());
  ASSERT_EQ(DICTIONARY_HISTORY.entries().size(), 1U);
  EXPECT_EQ(DICTIONARY_HISTORY.entries().front(), "alpha");
}

TEST_F(DictionaryHistoryStoreTest, MalformedPrimaryIsBrowsableFromBackupButReadOnly) {
  DICTIONARY_HISTORY.record("alpha");
  ASSERT_TRUE(DICTIONARY_HISTORY.flush());
  DICTIONARY_HISTORY.record("beta");
  ASSERT_TRUE(DICTIONARY_HISTORY.flush());
  {
    std::ofstream corrupt(path_, std::ios::binary | std::ios::trunc);
    corrupt << "broken";
  }

  DICTIONARY_HISTORY.resetForTests(path_);
  EXPECT_FALSE(DICTIONARY_HISTORY.load());
  EXPECT_FALSE(DICTIONARY_HISTORY.isWritable());
  ASSERT_EQ(DICTIONARY_HISTORY.entries().size(), 1U);
  EXPECT_EQ(DICTIONARY_HISTORY.entries().front(), "alpha");
  DICTIONARY_HISTORY.record("gamma");
  EXPECT_FALSE(DICTIONARY_HISTORY.flush());
}
