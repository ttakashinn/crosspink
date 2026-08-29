#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "Epub/Epub/blocks/BlockStyle.h"

TEST(BlockStyleTest, CombinedInsetsSaturateInsteadOfWrapping) {
  BlockStyle parent;
  parent.marginLeft = std::numeric_limits<int16_t>::max();
  parent.paddingRight = std::numeric_limits<int16_t>::max();

  BlockStyle child;
  child.marginLeft = 100;
  child.paddingRight = 100;

  const BlockStyle combined = parent.getCombinedBlockStyle(child, BlockStyle::CombineAxis::Horizontal);
  EXPECT_EQ(combined.marginLeft, std::numeric_limits<int16_t>::max());
  EXPECT_EQ(combined.paddingRight, std::numeric_limits<int16_t>::max());
  EXPECT_GT(combined.totalHorizontalInset(), std::numeric_limits<int16_t>::max());
}

TEST(BlockStyleTest, HorizontalLayoutClampsNegativeMarginsToViewport) {
  BlockStyle style;
  style.marginLeft = -80;
  style.paddingLeft = 20;
  style.marginRight = 30;

  const auto layout = style.resolveHorizontalLayout(480);
  EXPECT_EQ(layout.xOffset, 0);
  EXPECT_EQ(layout.contentWidth, 450);
}

TEST(BlockStyleTest, HorizontalLayoutPreservesReadableWidthAndInsetRatio) {
  BlockStyle style;
  style.marginLeft = 300;
  style.marginRight = 100;

  const auto layout = style.resolveHorizontalLayout(480, 160);
  EXPECT_EQ(layout.contentWidth, 160);
  EXPECT_EQ(layout.xOffset, 240);
}

TEST(BlockStyleTest, HorizontalLayoutHandlesZeroViewport) {
  BlockStyle style;
  style.marginLeft = 20;

  const auto layout = style.resolveHorizontalLayout(0, 10);
  EXPECT_EQ(layout.xOffset, 0);
  EXPECT_EQ(layout.contentWidth, 0);
}
