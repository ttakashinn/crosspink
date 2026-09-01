#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace adaptive_grayscale_strip {

// 80 rows costs 8 KiB on an 800 px-wide panel. Under fragmented heap, retry
// progressively smaller bands instead of silently dropping text AA for the
// whole page. Five rows is the bounded floor: it keeps AA available with only
// ~500 bytes on an 800 px panel, while smaller bands add disproportionate
// full-page re-render overhead.
constexpr std::array<uint16_t, 5> ROW_CANDIDATES = {80, 40, 20, 10, 5};
constexpr uint16_t DEFAULT_ROWS = ROW_CANDIDATES.front();

template <typename Allocate>
std::unique_ptr<uint8_t[]> allocate(const size_t rowBytes, uint16_t& selectedRows, Allocate&& allocateBytes) {
  selectedRows = 0;
  if (rowBytes == 0) return nullptr;

  for (const uint16_t rows : ROW_CANDIDATES) {
    if (rowBytes > std::numeric_limits<size_t>::max() / rows) continue;
    auto buffer = allocateBytes(rowBytes * rows);
    if (buffer) {
      selectedRows = rows;
      return buffer;
    }
  }
  return nullptr;
}

}  // namespace adaptive_grayscale_strip

namespace grayscale_pass {

// A settle pass is useful only when grayscale planes will actually be
// written. Panels that defer/combine the B/W base with those planes own the
// complete waveform and must not receive a separate precondition request.
// The display layer keeps this policy portable: the request is an X3-specific
// hook and a deliberate no-op on panels that do not need it.
constexpr bool shouldPrecondition(const bool planesAvailable, const bool combinedBase) {
  return planesAvailable && !combinedBase;
}

}  // namespace grayscale_pass
