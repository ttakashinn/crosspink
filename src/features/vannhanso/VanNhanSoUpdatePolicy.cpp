#include "VanNhanSoUpdatePolicy.h"

#include <algorithm>
#include <iterator>

namespace vannhanso_update_policy {

bool isDailyInteractive(const UpdateTrigger trigger) { return trigger == UpdateTrigger::FIRST_START_OF_DAY; }

bool maySkipCurrentCache(const UpdateTrigger trigger) { return isDailyInteractive(trigger); }

bool shouldSkipCurrentCache(const UpdateTrigger trigger, const bool cacheMatchesCurrentDate,
                            const uint32_t currentDate) {
  return maySkipCurrentCache(trigger) && cacheMatchesCurrentDate && currentDate != 0;
}

bool isManifestDateOlderThanCache(const uint32_t manifestDate, const uint32_t cachedDate) {
  return manifestDate != 0 && cachedDate != 0 && manifestDate < cachedDate;
}

uint32_t pendingProfileHash(const bool hasCurrentProfileImage, const uint32_t currentProfileHash) {
  return hasCurrentProfileImage ? 0 : currentProfileHash;
}

bool isBackoffActive(const uint32_t currentProfileHash, const uint32_t failureProfileHash, const bool lastAttemptFailed,
                     const uint8_t consecutiveFailures, const uint32_t currentDate, const uint16_t currentMinute,
                     const uint32_t lastAttemptDate, const uint16_t lastAttemptMinute) {
  if (currentProfileHash == 0 || failureProfileHash != currentProfileHash || !lastAttemptFailed ||
      consecutiveFailures == 0) {
    return false;
  }

  // Without a trustworthy clock there is no safe way to measure elapsed
  // backoff across deep-sleep resets. Retrying on a later daily trigger is safer
  // than suppressing the profile forever: the first successful HTTPS response
  // restores the clock, after which the normal bounded backoff applies.
  if (currentDate == 0) return false;
  if (lastAttemptDate != currentDate) return false;
  if (currentMinute >= 24U * 60U || lastAttemptMinute >= 24U * 60U || currentMinute < lastAttemptMinute) {
    return false;
  }

  static constexpr uint16_t RETRY_DELAYS_MINUTES[] = {5, 30, 180};
  const uint8_t delayIndex = std::min<uint8_t>(consecutiveFailures - 1, std::size(RETRY_DELAYS_MINUTES) - 1);
  return currentMinute - lastAttemptMinute < RETRY_DELAYS_MINUTES[delayIndex];
}

uint8_t automaticRetrySkipsAfterFailure(const uint8_t consecutiveFailures) {
  if (consecutiveFailures == 0) return 0;
  static constexpr uint8_t RETRY_SKIP_COUNTS[] = {1, 3, 7};
  const uint8_t index = std::min<uint8_t>(consecutiveFailures - 1, std::size(RETRY_SKIP_COUNTS) - 1);
  return RETRY_SKIP_COUNTS[index];
}

bool shouldSkipAutomaticRetry(const uint32_t currentProfileHash, const uint32_t failureProfileHash,
                              const bool lastAttemptFailed, const uint8_t skipsRemaining) {
  return currentProfileHash != 0 && currentProfileHash == failureProfileHash && lastAttemptFailed && skipsRemaining > 0;
}

bool shouldAutoCloseSuccess(const bool successScreenRendered, const uint32_t successRenderedAtMs,
                            const uint32_t nowMs) {
  return successScreenRendered && nowMs - successRenderedAtMs >= SUCCESS_AUTO_CLOSE_DELAY_MS;
}

}  // namespace vannhanso_update_policy
