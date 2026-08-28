#include <gtest/gtest.h>

#include "SmallCaps.h"

namespace {

uint32_t uppercase(const uint32_t cp) {
  uint32_t result = 0;
  EXPECT_TRUE(smallcaps::uppercaseCodepoint(cp, result));
  return result;
}

TEST(SmallCapsTest, MapsVietnameseAlphabetAndToneLetters) {
  EXPECT_EQ(uppercase(0x0103), 0x0102);  // ă -> Ă
  EXPECT_EQ(uppercase(0x0111), 0x0110);  // đ -> Đ
  EXPECT_EQ(uppercase(0x01A1), 0x01A0);  // ơ -> Ơ
  EXPECT_EQ(uppercase(0x01B0), 0x01AF);  // ư -> Ư
  EXPECT_EQ(uppercase(0x1EA1), 0x1EA0);  // ạ -> Ạ
  EXPECT_EQ(uppercase(0x1ED9), 0x1ED8);  // ộ -> Ộ
  EXPECT_EQ(uppercase(0x1EF1), 0x1EF0);  // ự -> Ự
  EXPECT_EQ(uppercase(0x1EF9), 0x1EF8);  // ỹ -> Ỹ
}

TEST(SmallCapsTest, HandlesLatinParityExceptionsWithoutGuessing) {
  EXPECT_EQ(uppercase(0x0131), static_cast<uint32_t>('I'));
  EXPECT_EQ(uppercase(0x013A), 0x0139);
  EXPECT_EQ(uppercase(0x014B), 0x014A);
  EXPECT_EQ(uppercase(0x017E), 0x017D);

  uint32_t result = 0;
  EXPECT_FALSE(smallcaps::uppercaseCodepoint(0x00DF, result));  // ß needs a multi-codepoint expansion
  EXPECT_EQ(result, 0x00DF);
  EXPECT_FALSE(smallcaps::uppercaseCodepoint(0x00F7, result));  // division sign
  EXPECT_EQ(result, 0x00F7);
}

TEST(SmallCapsTest, UsesSymmetricThreeQuarterFixedPointScaling) {
  EXPECT_EQ(smallcaps::scaleAdvance(16), 12);
  EXPECT_EQ(smallcaps::scaleAdvance(17), 13);
  EXPECT_EQ(smallcaps::scaleAdvance(-16), -12);
  EXPECT_EQ(smallcaps::scaleAdvance(-17), -13);
  EXPECT_EQ(smallcaps::scaleExtent(5), 4);
}

}  // namespace
