#pragma once

#include <cstdint>

// Keep the default int underlying type: ActivityResult forward-declares this
// enum and existing firmware code historically used that ABI.
enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, USB_DRIVE };

// Only WiFi-backed File Transfer modes may be resumed from the RTC boot
// payload. USB Drive owns a separate storage-handoff lifecycle and must never
// be entered through the network boot path.
constexpr bool isFileTransferNetworkMode(const NetworkMode mode) {
  switch (mode) {
    case NetworkMode::JOIN_NETWORK:
    case NetworkMode::CONNECT_CALIBRE:
    case NetworkMode::CREATE_HOTSPOT:
      return true;
    case NetworkMode::USB_DRIVE:
      return false;
  }
  return false;
}

constexpr uint32_t encodeFileTransferNetworkMode(const NetworkMode mode) { return static_cast<uint32_t>(mode); }

constexpr bool decodeFileTransferNetworkMode(const uint32_t payload, NetworkMode& mode) {
  if (payload > static_cast<uint32_t>(NetworkMode::CREATE_HOTSPOT)) return false;
  mode = static_cast<NetworkMode>(payload);
  return isFileTransferNetworkMode(mode);
}
