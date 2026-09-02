#pragma once

#include <cstdint>

#include "fontIds.h"

namespace dictionary_typography {

// Dictionary text, including sleep-screen review cards, deliberately uses a
// built-in Noto Sans face. Definitions often contain IPA mixed into ordinary
// meaning lines; using a Serif or active book/SD font makes phonetic coverage
// vary with the rendering path. The built-in faces are always resident and are
// generated with the complete phonetic ranges used by dictionaries.
inline int bodyFontId(const uint8_t requestedPointSize) {
  if (requestedPointSize <= 13) return NOTOSANS_12_FONT_ID;
  if (requestedPointSize <= 15) return NOTOSANS_14_FONT_ID;
  if (requestedPointSize <= 17) return NOTOSANS_16_FONT_ID;
  return NOTOSANS_18_FONT_ID;
}

inline float lineCompression(const uint8_t lineSpacing) {
  switch (lineSpacing) {
    case 0:
      return 0.90f;
    case 2:
      return 1.0f;
    case 3:
      return 1.05f;
    case 1:
    default:
      return 0.95f;
  }
}

}  // namespace dictionary_typography
