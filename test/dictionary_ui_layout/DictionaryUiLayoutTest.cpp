#include <gtest/gtest.h>

#include "DictionaryUiLayout.h"

TEST(DictionaryUiLayout, HeadwordUsesSafeBandWhenThereIsNoCounter) {
  const auto layout = dictionary_ui::definitionHeaderLayout(40, 720, 20, 0, 8);
  EXPECT_EQ(layout.titleX, 60);
  EXPECT_EQ(layout.titleWidth, 680);
  EXPECT_EQ(layout.counterX, 740);
}

TEST(DictionaryUiLayout, PageCounterCannotOverlapHeadword) {
  const auto layout = dictionary_ui::definitionHeaderLayout(0, 480, 20, 27, 8);
  EXPECT_EQ(layout.titleX, 20);
  EXPECT_EQ(layout.counterX, 433);
  EXPECT_EQ(layout.titleWidth, 405);
  EXPECT_EQ(layout.titleX + layout.titleWidth + 8, layout.counterX);
}

TEST(DictionaryUiLayout, DegenerateBandNeverProducesNegativeWidth) {
  const auto layout = dictionary_ui::definitionHeaderLayout(10, 24, 20, 40, 8);
  EXPECT_EQ(layout.titleX, 30);
  EXPECT_EQ(layout.titleWidth, 0);
  EXPECT_EQ(layout.counterX, 30);
}
