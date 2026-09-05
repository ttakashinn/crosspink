#pragma once

#include <cstdint>
#include <limits>

namespace font_read_safety {

constexpr bool containsRange(const uint64_t fileSize, const uint64_t offset, const uint64_t length) {
  return offset <= fileSize && length <= fileSize - offset;
}

constexpr bool addUint32(const uint32_t current, const uint32_t value, uint32_t& result) {
  if (value > std::numeric_limits<uint32_t>::max() - current) return false;
  result = current + value;
  return true;
}

}  // namespace font_read_safety
