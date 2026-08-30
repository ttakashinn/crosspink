#include "WifiConnectionDiagnostics.h"

#if defined(SIMULATOR)

namespace wifi_connection_diagnostics {

void beginAttempt() {}
void endAttempt() {}
wifi_connection_policy::FailureHint failureHint() { return wifi_connection_policy::FailureHint::NONE; }

}  // namespace wifi_connection_diagnostics

#else

#include <Logging.h>
#include <WiFi.h>

#include <atomic>

namespace wifi_connection_diagnostics {
namespace {

std::atomic<bool> active{false};
std::atomic<wifi_connection_policy::FailureHint> lastFailureHint{wifi_connection_policy::FailureHint::NONE};
bool initialized = false;

wifi_connection_policy::FailureHint hintForReason(const uint8_t reason) {
  using wifi_connection_policy::FailureHint;
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
      return FailureHint::NO_ACCESS_POINT;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
      return FailureHint::AUTHENTICATION;
    default:
      return FailureHint::TRANSIENT;
  }
}

void onWifiEvent(const WiFiEvent_t event, const WiFiEventInfo_t info) {
  if (!active.load(std::memory_order_relaxed)) return;
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      LOG_INF("WIFI", "STA associated with access point");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      LOG_INF("WIFI", "STA received an IP address");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      const uint8_t reason =
          info.wifi_sta_disconnected.reason == 0 ? WIFI_REASON_UNSPECIFIED : info.wifi_sta_disconnected.reason;
      lastFailureHint.store(hintForReason(reason), std::memory_order_relaxed);
      LOG_INF("WIFI", "STA disconnected reason=%u(%s)", reason,
              WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      LOG_INF("WIFI", "STA lost its IP address");
      break;
    default:
      break;
  }
}

void initialize() {
  if (initialized) return;
  WiFi.onEvent(&onWifiEvent);
  initialized = true;
}

}  // namespace

void beginAttempt() {
  initialize();
  lastFailureHint.store(wifi_connection_policy::FailureHint::NONE, std::memory_order_relaxed);
  active.store(true, std::memory_order_relaxed);
}

void endAttempt() { active.store(false, std::memory_order_relaxed); }

wifi_connection_policy::FailureHint failureHint() { return lastFailureHint.load(std::memory_order_relaxed); }

}  // namespace wifi_connection_diagnostics

#endif
