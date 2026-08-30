#pragma once

namespace ota_update_policy {

// One retry absorbs a transient TLS/socket failure without repeatedly writing
// a large temporary image to the SD card on a persistent connection problem.
constexpr unsigned HTTP_ATTEMPTS = 2;
constexpr int PROGRESS_STEP_PERCENT = 5;

constexpr bool hasAnotherHttpAttempt(const unsigned completedAttempts) { return completedAttempts < HTTP_ATTEMPTS; }

constexpr bool shouldPublishProgress(const int lastPercent, const int currentPercent) {
  return lastPercent < 0 || currentPercent < lastPercent || currentPercent >= 100 ||
         currentPercent >= lastPercent + PROGRESS_STEP_PERCENT;
}

}  // namespace ota_update_policy
