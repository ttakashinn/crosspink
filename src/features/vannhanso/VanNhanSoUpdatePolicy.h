#pragma once

#include <cstdint>

namespace vannhanso_update_policy {

uint32_t pendingProfileHash(bool hasCurrentProfileImage, uint32_t currentProfileHash);

bool isBackoffActive(uint32_t currentProfileHash, uint32_t failureProfileHash, bool lastAttemptFailed,
                     bool currentProfileMissing, uint8_t consecutiveFailures, uint32_t currentDate,
                     uint16_t currentMinute, uint32_t lastAttemptDate, uint16_t lastAttemptMinute);

}  // namespace vannhanso_update_policy
