#include <gtest/gtest.h>
#include <util/AdaptiveGrayscaleStrip.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace {

std::unique_ptr<uint8_t[]> allocateBytes(const size_t bytes) {
  return std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[bytes]);
}

}  // namespace

TEST(AdaptiveGrayscaleStripTest, UsesFullBandWhenFirstAllocationSucceeds) {
  uint16_t rows = 0;
  std::vector<size_t> attempts;
  auto buffer = adaptive_grayscale_strip::allocate(100, rows, [&](const size_t bytes) {
    attempts.push_back(bytes);
    return allocateBytes(bytes);
  });

  ASSERT_NE(nullptr, buffer);
  EXPECT_EQ(80, rows);
  EXPECT_EQ((std::vector<size_t>{8000}), attempts);
}

TEST(AdaptiveGrayscaleStripTest, RetriesProgressivelySmallerBands) {
  uint16_t rows = 0;
  std::vector<size_t> attempts;
  auto buffer = adaptive_grayscale_strip::allocate(100, rows, [&](const size_t bytes) {
    attempts.push_back(bytes);
    return bytes <= 2000 ? allocateBytes(bytes) : nullptr;
  });

  ASSERT_NE(nullptr, buffer);
  EXPECT_EQ(20, rows);
  EXPECT_EQ((std::vector<size_t>{8000, 4000, 2000}), attempts);
}

TEST(AdaptiveGrayscaleStripTest, ReportsFailureOnlyAfterEveryBoundedRetry) {
  uint16_t rows = 99;
  std::vector<size_t> attempts;
  auto buffer = adaptive_grayscale_strip::allocate(100, rows, [&](const size_t bytes) {
    attempts.push_back(bytes);
    return std::unique_ptr<uint8_t[]>();
  });

  EXPECT_EQ(nullptr, buffer);
  EXPECT_EQ(0, rows);
  EXPECT_EQ((std::vector<size_t>{8000, 4000, 2000, 1000}), attempts);
}

TEST(AdaptiveGrayscaleStripTest, RejectsInvalidRowWidthWithoutAllocating) {
  uint16_t rows = 99;
  bool called = false;
  auto buffer = adaptive_grayscale_strip::allocate(0, rows, [&](const size_t bytes) {
    called = true;
    return allocateBytes(bytes);
  });

  EXPECT_EQ(nullptr, buffer);
  EXPECT_EQ(0, rows);
  EXPECT_FALSE(called);
}
