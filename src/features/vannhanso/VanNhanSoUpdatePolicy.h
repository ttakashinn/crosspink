#pragma once

#include <cstdint>

namespace vannhanso_update_policy {

inline constexpr uint32_t SUCCESS_AUTO_CLOSE_DELAY_MS = 1200;

enum class UpdateTrigger : uint8_t {
  MANUAL = 0,
  FIRST_START_OF_DAY = 1,
};

// The first-start trigger may skip an already-current cache, but any required
// network work is interactive: the user sees Wi-Fi selection and update status.
bool isDailyInteractive(UpdateTrigger trigger);
bool maySkipCurrentCache(UpdateTrigger trigger);

// The daily startup refresh trusts a valid profile image whose durable date
// marker matches today's wall-clock date. Manual refresh always reaches the
// server, and an unavailable/different date never suppresses the daily check.
bool shouldSkipCurrentCache(UpdateTrigger trigger, bool cacheMatchesCurrentDate, uint32_t currentDate);

// A profile date is written only from a validated server manifest. Once a
// newer date is installed, a stale response must never move its marker back.
bool isManifestDateOlderThanCache(uint32_t manifestDate, uint32_t cachedDate);

uint32_t pendingProfileHash(bool hasCurrentProfileImage, uint32_t currentProfileHash);

bool isBackoffActive(uint32_t currentProfileHash, uint32_t failureProfileHash, bool lastAttemptFailed,
                     uint8_t consecutiveFailures, uint32_t currentDate, uint16_t currentMinute,
                     uint32_t lastAttemptDate, uint16_t lastAttemptMinute);

// A device without trustworthy wall-clock time cannot measure a minute-based
// backoff across deep sleep. Persist a small number of daily triggers to
// skip instead, so an unavailable network does not delay every wake while a
// later trigger can prompt the user again.
uint8_t automaticRetrySkipsAfterFailure(uint8_t consecutiveFailures);
bool shouldSkipAutomaticRetry(uint32_t currentProfileHash, uint32_t failureProfileHash, bool lastAttemptFailed,
                              uint8_t skipsRemaining);

// Start the delay only after the success screen has physically finished
// rendering. Unsigned subtraction keeps the comparison safe across millis()
// rollover.
bool shouldAutoCloseSuccess(bool successScreenRendered, uint32_t successRenderedAtMs, uint32_t nowMs);

}  // namespace vannhanso_update_policy
