#pragma once

#include <cstdint>

namespace text_raster {

// Reader glyphs store FreeType coverage in four levels:
//   0 = white, 1 = light edge, 2 = dark edge, 3 = solid ink.
//
// A grayscale base must include every non-white sample because the two delta
// planes can only lighten pixels which the base first paints black. A pure
// 1-bit page instead uses the midpoint threshold: promoting level 1 to black
// thickens every outline and is the source of the visibly jagged/dark no-AA
// rendering. Level 2 retains the supported part of thin strokes and Vietnamese
// diacritics while discarding only the weakest fringe.
enum class BwPurpose : uint8_t { GrayscaleBase, CrispMonochrome };

constexpr bool paintsBw(const uint8_t coverage, const BwPurpose purpose) {
  return purpose == BwPurpose::GrayscaleBase ? coverage != 0 : coverage >= 2;
}

enum GrayPlane : uint8_t { None = 0, Lsb = 1, Msb = 2 };

// Delta masks consumed by the display drivers. Solid black is already present
// in the B/W base, while partial coverage selects one or both gray planes.
constexpr uint8_t grayPlanes(const uint8_t coverage) {
  return coverage == 1 ? Msb : (coverage == 2 ? static_cast<uint8_t>(Lsb | Msb) : None);
}

}  // namespace text_raster
