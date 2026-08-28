#include <Epub/EpubRenderMode.h>
#include <Epub/ReaderRenderSpec.h>
#include <gtest/gtest.h>

TEST(ReaderRenderSpecTest, StandardModePreservesRequestedFeatures) {
  ReaderRenderSpec requested;
  requested.embeddedStyle = true;
  requested.imageRendering = 2;

  const ReaderRenderSpec actual = applyEpubRenderMode(requested, EpubRenderMode::Standard);

  EXPECT_TRUE(actual.embeddedStyle);
  EXPECT_EQ(2, actual.imageRendering);
  EXPECT_EQ(EpubRenderMode::Standard, actual.renderMode);
  EXPECT_STREQ("standard", epubRenderModeName(actual.renderMode));
}

TEST(ReaderRenderSpecTest, SafeModeDisablesHeavyOptionalLayoutInputs) {
  ReaderRenderSpec requested;
  requested.embeddedStyle = true;
  requested.imageRendering = 0;

  const ReaderRenderSpec actual = applyEpubRenderMode(requested, EpubRenderMode::Safe);

  EXPECT_FALSE(actual.embeddedStyle);
  EXPECT_EQ(1, actual.imageRendering);
  EXPECT_EQ(EpubRenderMode::Safe, actual.renderMode);
  EXPECT_STREQ("safe", epubRenderModeName(actual.renderMode));
}
