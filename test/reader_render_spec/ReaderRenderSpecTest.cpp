#include <Epub/EpubBuildRecovery.h>
#include <Epub/EpubRenderMode.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/SectionBuildFailure.h>
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

TEST(ReaderRenderSpecTest, UsesProgressivelyLowerHeapFloorsForFallbackModes) {
  const auto standard = epubLayoutHeapFloor(EpubRenderMode::Standard);
  const auto simplified = epubLayoutHeapFloor(EpubRenderMode::Simplified);
  const auto safe = epubLayoutHeapFloor(EpubRenderMode::Safe);

  EXPECT_GT(standard.minFreeHeap, simplified.minFreeHeap);
  EXPECT_GT(simplified.minFreeHeap, safe.minFreeHeap);
  EXPECT_GT(standard.minMaxAlloc, simplified.minMaxAlloc);
  EXPECT_GT(simplified.minMaxAlloc, safe.minMaxAlloc);

  EXPECT_FALSE(epubLayoutHeapSufficient(EpubRenderMode::Standard, 39 * 1024, 27 * 1024));
  EXPECT_FALSE(epubLayoutHeapSufficient(EpubRenderMode::Simplified, 39 * 1024, 27 * 1024));
  EXPECT_TRUE(epubLayoutHeapSufficient(EpubRenderMode::Safe, 39 * 1024, 27 * 1024));
  EXPECT_FALSE(epubLayoutHeapSufficient(EpubRenderMode::Safe, 35 * 1024, 23 * 1024));
}

TEST(ReaderRenderSpecTest, KeepsReadableCacheForTransientBuildFailures) {
  EXPECT_TRUE(shouldPreserveSectionCache(SectionBuildFailure::LowMemory));
  EXPECT_TRUE(shouldPreserveSectionCache(SectionBuildFailure::Io));
  EXPECT_TRUE(shouldPreserveSectionCache(SectionBuildFailure::InvalidContent));
  EXPECT_FALSE(shouldPreserveSectionCache(SectionBuildFailure::None));
}

TEST(ReaderRenderSpecTest, SuspendsOnlyUsefulLowMemoryBuilds) {
  EXPECT_TRUE(shouldSuspendFailedSectionBuild(SectionBuildFailure::LowMemory, 1, false));
  EXPECT_TRUE(shouldSuspendFailedSectionBuild(SectionBuildFailure::LowMemory, 0, true));
  EXPECT_FALSE(shouldSuspendFailedSectionBuild(SectionBuildFailure::LowMemory, 0, false));
  EXPECT_FALSE(shouldSuspendFailedSectionBuild(SectionBuildFailure::Io, 3, false));
  EXPECT_FALSE(shouldSuspendFailedSectionBuild(SectionBuildFailure::InvalidContent, 3, true));
}

TEST(ReaderRenderSpecTest, PreferredQualityTrialRequiresAnUnchangedWorkingFallback) {
  EXPECT_TRUE(
      shouldStartPreferredRenderTrial(static_cast<uint8_t>(EpubRenderMode::Standard), EpubRenderMode::Safe, false));
  EXPECT_FALSE(
      shouldStartPreferredRenderTrial(static_cast<uint8_t>(EpubRenderMode::Standard), EpubRenderMode::Standard, false));
  EXPECT_FALSE(
      shouldStartPreferredRenderTrial(static_cast<uint8_t>(EpubRenderMode::Standard), EpubRenderMode::Safe, true));
}

TEST(ReaderRenderSpecTest, RestartsOnlyOnceWhenSafeModeAlsoRunsOutOfMemory) {
  EXPECT_EQ(EpubBuildRecovery::RetryLighterMode,
            epubBuildRecoveryFor(SectionBuildFailure::LowMemory, EpubRenderMode::Standard, false));
  EXPECT_EQ(EpubBuildRecovery::RetryLighterMode,
            epubBuildRecoveryFor(SectionBuildFailure::LowMemory, EpubRenderMode::Simplified, false));
  EXPECT_EQ(EpubBuildRecovery::RestartReaderOnce,
            epubBuildRecoveryFor(SectionBuildFailure::LowMemory, EpubRenderMode::Safe, false));
  EXPECT_EQ(EpubBuildRecovery::None, epubBuildRecoveryFor(SectionBuildFailure::LowMemory, EpubRenderMode::Safe, true));
  EXPECT_EQ(EpubBuildRecovery::None, epubBuildRecoveryFor(SectionBuildFailure::Io, EpubRenderMode::Standard, false));
}

TEST(ReaderRenderSpecTest, PausesSpeculativeBuildOnlyWhenLowMemoryLeavesReadablePages) {
  EXPECT_TRUE(shouldPauseEpubBackgroundBuild(SectionBuildFailure::LowMemory, true, 1));
  EXPECT_FALSE(shouldPauseEpubBackgroundBuild(SectionBuildFailure::LowMemory, false, 1));
  EXPECT_FALSE(shouldPauseEpubBackgroundBuild(SectionBuildFailure::LowMemory, true, 0));
  EXPECT_FALSE(shouldPauseEpubBackgroundBuild(SectionBuildFailure::Io, true, 4));
}
