#pragma once

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

// A low-memory retry is only a candidate until a page has actually made it
// through layout, load and render. Persisting it earlier can pin a book to a
// fallback that also fails.
inline bool shouldRememberEpubFallback(const uint8_t preferredMode, const EpubRenderMode activeMode,
                                       const bool pageRendered) {
  return pageRendered && isValidEpubRenderMode(preferredMode) && static_cast<uint8_t>(activeMode) > preferredMode;
}
