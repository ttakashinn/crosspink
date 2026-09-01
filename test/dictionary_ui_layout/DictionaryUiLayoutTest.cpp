#include <gtest/gtest.h>

#include "DictionaryTypography.h"
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

TEST(DictionaryTypography, UsesStableBuiltinFacesInsteadOfTheBookFont) {
  EXPECT_EQ(dictionary_typography::bodyFontId(8), NOTOSANS_12_FONT_ID);
  EXPECT_EQ(dictionary_typography::bodyFontId(14), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(dictionary_typography::bodyFontId(16), NOTOSANS_16_FONT_ID);
  EXPECT_EQ(dictionary_typography::bodyFontId(40), NOTOSANS_18_FONT_ID);
}

TEST(DictionaryTypography, UsesNotoSansLineCompression) {
  EXPECT_FLOAT_EQ(dictionary_typography::lineCompression(0), 0.90f);
  EXPECT_FLOAT_EQ(dictionary_typography::lineCompression(1), 0.95f);
  EXPECT_FLOAT_EQ(dictionary_typography::lineCompression(2), 1.0f);
  EXPECT_FLOAT_EQ(dictionary_typography::lineCompression(3), 1.05f);
}
