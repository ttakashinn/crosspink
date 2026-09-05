#include <gtest/gtest.h>

#include "HalStorage.h"
#include "SDCardManager.h"
#include "SdCardFontRegistry.h"

class HalFileIoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    hal_file_test::fileError = 0;
    hal_file_test::blockReadResult = 0;
    hal_file_test::pathExists = false;
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

TEST(HalFileAllocationFailure, OpenReturnsInvalidHandleInsteadOfAborting) {
  HalFile::failNextImplAllocationForTest();

  HalFile file = Storage.open("/fixture.bin");

  EXPECT_FALSE(file.isOpen());
  EXPECT_TRUE(file.close());
}

TEST(HalFileAllocationFailure, ReadOpenClearsOutputHandle) {
  HalFile file = Storage.open("/fixture.bin");
  ASSERT_TRUE(file.isOpen());
  HalFile::failNextImplAllocationForTest();

  EXPECT_FALSE(Storage.openFileForRead("TEST", "/fixture.bin", file));
  EXPECT_FALSE(file.isOpen());
}

TEST(SdCardFontRegistryFailure, DistinguishesEmptyRootsFromAllocationFailure) {
  SdCardFontRegistry emptyRegistry;
  EXPECT_FALSE(emptyRegistry.discover());
  EXPECT_FALSE(emptyRegistry.lastDiscoveryFailed());

  SdCardFontRegistry failedRegistry;
  hal_file_test::pathExists = true;
  HalFile::failNextImplAllocationForTest();
  EXPECT_FALSE(failedRegistry.discover());
  EXPECT_TRUE(failedRegistry.lastDiscoveryFailed());
  EXPECT_TRUE(failedRegistry.getFamilies().empty());
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
