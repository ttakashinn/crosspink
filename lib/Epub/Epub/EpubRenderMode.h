#pragma once

#include <cstdint>

// Standard preserves publisher styling and images. Safe is an automatic,
// session-scoped fallback used only after a section build reports a low-memory
// failure; it keeps text readable while avoiding the two heaviest optional
// inputs to layout.
enum class EpubRenderMode : uint8_t {
  Standard = 0,
  Safe = 1,
};

inline const char* epubRenderModeName(const EpubRenderMode mode) {
  return mode == EpubRenderMode::Safe ? "safe" : "standard";
}
