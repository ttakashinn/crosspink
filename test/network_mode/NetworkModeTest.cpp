#include <gtest/gtest.h>

#include "activities/network/NetworkMode.h"

TEST(NetworkMode, RoundTripsEveryWifiBackedFileTransferMode) {
  for (const auto expected : {NetworkMode::JOIN_NETWORK, NetworkMode::CONNECT_CALIBRE, NetworkMode::CREATE_HOTSPOT}) {
    NetworkMode decoded = NetworkMode::USB_DRIVE;
    EXPECT_TRUE(decodeFileTransferNetworkMode(encodeFileTransferNetworkMode(expected), decoded));
    EXPECT_EQ(decoded, expected);
  }
}

TEST(NetworkMode, RejectsUsbDriveAndCorruptRtcPayloads) {
  NetworkMode decoded = NetworkMode::JOIN_NETWORK;
  EXPECT_FALSE(decodeFileTransferNetworkMode(encodeFileTransferNetworkMode(NetworkMode::USB_DRIVE), decoded));
  EXPECT_FALSE(decodeFileTransferNetworkMode(0xFFFFFFFFU, decoded));
}
