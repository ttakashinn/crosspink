#include <gtest/gtest.h>

#include "features/vannhanso/VanNhanSoUpdatePolicy.h"

namespace policy = vannhanso_update_policy;

TEST(VanNhanSoUpdatePolicy, DailyTriggerUsesInteractiveNetworkFlow) {
  EXPECT_FALSE(policy::isDailyInteractive(policy::UpdateTrigger::MANUAL));
  EXPECT_TRUE(policy::isDailyInteractive(policy::UpdateTrigger::FIRST_START_OF_DAY));
}

TEST(VanNhanSoUpdatePolicy, OnlyDailyUpdateMayTrustTheCurrentCache) {
  EXPECT_FALSE(policy::maySkipCurrentCache(policy::UpdateTrigger::MANUAL));
  EXPECT_TRUE(policy::maySkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY));
}

TEST(VanNhanSoUpdatePolicy, DailyUpdateSkipsAValidCacheForTheCurrentDateWithoutMinuteGates) {
  EXPECT_TRUE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY, true, 20260829U));
  EXPECT_FALSE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY, false, 20260829U));
  EXPECT_FALSE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY, true, 0U));
}

TEST(VanNhanSoUpdatePolicy, ManualModeAlwaysChecksTheManifest) {
  EXPECT_FALSE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::MANUAL, true, 20260829U));
}

TEST(VanNhanSoUpdatePolicy, RejectsOnlyManifestDatesOlderThanTheInstalledServerDate) {
  EXPECT_TRUE(policy::isManifestDateOlderThanCache(20260828U, 20260829U));
  EXPECT_FALSE(policy::isManifestDateOlderThanCache(20260829U, 20260829U));
  EXPECT_FALSE(policy::isManifestDateOlderThanCache(20260830U, 20260829U));
  EXPECT_FALSE(policy::isManifestDateOlderThanCache(0U, 20260829U));
  EXPECT_FALSE(policy::isManifestDateOlderThanCache(20260829U, 0U));
}

TEST(VanNhanSoUpdatePolicy, MarksOnlyMissingCurrentProfileAsPending) {
  EXPECT_EQ(policy::pendingProfileHash(false, 0x12345678U), 0x12345678U);
  EXPECT_EQ(policy::pendingProfileHash(true, 0x12345678U), 0U);
  EXPECT_EQ(policy::pendingProfileHash(false, 0U), 0U);
}

TEST(VanNhanSoUpdatePolicy, FailureFromAnotherProfileDoesNotDelayCurrentProfile) {
  EXPECT_FALSE(policy::isBackoffActive(0x2222U, 0x1111U, true, 3, 20260828U, 600U, 20260828U, 599U));
}

TEST(VanNhanSoUpdatePolicy, SameProfileUsesProgressiveBackoff) {
  EXPECT_TRUE(policy::isBackoffActive(0x1111U, 0x1111U, true, 1, 20260828U, 604U, 20260828U, 600U));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, 1, 20260828U, 605U, 20260828U, 600U));
  EXPECT_TRUE(policy::isBackoffActive(0x1111U, 0x1111U, true, 2, 20260828U, 629U, 20260828U, 600U));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, 2, 20260828U, 630U, 20260828U, 600U));
}

TEST(VanNhanSoUpdatePolicy, RepeatedFailuresRemainRecoverableAfterThreeHours) {
  EXPECT_TRUE(policy::isBackoffActive(0x1111U, 0x1111U, true, 3, 20260828U, 189U, 20260828U, 10U));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, 3, 20260828U, 190U, 20260828U, 10U));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, 4, 20260828U, 190U, 20260828U, 10U));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, 3, 20260829U, 1U, 20260828U, 10U));
}

TEST(VanNhanSoUpdatePolicy, MissingClockNeverPermanentlySuppressesAutomaticRecovery) {
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, 1, 0U, UINT16_MAX, 0U, UINT16_MAX));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, false, 1, 0U, UINT16_MAX, 0U, UINT16_MAX));
}

TEST(VanNhanSoUpdatePolicy, MissingClockUsesBoundedTriggerBackoff) {
  EXPECT_EQ(policy::automaticRetrySkipsAfterFailure(0), 0);
  EXPECT_EQ(policy::automaticRetrySkipsAfterFailure(1), 1);
  EXPECT_EQ(policy::automaticRetrySkipsAfterFailure(2), 3);
  EXPECT_EQ(policy::automaticRetrySkipsAfterFailure(3), 7);
  EXPECT_EQ(policy::automaticRetrySkipsAfterFailure(4), 7);

  EXPECT_TRUE(policy::shouldSkipAutomaticRetry(0x1111U, 0x1111U, true, 1));
  EXPECT_FALSE(policy::shouldSkipAutomaticRetry(0x1111U, 0x1111U, true, 0));
  EXPECT_FALSE(policy::shouldSkipAutomaticRetry(0x2222U, 0x1111U, true, 7));
  EXPECT_FALSE(policy::shouldSkipAutomaticRetry(0x1111U, 0x1111U, false, 7));
}

TEST(VanNhanSoUpdatePolicy, SuccessfulUpdateClosesOnlyAfterItsRenderedHoldTime) {
  constexpr uint32_t renderedAt = 5000U;
  EXPECT_FALSE(policy::shouldAutoCloseSuccess(false, renderedAt, renderedAt + 5000U));
  EXPECT_FALSE(policy::shouldAutoCloseSuccess(true, renderedAt, renderedAt + policy::SUCCESS_AUTO_CLOSE_DELAY_MS - 1U));
  EXPECT_TRUE(policy::shouldAutoCloseSuccess(true, renderedAt, renderedAt + policy::SUCCESS_AUTO_CLOSE_DELAY_MS));
}

TEST(VanNhanSoUpdatePolicy, SuccessfulUpdateAutoCloseSurvivesMillisRollover) {
  constexpr uint32_t renderedAt = UINT32_MAX - 500U;
  const uint32_t beforeDeadline = renderedAt + policy::SUCCESS_AUTO_CLOSE_DELAY_MS - 1U;
  const uint32_t atDeadline = renderedAt + policy::SUCCESS_AUTO_CLOSE_DELAY_MS;
  EXPECT_FALSE(policy::shouldAutoCloseSuccess(true, renderedAt, beforeDeadline));
  EXPECT_TRUE(policy::shouldAutoCloseSuccess(true, renderedAt, atDeadline));
}
