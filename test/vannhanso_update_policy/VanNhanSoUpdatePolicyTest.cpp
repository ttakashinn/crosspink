#include <gtest/gtest.h>

#include "features/vannhanso/VanNhanSoUpdatePolicy.h"

namespace policy = vannhanso_update_policy;

TEST(VanNhanSoUpdatePolicy, GivesEachUpdateModeDistinctCacheAndSleepSemantics) {
  EXPECT_FALSE(policy::isAutomatic(policy::UpdateTrigger::MANUAL));
  EXPECT_FALSE(policy::maySkipCurrentCache(policy::UpdateTrigger::MANUAL));
  EXPECT_FALSE(policy::shouldSleepAfterUpdate(policy::UpdateTrigger::MANUAL));

  EXPECT_TRUE(policy::isAutomatic(policy::UpdateTrigger::FIRST_START_OF_DAY));
  EXPECT_TRUE(policy::maySkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY));
  EXPECT_FALSE(policy::shouldSleepAfterUpdate(policy::UpdateTrigger::FIRST_START_OF_DAY));

  EXPECT_TRUE(policy::isAutomatic(policy::UpdateTrigger::ENTERING_SLEEP));
  EXPECT_TRUE(policy::maySkipCurrentCache(policy::UpdateTrigger::ENTERING_SLEEP));
  EXPECT_TRUE(policy::shouldSleepAfterUpdate(policy::UpdateTrigger::ENTERING_SLEEP));
}

TEST(VanNhanSoUpdatePolicy, AutomaticModesSkipAProfileAlreadyConfirmedForTheCurrentDate) {
  EXPECT_TRUE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY, true, 20260829U, 601U,
                                             20260829U, 600U));
  EXPECT_FALSE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY, true, 20260829U, 600U,
                                              20260829U, 600U));
  EXPECT_FALSE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::ENTERING_SLEEP, true, 20260829U, UINT16_MAX,
                                              20260829U, UINT16_MAX));
  EXPECT_FALSE(
      policy::shouldSkipCurrentCache(policy::UpdateTrigger::ENTERING_SLEEP, true, 20260829U, 599U, 20260829U, 600U));
  EXPECT_FALSE(
      policy::shouldSkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY, true, 20260829U, 601U, 0U, UINT16_MAX));
  EXPECT_FALSE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::FIRST_START_OF_DAY, false, 20260829U, 601U,
                                              20260829U, 600U));
}

TEST(VanNhanSoUpdatePolicy, ManualModeAlwaysChecksTheManifest) {
  EXPECT_FALSE(policy::shouldSkipCurrentCache(policy::UpdateTrigger::MANUAL, true, 20260829U, 601U, 20260829U, 600U));
}

TEST(VanNhanSoUpdatePolicy, TriggeringPowerButtonDoesNotCancelAutomaticUpdate) {
  EXPECT_FALSE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::ENTERING_SLEEP, false, false, true, true,
                                                   false, true));
  EXPECT_TRUE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::ENTERING_SLEEP, false, false, true, true,
                                                  false, false));
  EXPECT_TRUE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::ENTERING_SLEEP, false, false, true, false,
                                                  false, false));
  EXPECT_TRUE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::ENTERING_SLEEP, false, false, false, false,
                                                  true, false));
  EXPECT_TRUE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::ENTERING_SLEEP, false, true, false, false,
                                                  false, true));
}

TEST(VanNhanSoUpdatePolicy, MissingProfileRefreshStillYieldsToNormalUse) {
  EXPECT_TRUE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::FIRST_START_OF_DAY, true, false, true, false,
                                                  true, false));
  EXPECT_TRUE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::FIRST_START_OF_DAY, true, true, false, false,
                                                  false, false));
}

TEST(VanNhanSoUpdatePolicy, SleepCanBeCancelledEvenWhenTheCurrentProfileHasNoImageYet) {
  EXPECT_TRUE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::ENTERING_SLEEP, true, false, true, false,
                                                  false, false));
  EXPECT_TRUE(policy::shouldCancelAutomaticUpdate(policy::UpdateTrigger::ENTERING_SLEEP, true, false, false, false,
                                                  true, false));
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
