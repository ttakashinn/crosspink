#pragma once

#include <string_view>

inline bool utf8IsValid(std::string_view text) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = bytes[i++];
    if (c < 0x80) continue;
    size_t continuation = c < 0xE0 ? 1 : (c < 0xF0 ? 2 : (c < 0xF8 ? 3 : 99));
    if (continuation == 99 || i + continuation > text.size()) return false;
    while (continuation--) {
      if ((bytes[i++] & 0xC0) != 0x80) return false;
    }
  }
  return true;
}
