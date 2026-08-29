#include "VanNhanSoUpdatePolicy.h"

#include <algorithm>
#include <iterator>

namespace vannhanso_update_policy {

bool isAutomatic(const UpdateTrigger trigger) { return trigger != UpdateTrigger::MANUAL; }

bool maySkipCurrentCache(const UpdateTrigger trigger) { return trigger == UpdateTrigger::FIRST_START_OF_DAY; }

bool shouldSleepAfterUpdate(const UpdateTrigger trigger) { return trigger == UpdateTrigger::ENTERING_SLEEP; }

bool shouldSkipCurrentCache(const UpdateTrigger trigger, const bool cacheMatchesCurrentDate, const uint32_t currentDate,
                            const uint16_t currentMinute, const uint32_t lastSuccessDate,
                            const uint16_t lastSuccessMinute) {
  if (!maySkipCurrentCache(trigger) || !cacheMatchesCurrentDate || currentDate == 0 || currentMinute >= 24U * 60U) {
    return false;
  }

  // Require evidence that the clock has advanced since the image was last
  // confirmed by the server. Equality is intentionally not enough: it catches
  // a stopped RTC after a battery cut at the cost of one harmless manifest
  // request when the user wakes twice within the same minute.
  return lastSuccessDate == currentDate && lastSuccessMinute < 24U * 60U && currentMinute > lastSuccessMinute;
}

bool shouldCancelAutomaticUpdate(const UpdateTrigger trigger, const bool pendingProfileRequired, const bool backPressed,
                                 const bool anyButtonPressed, const bool powerButtonPressed, const bool screenTapped,
                                 const bool ignoreTriggerPowerButton) {
  if (backPressed) return true;

  // A sleep update runs between a user action and deep sleep. Any new input
  // means the user wants to stay awake. The one exception is the still-held
  // power press that initiated an explicit sleep request.
  if (trigger == UpdateTrigger::ENTERING_SLEEP) {
    if (screenTapped) return true;
    if (!anyButtonPressed) return false;
    return !(ignoreTriggerPowerButton && powerButtonPressed);
  }

  // When no image exists for the selected profile, keep the bounded first-start
  // update modal except for its explicit Back action. Routine background checks
  // still yield immediately to normal interaction.
  if (pendingProfileRequired) return false;
  if (screenTapped) return true;
  if (!anyButtonPressed) return false;
  return !(ignoreTriggerPowerButton && powerButtonPressed);
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
  // backoff across deep-sleep resets. Retrying on the next trigger is safer
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

}  // namespace vannhanso_update_policy
