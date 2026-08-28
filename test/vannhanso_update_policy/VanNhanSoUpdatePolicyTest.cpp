#include <gtest/gtest.h>

#include "features/vannhanso/VanNhanSoUpdatePolicy.h"

namespace policy = vannhanso_update_policy;

TEST(VanNhanSoUpdatePolicy, MarksOnlyMissingCurrentProfileAsPending) {
  EXPECT_EQ(policy::pendingProfileHash(false, 0x12345678U), 0x12345678U);
  EXPECT_EQ(policy::pendingProfileHash(true, 0x12345678U), 0U);
  EXPECT_EQ(policy::pendingProfileHash(false, 0U), 0U);
}

TEST(VanNhanSoUpdatePolicy, FailureFromAnotherProfileDoesNotDelayCurrentProfile) {
  EXPECT_FALSE(policy::isBackoffActive(0x2222U, 0x1111U, true, false, 3, 20260828U, 600U, 20260828U, 599U));
}

TEST(VanNhanSoUpdatePolicy, SameProfileUsesProgressiveBackoff) {
  EXPECT_TRUE(policy::isBackoffActive(0x1111U, 0x1111U, true, false, 1, 20260828U, 604U, 20260828U, 600U));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, false, 1, 20260828U, 605U, 20260828U, 600U));
  EXPECT_TRUE(policy::isBackoffActive(0x1111U, 0x1111U, true, false, 2, 20260828U, 629U, 20260828U, 600U));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, false, 2, 20260828U, 630U, 20260828U, 600U));
}

TEST(VanNhanSoUpdatePolicy, ThirdSameDayFailureStopsAutomaticRetries) {
  EXPECT_TRUE(policy::isBackoffActive(0x1111U, 0x1111U, true, false, 3, 20260828U, 1400U, 20260828U, 10U));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, false, 3, 20260829U, 1U, 20260828U, 10U));
}

TEST(VanNhanSoUpdatePolicy, MissingClockAllowsOnlyMissingProfileToRetryOnNextWake) {
  EXPECT_TRUE(policy::isBackoffActive(0x1111U, 0x1111U, true, false, 1, 0U, UINT16_MAX, 0U, UINT16_MAX));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, true, true, 1, 0U, UINT16_MAX, 0U, UINT16_MAX));
  EXPECT_FALSE(policy::isBackoffActive(0x1111U, 0x1111U, false, false, 1, 0U, UINT16_MAX, 0U, UINT16_MAX));
}
