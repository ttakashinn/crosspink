#pragma once

#include <cstdint>

namespace vannhanso_update_policy {

enum class UpdateTrigger : uint8_t {
  MANUAL = 0,
  FIRST_START_OF_DAY = 1,
  ENTERING_SLEEP = 2,
};

bool isAutomatic(UpdateTrigger trigger);
bool maySkipCurrentCache(UpdateTrigger trigger);
bool shouldSleepAfterUpdate(UpdateTrigger trigger);

// The daily mode may trust a matching cache only when the clock has moved
// forward since the last server-confirmed success. A frozen RTC or a restored
// settings file must perform a lightweight manifest check instead of keeping
// yesterday's image indefinitely.
bool shouldSkipCurrentCache(UpdateTrigger trigger, bool cacheMatchesCurrentDate, uint32_t currentDate,
                            uint16_t currentMinute, uint32_t lastSuccessDate, uint16_t lastSuccessMinute);

// Automatic refreshes must yield to real user input, but the power-button
// edge that woke the device or requested sleep is part of the update trigger,
// not a cancellation request.
bool shouldCancelAutomaticUpdate(UpdateTrigger trigger, bool pendingProfileRequired, bool backPressed,
                                 bool anyButtonPressed, bool powerButtonPressed, bool screenTapped,
                                 bool ignoreTriggerPowerButton);

uint32_t pendingProfileHash(bool hasCurrentProfileImage, uint32_t currentProfileHash);

bool isBackoffActive(uint32_t currentProfileHash, uint32_t failureProfileHash, bool lastAttemptFailed,
                     uint8_t consecutiveFailures, uint32_t currentDate, uint16_t currentMinute,
                     uint32_t lastAttemptDate, uint16_t lastAttemptMinute);

}  // namespace vannhanso_update_policy
