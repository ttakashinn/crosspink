#include <gtest/gtest.h>

#include "network/WebServerLoopPolicy.h"

namespace policy = web_server_loop_policy;

TEST(WebServerLoopPolicy, BoundsIdlePollingBurst) {
  EXPECT_EQ(policy::iterations(false, 500), policy::IDLE_ITERATIONS);
  EXPECT_LE(policy::IDLE_ITERATIONS, 12);
}

TEST(WebServerLoopPolicy, PreservesTransferThroughputBurst) {
  EXPECT_EQ(policy::iterations(true, 500), 500);
  EXPECT_EQ(policy::iterations(true, 80), 80);
}

TEST(WebServerLoopPolicy, SkipsOuterDelayOnlyDuringActiveTransfer) {
  EXPECT_FALSE(policy::skipMainLoopDelay(false, false));
  EXPECT_FALSE(policy::skipMainLoopDelay(true, false));
  EXPECT_FALSE(policy::skipMainLoopDelay(false, true));
  EXPECT_TRUE(policy::skipMainLoopDelay(true, true));
}
