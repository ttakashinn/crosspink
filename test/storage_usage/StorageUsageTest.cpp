#include <gtest/gtest.h>

#include "util/StorageUsage.h"

namespace usage = storage_usage;

TEST(StorageUsage, FormatsAbsoluteValuesAndPercentageInOneCompactLabel) {
  constexpr uint64_t GIB = 1024ULL * 1024ULL * 1024ULL;
  char output[48];

  EXPECT_TRUE(usage::format({8ULL * GIB, 32ULL * GIB}, output, sizeof(output)));
  EXPECT_STREQ(output, "8.0 / 32.0 GiB (25%)");
  EXPECT_EQ(usage::percent({8ULL * GIB, 32ULL * GIB}), 25);
}

TEST(StorageUsage, UsesMebibytesForSmallCardsAndClampsInvalidUsedValues) {
  constexpr uint64_t MIB = 1024ULL * 1024ULL;
  char output[48];

  EXPECT_TRUE(usage::format({640ULL * MIB, 512ULL * MIB}, output, sizeof(output)));
  EXPECT_STREQ(output, "512.0 / 512.0 MiB (100%)");
}

TEST(StorageUsage, RejectsUnavailableOrTruncatedLabels) {
  char output[8] = "stale";
  EXPECT_FALSE(usage::format({}, output, sizeof(output)));
  EXPECT_STREQ(output, "");

  EXPECT_FALSE(usage::format({1, 1024ULL * 1024ULL * 1024ULL}, output, sizeof(output)));
}
