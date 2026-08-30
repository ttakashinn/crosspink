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

// Automatic modes may trust a matching cache only after the wall clock has
// advanced beyond the minute when the server confirmed it. A frozen, invalid,
// or backwards clock must not keep suppressing refreshes. Manual refresh always
// reaches the server.
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

// A device without trustworthy wall-clock time cannot measure a minute-based
// backoff across deep sleep. Persist a small number of automatic triggers to
// skip instead, so an unavailable network does not delay every sleep while a
// later trigger can still recover without user intervention.
uint8_t automaticRetrySkipsAfterFailure(uint8_t consecutiveFailures);
bool shouldSkipAutomaticRetry(uint32_t currentProfileHash, uint32_t failureProfileHash, bool lastAttemptFailed,
                              uint8_t skipsRemaining);

}  // namespace vannhanso_update_policy
