#include "VanNhanSoUpdatePolicy.h"

#include <algorithm>
#include <iterator>

namespace vannhanso_update_policy {

uint32_t pendingProfileHash(const bool hasCurrentProfileImage, const uint32_t currentProfileHash) {
  return hasCurrentProfileImage ? 0 : currentProfileHash;
}

bool isBackoffActive(const uint32_t currentProfileHash, const uint32_t failureProfileHash, const bool lastAttemptFailed,
                     const bool currentProfileMissing, const uint8_t consecutiveFailures, const uint32_t currentDate,
                     const uint16_t currentMinute, const uint32_t lastAttemptDate, const uint16_t lastAttemptMinute) {
  if (currentProfileHash == 0 || failureProfileHash != currentProfileHash || !lastAttemptFailed ||
      consecutiveFailures == 0) {
    return false;
  }

  // Without a trustworthy clock there is no safe way to measure elapsed
  // backoff across deep-sleep resets. Keep routine updates suppressed, but a
  // profile with no usable image gets one bounded attempt on the next wake;
  // otherwise a transient first failure could block it indefinitely.
  if (currentDate == 0) return !currentProfileMissing;
  if (lastAttemptDate != currentDate) return false;
  if (consecutiveFailures >= 3) return true;
  if (currentMinute >= 24U * 60U || lastAttemptMinute >= 24U * 60U || currentMinute < lastAttemptMinute) {
    return false;
  }

  static constexpr uint16_t RETRY_DELAYS_MINUTES[] = {5, 30, 180};
  const uint8_t delayIndex = std::min<uint8_t>(consecutiveFailures - 1, std::size(RETRY_DELAYS_MINUTES) - 1);
  return currentMinute - lastAttemptMinute < RETRY_DELAYS_MINUTES[delayIndex];
}

}  // namespace vannhanso_update_policy
