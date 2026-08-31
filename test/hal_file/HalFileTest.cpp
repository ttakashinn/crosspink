#include <gtest/gtest.h>

#include "HalStorage.h"

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
