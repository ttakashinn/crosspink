#include <gtest/gtest.h>

#include "network/OtaUpdatePolicy.h"

namespace policy = ota_update_policy;

TEST(OtaUpdatePolicy, RetriesOneTransientNetworkFailureOnly) {
  EXPECT_TRUE(policy::hasAnotherHttpAttempt(1));
  EXPECT_FALSE(policy::hasAnotherHttpAttempt(2));
}

TEST(OtaUpdatePolicy, ThrottlesProgressButPreservesCompletionAndRetryReset) {
  EXPECT_TRUE(policy::shouldPublishProgress(-1, 0));
  EXPECT_FALSE(policy::shouldPublishProgress(0, 1));
  EXPECT_FALSE(policy::shouldPublishProgress(0, 4));
  EXPECT_TRUE(policy::shouldPublishProgress(0, 5));
  EXPECT_TRUE(policy::shouldPublishProgress(95, 100));
  EXPECT_TRUE(policy::shouldPublishProgress(35, 0));
}
