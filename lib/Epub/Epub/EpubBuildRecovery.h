#pragma once

#include <cstdint>

#include "EpubRenderMode.h"
#include "SectionBuildFailure.h"

enum class EpubBuildRecovery : uint8_t {
  None,
  RetryLighterMode,
  RestartReaderOnce,
};

constexpr EpubBuildRecovery epubBuildRecoveryFor(const SectionBuildFailure failure, const EpubRenderMode activeMode,
                                                 const bool bootWasLowMemoryRestart) {
  if (failure != SectionBuildFailure::LowMemory) return EpubBuildRecovery::None;
  if (activeMode != EpubRenderMode::Safe) return EpubBuildRecovery::RetryLighterMode;
  return bootWasLowMemoryRestart ? EpubBuildRecovery::None : EpubBuildRecovery::RestartReaderOnce;
}

// Background indexing is speculative. If OOM left a readable partial, keep
// the current pagination stable and defer fallback until the reader actually
// reaches that watermark.
constexpr bool shouldPauseEpubBackgroundBuild(const SectionBuildFailure failure, const bool hasReadablePartial,
                                              const uint16_t readablePages) {
  return failure == SectionBuildFailure::LowMemory && hasReadablePartial && readablePages > 0;
}
