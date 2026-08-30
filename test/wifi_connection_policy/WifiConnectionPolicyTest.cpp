#include <gtest/gtest.h>

#include "network/WifiConnectionPolicy.h"

namespace policy = wifi_connection_policy;

TEST(WifiConnectionPolicy, AllowsFrameworkRetriesBeforeDeadline) {
  for (const auto status :
       {policy::LinkStatus::IDLE, policy::LinkStatus::DISCONNECTED, policy::LinkStatus::CONNECTION_LOST}) {
    const policy::Snapshot snapshot{status, policy::FailureHint::TRANSIENT, true, 14999, 15000};
    EXPECT_EQ(policy::evaluate(snapshot), policy::Outcome::PENDING);
  }
}

TEST(WifiConnectionPolicy, ReportsConnectedBeforeConsideringOldFailureHint) {
  const policy::Snapshot snapshot{policy::LinkStatus::CONNECTED, policy::FailureHint::AUTHENTICATION, true, 3000,
                                  15000};
  EXPECT_EQ(policy::evaluate(snapshot), policy::Outcome::CONNECTED);
}

TEST(WifiConnectionPolicy, RejectsBeginOrStoppedRadioImmediately) {
  EXPECT_EQ(policy::evaluate({policy::LinkStatus::DISCONNECTED, policy::FailureHint::NONE, false, 0, 25000}),
            policy::Outcome::RADIO_FAILED);
  EXPECT_EQ(policy::evaluate({policy::LinkStatus::STOPPED, policy::FailureHint::NONE, true, 0, 25000}),
            policy::Outcome::RADIO_FAILED);
}

TEST(WifiConnectionPolicy, PreservesUsefulTerminalErrors) {
  EXPECT_EQ(policy::evaluate({policy::LinkStatus::NO_SSID, policy::FailureHint::NO_ACCESS_POINT, true, 1000, 25000}),
            policy::Outcome::NETWORK_NOT_FOUND);
  EXPECT_EQ(
      policy::evaluate({policy::LinkStatus::CONNECT_FAILED, policy::FailureHint::AUTHENTICATION, true, 1000, 25000}),
      policy::Outcome::AUTHENTICATION_FAILED);
  EXPECT_EQ(policy::evaluate({policy::LinkStatus::CONNECT_FAILED, policy::FailureHint::TRANSIENT, true, 1000, 25000}),
            policy::Outcome::CONNECTION_FAILED);
}

TEST(WifiConnectionPolicy, ClassifiesExpiredRetriesByBestAvailableEvidence) {
  EXPECT_EQ(
      policy::evaluate({policy::LinkStatus::DISCONNECTED, policy::FailureHint::NO_ACCESS_POINT, true, 25000, 25000}),
      policy::Outcome::NETWORK_NOT_FOUND);
  EXPECT_EQ(
      policy::evaluate({policy::LinkStatus::CONNECTION_LOST, policy::FailureHint::AUTHENTICATION, true, 25000, 25000}),
      policy::Outcome::AUTHENTICATION_FAILED);
  EXPECT_EQ(policy::evaluate({policy::LinkStatus::CONNECTION_LOST, policy::FailureHint::TRANSIENT, true, 25000, 25000}),
            policy::Outcome::TIMED_OUT);
}
