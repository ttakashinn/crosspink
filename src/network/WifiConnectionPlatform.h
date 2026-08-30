#pragma once

#include <WiFi.h>

#include <cstdint>

#include "WifiConnectionPolicy.h"

namespace wifi_connection_platform {

inline const char* statusName(const wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "IDLE";
    case WL_NO_SSID_AVAIL:
      return "NO_SSID_AVAIL";
    case WL_CONNECTED:
      return "CONNECTED";
    case WL_CONNECT_FAILED:
      return "CONNECT_FAILED";
#if !defined(SIMULATOR)
    case WL_CONNECTION_LOST:
      return "CONNECTION_LOST";
#endif
    case WL_DISCONNECTED:
      return "DISCONNECTED";
#if !defined(SIMULATOR)
    case WL_STOPPED:
      return "STOPPED";
#endif
    default:
      return "OTHER";
  }
}

inline wifi_connection_policy::LinkStatus policyStatus(const wl_status_t status) {
  using wifi_connection_policy::LinkStatus;
  switch (status) {
    case WL_IDLE_STATUS:
      return LinkStatus::IDLE;
    case WL_NO_SSID_AVAIL:
      return LinkStatus::NO_SSID;
    case WL_CONNECTED:
      return LinkStatus::CONNECTED;
    case WL_CONNECT_FAILED:
      return LinkStatus::CONNECT_FAILED;
#if !defined(SIMULATOR)
    case WL_CONNECTION_LOST:
      return LinkStatus::CONNECTION_LOST;
#endif
    case WL_DISCONNECTED:
      return LinkStatus::DISCONNECTED;
#if !defined(SIMULATOR)
    case WL_STOPPED:
      return LinkStatus::STOPPED;
#endif
    default:
      return LinkStatus::OTHER;
  }
}

inline bool enterStationMode() {
#if defined(SIMULATOR)
  WiFi.mode(WIFI_STA);
  return true;
#else
  return WiFi.mode(WIFI_STA);
#endif
}

inline bool disconnectForRetry(const uint32_t timeoutMs) {
#if defined(SIMULATOR)
  static_cast<void>(timeoutMs);
  WiFi.disconnect(false, false);
  return true;
#else
  return WiFi.disconnect(false, false, timeoutMs);
#endif
}

inline bool disablePowerSave() {
#if defined(SIMULATOR)
  WiFi.setSleep(false);
  return true;
#else
  return WiFi.setSleep(false);
#endif
}

inline bool beginAccepted(const bool stationModeReady, const wl_status_t status) {
#if defined(SIMULATOR)
  return stationModeReady && status != WL_CONNECT_FAILED;
#else
  return stationModeReady && status != WL_CONNECT_FAILED && status != WL_STOPPED;
#endif
}

}  // namespace wifi_connection_platform
