#include <Epub/converters/DitherUtils.h>
#include <Epub/converters/PixelCacheFormat.h>
#include <gtest/gtest.h>

#include <cstdlib>

TEST(DitherUtilsTest, PixelCacheFormatInvalidatesLegacyQuantization) {
  EXPECT_EQ(2, pixel_cache_format::VERSION);
  EXPECT_EQ(7u, pixel_cache_format::HEADER_SIZE);
  EXPECT_GT(pixel_cache_format::MAGIC, 800);
}

TEST(DitherUtilsTest, NativePanelLevelsRemainStable) {
  for (const uint8_t gray : {0, 85, 170, 255}) {
    const uint8_t expected = gray / 85;
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        EXPECT_EQ(expected, applyBayerDither4Level(gray, x, y)) << "gray=" << static_cast<int>(gray);
      }
    }
  }
}

TEST(DitherUtilsTest, BlockAverageTracksInputLuminance) {
  for (int gray = 0; gray <= 255; ++gray) {
    int nativeLevelSum = 0;
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        nativeLevelSum += applyBayerDither4Level(static_cast<uint8_t>(gray), x, y);
      }
    }
    const int reconstructedTimes16 = nativeLevelSum * 85;
    EXPECT_LE(std::abs(reconstructedTimes16 - gray * 16), 85) << "gray=" << gray << " sum=" << nativeLevelSum;
  }
}

TEST(DitherUtilsTest, OutputIsMonotonicAtEveryMatrixPosition) {
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      uint8_t previous = 0;
      for (int gray = 0; gray <= 255; ++gray) {
        const uint8_t current = applyBayerDither4Level(static_cast<uint8_t>(gray), x, y);
        EXPECT_GE(current, previous);
        EXPECT_LE(current, 3);
        previous = current;
      }
    }
  }
}
