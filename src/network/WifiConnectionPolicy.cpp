#include "WifiConnectionPolicy.h"

namespace wifi_connection_policy {

Outcome evaluate(const Snapshot& snapshot) {
  if (snapshot.status == LinkStatus::CONNECTED) {
    return Outcome::CONNECTED;
  }

  if (!snapshot.beginAccepted || snapshot.status == LinkStatus::STOPPED) {
    return Outcome::RADIO_FAILED;
  }

  const bool timedOut = snapshot.timeoutMs != 0 && snapshot.elapsedMs >= snapshot.timeoutMs;
  if (!timedOut) {
    if (snapshot.status == LinkStatus::NO_SSID) {
      return Outcome::NETWORK_NOT_FOUND;
    }
    if (snapshot.status == LinkStatus::CONNECT_FAILED) {
      return snapshot.failureHint == FailureHint::AUTHENTICATION ? Outcome::AUTHENTICATION_FAILED
                                                                 : Outcome::CONNECTION_FAILED;
    }
    return Outcome::PENDING;
  }

  if (snapshot.status == LinkStatus::NO_SSID || snapshot.failureHint == FailureHint::NO_ACCESS_POINT) {
    return Outcome::NETWORK_NOT_FOUND;
  }
  if (snapshot.status == LinkStatus::CONNECT_FAILED || snapshot.failureHint == FailureHint::AUTHENTICATION) {
    return Outcome::AUTHENTICATION_FAILED;
  }
  return Outcome::TIMED_OUT;
}

}  // namespace wifi_connection_policy
