#pragma once

#include <cstdint>

namespace wifi_connection_policy {

enum class LinkStatus : uint8_t {
  IDLE,
  NO_SSID,
  CONNECTED,
  CONNECT_FAILED,
  CONNECTION_LOST,
  DISCONNECTED,
  STOPPED,
  OTHER,
};

enum class FailureHint : uint8_t {
  NONE,
  NO_ACCESS_POINT,
  AUTHENTICATION,
  TRANSIENT,
};

enum class Outcome : uint8_t {
  PENDING,
  CONNECTED,
  NETWORK_NOT_FOUND,
  AUTHENTICATION_FAILED,
  RADIO_FAILED,
  CONNECTION_FAILED,
  TIMED_OUT,
};

struct Snapshot {
  LinkStatus status = LinkStatus::IDLE;
  FailureHint failureHint = FailureHint::NONE;
  bool beginAccepted = true;
  uint32_t elapsedMs = 0;
  uint32_t timeoutMs = 0;
};

// WiFi.status() is intentionally not evaluated directly in the activity. The
// ESP32 framework temporarily reports DISCONNECTED/CONNECTION_LOST while it
// retries WPA association, so only terminal states or an expired deadline end
// an attempt.
Outcome evaluate(const Snapshot& snapshot);

}  // namespace wifi_connection_policy
