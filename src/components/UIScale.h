#pragma once
#include "CrossPointSettings.h"
#include "fontIds.h"

// FreeInkUI font slots. This follows CrossInk's useful two-tier model: small
// uses 10 pt throughout, large uses 12 pt throughout. Keeping secondary text
// at the same point size avoids tiny values beside readable list labels.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
  if (SETTINGS.uiScale == CrossPointSettings::UI_SCALE_SMALL) {
    spec.smallFontId = UI_10_FONT_ID;
    spec.bodyFontId = UI_10_FONT_ID;
  } else {
    spec.smallFontId = UI_12_FONT_ID;
    spec.bodyFontId = UI_12_FONT_ID;
  }
  // Titles use the UI font, not a reader font: fui headers draw book and
  // directory titles, and the built-in Inter UI fonts cover Hebrew (plus the
  // size-matched SD CJK fallback) where the NotoSans reader subsets do not.
  // Keep them at 12 pt in both tiers so Compact never shrinks navigation
  // landmarks below the firmware's established title size.
  spec.titleFontId = UI_12_FONT_ID;
  return spec;
}

inline int16_t uiScaledListMetric(const int metric) {
  constexpr int basePointSize = 10;
  const int pointSize = SETTINGS.uiScale == CrossPointSettings::UI_SCALE_SMALL ? 10 : 12;
  return static_cast<int16_t>((metric * pointSize + basePointSize / 2) / basePointSize);
}
