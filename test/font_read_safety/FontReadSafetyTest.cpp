#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "FontReadSafety.h"

TEST(FontReadSafetyTest, AcceptsOnlyRangesFullyInsideTheFontFile) {
  EXPECT_TRUE(font_read_safety::containsRange(100, 0, 100));
  EXPECT_TRUE(font_read_safety::containsRange(100, 100, 0));
  EXPECT_FALSE(font_read_safety::containsRange(100, 101, 0));
  EXPECT_FALSE(font_read_safety::containsRange(100, 90, 11));
  EXPECT_FALSE(font_read_safety::containsRange(100, std::numeric_limits<uint64_t>::max(), 2));
}

TEST(FontReadSafetyTest, RejectsBitmapAccumulatorOverflow) {
  uint32_t result = 0;
  EXPECT_TRUE(font_read_safety::addUint32(100, 23, result));
  EXPECT_EQ(123U, result);

  result = 77;
  EXPECT_FALSE(font_read_safety::addUint32(std::numeric_limits<uint32_t>::max() - 2, 3, result));
  EXPECT_EQ(77U, result);
}
