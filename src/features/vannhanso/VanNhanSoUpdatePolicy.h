#pragma once

#include <cstdint>

namespace vannhanso_update_policy {

enum class UpdateTrigger : uint8_t {
  MANUAL = 0,
  FIRST_START_OF_DAY = 1,
};

bool isAutomatic(UpdateTrigger trigger);
bool maySkipCurrentCache(UpdateTrigger trigger);

// The daily automatic refresh trusts a valid profile image whose durable date
// marker matches today's wall-clock date. Manual refresh always reaches the
// server, and an unavailable/different date never suppresses the daily check.
bool shouldSkipCurrentCache(UpdateTrigger trigger, bool cacheMatchesCurrentDate, uint32_t currentDate);

// A profile date is written only from a validated server manifest. Once a
// newer date is installed, a stale response must never move its marker back.
bool isManifestDateOlderThanCache(uint32_t manifestDate, uint32_t cachedDate);

// The first-start refresh is opportunistic and must yield to real user input.
bool shouldCancelAutomaticUpdate(bool backPressed, bool anyButtonPressed, bool screenTapped);

uint32_t pendingProfileHash(bool hasCurrentProfileImage, uint32_t currentProfileHash);

bool isBackoffActive(uint32_t currentProfileHash, uint32_t failureProfileHash, bool lastAttemptFailed,
                     uint8_t consecutiveFailures, uint32_t currentDate, uint16_t currentMinute,
                     uint32_t lastAttemptDate, uint16_t lastAttemptMinute);

// A device without trustworthy wall-clock time cannot measure a minute-based
// backoff across deep sleep. Persist a small number of automatic triggers to
// skip instead, so an unavailable network does not delay every wake while a
// later trigger can still recover without user intervention.
uint8_t automaticRetrySkipsAfterFailure(uint8_t consecutiveFailures);
bool shouldSkipAutomaticRetry(uint32_t currentProfileHash, uint32_t failureProfileHash, bool lastAttemptFailed,
                              uint8_t skipsRemaining);

}  // namespace vannhanso_update_policy
