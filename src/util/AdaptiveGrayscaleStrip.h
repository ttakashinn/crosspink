#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace adaptive_grayscale_strip {

// 80 rows costs 8 KiB on an 800 px-wide panel. Under fragmented heap, retry
// progressively smaller bands instead of silently dropping text AA for the
// whole page. Ten rows is the bounded floor: smaller bands add substantial
// re-render overhead for little practical allocation benefit.
constexpr std::array<uint16_t, 4> ROW_CANDIDATES = {80, 40, 20, 10};
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
