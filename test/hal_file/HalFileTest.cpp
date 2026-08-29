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
