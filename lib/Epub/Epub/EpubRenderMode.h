#pragma once

#include <cstddef>
#include <cstdint>

// Standard preserves the complete renderer. Simplified keeps publisher CSS and
// images but avoids descendant selector matching, complex table grids and
// decorative lines. Safe is the final low-memory text-first fallback.
enum class EpubRenderMode : uint8_t {
  Standard = 0,
  Simplified = 1,
  Safe = 2,
};

inline const char* epubRenderModeName(const EpubRenderMode mode) {
  switch (mode) {
    case EpubRenderMode::Simplified:
      return "simplified";
    case EpubRenderMode::Safe:
      return "safe";
    case EpubRenderMode::Standard:
    default:
      return "standard";
  }
}

inline bool isValidEpubRenderMode(const uint8_t mode) { return mode <= static_cast<uint8_t>(EpubRenderMode::Safe); }

inline EpubRenderMode nextLighterEpubRenderMode(const EpubRenderMode mode) {
  return mode == EpubRenderMode::Standard ? EpubRenderMode::Simplified : EpubRenderMode::Safe;
}

struct EpubLayoutHeapFloor {
  size_t minFreeHeap;
  size_t minMaxAlloc;
};

// Optional layout features consume materially different amounts of heap. Keep
// the conservative historical floor for Standard, relax it in bounded steps
// for the two fallbacks, and never make Safe use the same gate that rejected
// Standard in the first place.
constexpr EpubLayoutHeapFloor epubLayoutHeapFloor(const EpubRenderMode mode) {
  switch (mode) {
    case EpubRenderMode::Simplified:
      return {40 * 1024, 28 * 1024};
    case EpubRenderMode::Safe:
      return {36 * 1024, 24 * 1024};
    case EpubRenderMode::Standard:
    default:
      return {44 * 1024, 32 * 1024};
  }
}

constexpr bool epubLayoutHeapSufficient(const EpubRenderMode mode, const size_t freeHeap, const size_t maxAlloc) {
  const EpubLayoutHeapFloor floor = epubLayoutHeapFloor(mode);
  return freeHeap >= floor.minFreeHeap && maxAlloc >= floor.minMaxAlloc;
}

constexpr bool shouldStartPreferredRenderTrial(const uint8_t preferredMode, const EpubRenderMode activeMode,
                                               const bool renderSettingsChanged) {
  return !renderSettingsChanged && isValidEpubRenderMode(preferredMode) &&
         static_cast<uint8_t>(activeMode) > preferredMode;
}

// A low-memory retry is only a candidate until a page has actually made it
// through layout, load and render. Persisting it earlier can pin a book to a
// fallback that also fails.
inline bool shouldRememberEpubFallback(const uint8_t preferredMode, const EpubRenderMode activeMode,
                                       const bool pageRendered) {
  return pageRendered && isValidEpubRenderMode(preferredMode) && static_cast<uint8_t>(activeMode) > preferredMode;
}
