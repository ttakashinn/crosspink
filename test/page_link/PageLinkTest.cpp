#include <Epub/Epub/PageLink.h>
#include <gtest/gtest.h>

TEST(PageLinkTest, HitAreaIncludesFingerSlopAndMinimumWidth) {
  PageLink link;
  link.x = 100;
  link.y = 50;
  link.width = 8;
  link.height = 20;

  EXPECT_TRUE(link.contains(90, 44, 6, 28));
  EXPECT_TRUE(link.contains(117, 75, 6, 28));
  EXPECT_FALSE(link.contains(89, 50, 6, 28));
  EXPECT_FALSE(link.contains(118, 50, 6, 28));
  EXPECT_FALSE(link.contains(100, 76, 6, 28));
}

TEST(PageLinkTest, TouchScorePrefersActualAndThenNearestLink) {
  PageLink left;
  left.x = 100;
  left.y = 50;
  left.width = 8;
  left.height = 20;
  PageLink right = left;
  right.x = 124;

  EXPECT_LT(left.touchScore(106, 60, 6, 28), right.touchScore(106, 60, 6, 28));
  EXPECT_LT(right.touchScore(120, 60, 6, 28), left.touchScore(120, 60, 6, 28));
}
