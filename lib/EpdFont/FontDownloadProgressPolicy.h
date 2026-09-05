#pragma once

#include <cstddef>
#include <cstdint>

namespace font_download_progress {

// E-paper progress is deliberately coarse: a family can contain several font
// files, and refreshing for every network chunk quickly builds visible charge
// residue. Four intermediate steps per file keep the UI useful without turning
// a download into dozens or hundreds of panel updates.
constexpr uint8_t UI_STEP_PERCENT = 25;
constexpr uint32_t UI_MIN_INTERVAL_MS = 1000;

constexpr bool isComplete(const size_t downloaded, const size_t total) { return total > 0 && downloaded >= total; }

constexpr uint8_t percent(const size_t downloaded, const size_t total) {
  if (total == 0) return 0;
  if (downloaded >= total) return 100;
  return static_cast<uint8_t>((static_cast<uint64_t>(downloaded) * 100U) / total);
}

constexpr bool shouldPublish(const uint8_t lastPercent, const uint8_t currentPercent, const uint32_t elapsedMs,
                             const bool completed) {
  if (completed) return lastPercent < 100;
  if (currentPercent <= lastPercent || elapsedMs < UI_MIN_INTERVAL_MS) return false;
  return static_cast<uint8_t>(currentPercent - lastPercent) >= UI_STEP_PERCENT;
}

}  // namespace font_download_progress
