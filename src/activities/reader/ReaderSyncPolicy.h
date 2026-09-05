#pragma once

#include <cstdint>

namespace reader_sync {

// The per-book overlay is torn down when the reader is replaced by the sync
// activity. Carry the active reader orientation explicitly; use the persisted
// global orientation only as a defensive fallback for corrupt input.
constexpr uint8_t displayOrientation(const uint8_t readerOrientation, const uint8_t globalOrientation,
                                     const uint8_t orientationCount) {
  if (readerOrientation < orientationCount) return readerOrientation;
  return globalOrientation < orientationCount ? globalOrientation : 0;
}

}  // namespace reader_sync
