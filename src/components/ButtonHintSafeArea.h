#pragma once

#include <cstdint>

namespace button_hints {

enum class Orientation : uint8_t { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

struct Insets {
  int16_t top = 0;
  int16_t right = 0;
  int16_t bottom = 0;
  int16_t left = 0;
};

// The front-button guide is painted in the panel's native portrait frame.
// In portrait it occupies the bottom (the top when inverted). In landscape,
// keep the content symmetrically clear of that guide: the real guide is on
// one side, while the matching opposite gutter prevents controls and long
// labels from appearing visually trapped under the hardware-button edge.
constexpr Insets safeAreaInsets(const Orientation orientation, const int16_t extent) {
  if (extent <= 0) return {};
  switch (orientation) {
    case Orientation::Portrait:
      return Insets{0, 0, extent, 0};
    case Orientation::PortraitInverted:
      return Insets{extent, 0, 0, 0};
    case Orientation::LandscapeClockwise:
    case Orientation::LandscapeCounterClockwise:
      return Insets{0, extent, 0, extent};
  }
  return {};
}

}  // namespace button_hints
