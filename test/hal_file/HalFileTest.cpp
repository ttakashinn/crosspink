#include <gtest/gtest.h>

#include "HalStorage.h"
#include "SDCardManager.h"

class HalFileIoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    hal_file_test::fileError = 0;
    hal_file_test::blockReadResult = 0;
  }
};

TEST(HalFileClose, DefaultConstructedHandleIsAlreadyClosed) {
  HalFile file;

  EXPECT_FALSE(file.isOpen());
  EXPECT_TRUE(file.close());
  EXPECT_TRUE(file.close());
}

TEST(HalFileClose, OpenHandleCanStillBeClosedNormally) {
  HalFile file = Storage.open("/fixture.bin");

  ASSERT_TRUE(file.isOpen());
  EXPECT_TRUE(file.close());
  EXPECT_FALSE(file.isOpen());
}

TEST(HalStorageCapacity, ForwardsCapacityThroughTheThreadSafeStorageLayer) {
  EXPECT_EQ(Storage.totalBytes(), 32ULL * 1024ULL * 1024ULL);
  EXPECT_EQ(Storage.usedBytes(), 8ULL * 1024ULL * 1024ULL);
}

TEST_F(HalFileIoTest, ExposesWrappedFilesystemError) {
  HalFile file = Storage.open("/fixture.bin");
  ASSERT_TRUE(file.isOpen());

  hal_file_test::fileError = 7;
  EXPECT_EQ(file.getError(), 7);
}

TEST_F(HalFileIoTest, NormalizesNegativeBlockReadToZero) {
  HalFile file = Storage.open("/fixture.bin");
  ASSERT_TRUE(file.isOpen());

  hal_file_test::blockReadResult = -1;
  uint8_t byte = 0;
  EXPECT_EQ(file.read(&byte, sizeof(byte)), 0);
}
