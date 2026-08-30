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

TEST(ReaderRenderSpecTest, SimplifiedModeKeepsImagesAndPublisherCss) {
  ReaderRenderSpec requested;
  requested.embeddedStyle = true;
  requested.imageRendering = 0;

  const ReaderRenderSpec actual = applyEpubRenderMode(requested, EpubRenderMode::Simplified);

  EXPECT_TRUE(actual.embeddedStyle);
  EXPECT_EQ(0, actual.imageRendering);
  EXPECT_EQ(EpubRenderMode::Simplified, actual.renderMode);
  EXPECT_STREQ("simplified", epubRenderModeName(actual.renderMode));
  EXPECT_EQ(EpubRenderMode::Safe, nextLighterEpubRenderMode(actual.renderMode));
}

TEST(ReaderRenderSpecTest, RemembersFallbackOnlyAfterSuccessfulPageRender) {
  EXPECT_FALSE(
      shouldRememberEpubFallback(static_cast<uint8_t>(EpubRenderMode::Standard), EpubRenderMode::Simplified, false));
  EXPECT_TRUE(
      shouldRememberEpubFallback(static_cast<uint8_t>(EpubRenderMode::Standard), EpubRenderMode::Simplified, true));
  EXPECT_FALSE(
      shouldRememberEpubFallback(static_cast<uint8_t>(EpubRenderMode::Simplified), EpubRenderMode::Simplified, true));
}
