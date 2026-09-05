#include <gtest/gtest.h>

#include "EpdFont/FontDownloadProgressPolicy.h"

TEST(FontDownloadProgressPolicy, RequiresCoarseStepAndMinimumInterval) {
  EXPECT_FALSE(font_download_progress::shouldPublish(0, 24, 5000, false));
  EXPECT_FALSE(font_download_progress::shouldPublish(0, 25, 999, false));
  EXPECT_TRUE(font_download_progress::shouldPublish(0, 25, 1000, false));
  EXPECT_FALSE(font_download_progress::shouldPublish(25, 49, 5000, false));
  EXPECT_TRUE(font_download_progress::shouldPublish(25, 50, 1000, false));
}

TEST(FontDownloadProgressPolicy, PublishesCompletionExactlyOnce) {
  EXPECT_TRUE(font_download_progress::shouldPublish(75, 100, 0, true));
  EXPECT_FALSE(font_download_progress::shouldPublish(100, 100, 5000, true));
}

TEST(FontDownloadProgressPolicy, HandlesUnknownAndOverReportedTotals) {
  EXPECT_EQ(font_download_progress::percent(100, 0), 0);
  EXPECT_FALSE(font_download_progress::isComplete(0, 0));
  EXPECT_EQ(font_download_progress::percent(150, 100), 100);
  EXPECT_TRUE(font_download_progress::isComplete(150, 100));
}

TEST(FontDownloadProgressPolicy, ElapsedTimeArithmeticSurvivesMillisWraparound) {
  const uint32_t beforeWrap = UINT32_MAX - 499;
  const uint32_t afterWrap = 500;
  EXPECT_EQ(afterWrap - beforeWrap, 1000U);
  EXPECT_TRUE(font_download_progress::shouldPublish(0, 25, afterWrap - beforeWrap, false));
}
