#pragma once

#include <cstdint>

enum class SectionBuildFailure : uint8_t {
  None,
  LowMemory,
  Io,
  InvalidContent,
};

// A newly failed build never makes an older, already validated cache unsafe.
// Keep that readable generation for every classified failure; explicit cache
// corruption is handled separately by the page-load recovery path.
constexpr bool shouldPreserveSectionCache(const SectionBuildFailure failure) {
  return failure != SectionBuildFailure::None;
}

// A low-memory abort is recoverable and may happen after several complete
// pages have already been serialized. Persist that readable prefix instead of
// throwing the work away and starting the same large chapter from page zero.
// For other failures, do not publish bytes from a suspect build.
constexpr bool shouldSuspendFailedSectionBuild(const SectionBuildFailure failure, const uint16_t builtPageCount,
                                               const bool hasReadablePartial) {
  return failure == SectionBuildFailure::LowMemory && (builtPageCount > 0 || hasReadablePartial);
}
