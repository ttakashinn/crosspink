#include <gtest/gtest.h>

#include "ReaderSyncPolicy.h"

TEST(ReaderSyncPolicy, PreservesEveryValidPerBookOrientation) {
  constexpr uint8_t orientationCount = 4;
  for (uint8_t orientation = 0; orientation < orientationCount; ++orientation) {
    EXPECT_EQ(reader_sync::displayOrientation(orientation, 0, orientationCount), orientation);
  }
}

TEST(ReaderSyncPolicy, FallsBackSafelyForInvalidValues) {
  EXPECT_EQ(reader_sync::displayOrientation(4, 2, 4), 2);
  EXPECT_EQ(reader_sync::displayOrientation(255, 255, 4), 0);
  EXPECT_EQ(reader_sync::displayOrientation(0, 0, 0), 0);
}
