#include <gtest/gtest.h>

#include "HomeMenuViewport.h"

TEST(HomeMenuViewportTest, KeepsFirstPageVisibleWhileCoverIsSelected) {
  const auto viewport = calculateHomeMenuViewport(7, -1, 300, 45, 8, 10);

  EXPECT_EQ(viewport.first, 0);
  EXPECT_EQ(viewport.count, 5);
  EXPECT_EQ(viewport.selected, -1);
}

TEST(HomeMenuViewportTest, PagesToKeepLastMenuItemAboveFooter) {
  const auto viewport = calculateHomeMenuViewport(7, 6, 300, 45, 8, 10);

  EXPECT_EQ(viewport.first, 2);
  EXPECT_EQ(viewport.count, 5);
  EXPECT_EQ(viewport.selected, 4);
}

TEST(HomeMenuViewportTest, UsesTrailingRowGapAsAvailableSpace) {
  const auto viewport = calculateHomeMenuViewport(6, 5, 430, 64, 8, 0);

  EXPECT_EQ(viewport.first, 0);
  EXPECT_EQ(viewport.count, 6);
  EXPECT_EQ(viewport.selected, 5);
}
