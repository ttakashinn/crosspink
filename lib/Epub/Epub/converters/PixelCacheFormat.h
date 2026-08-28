#pragma once

#include <stddef.h>
#include <stdint.h>

// On-disk .pxc header shared by the cache reader and streaming writer.
// Bump the version whenever quantization or packing semantics change so books
// already opened on a device are decoded again instead of rendering stale
// pixels. The magic cannot collide with a legacy width on supported panels.
namespace pixel_cache_format {

constexpr uint16_t MAGIC = 0x4358;  // "XC" in little-endian byte order
constexpr uint8_t VERSION = 2;      // v2: native-level-anchored Bayer quantization
constexpr size_t HEADER_SIZE = sizeof(MAGIC) + sizeof(VERSION) + sizeof(uint16_t) + sizeof(uint16_t);

}  // namespace pixel_cache_format
