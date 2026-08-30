#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace vns_reference {

inline constexpr uint8_t SCHEMA_VERSION = 1;
inline constexpr uint32_t POSITION_UNIT = 1024;

struct Position {
  uint8_t version = SCHEMA_VERSION;
  uint32_t contentSignature = 0;
  uint16_t spineIndex = 0;
  uint32_t visibleTextOffset = 0;
  uint32_t ordinal = 1;

  bool operator==(const Position&) const = default;
};

inline uint32_t appendSignature(uint32_t hash, const std::string_view bytes) {
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 16777619U;
  }
  return hash;
}

inline uint32_t appendSignature(uint32_t hash, const uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    hash ^= static_cast<uint8_t>(value >> shift);
    hash *= 16777619U;
  }
  return hash;
}

inline Position make(const uint32_t signature, const uint16_t spineIndex, const uint32_t cumulativeBytesBeforeSpine,
                     const uint32_t visibleTextOffset) {
  const uint64_t anchor = static_cast<uint64_t>(cumulativeBytesBeforeSpine) + visibleTextOffset;
  return {SCHEMA_VERSION, signature, spineIndex, visibleTextOffset, static_cast<uint32_t>(anchor / POSITION_UNIT + 1U)};
}

inline std::string encode(const Position& position) {
  char token[64];
  snprintf(token, sizeof(token), "vnspos:%u:%08lx:%u:%lu:%lu", static_cast<unsigned>(position.version),
           static_cast<unsigned long>(position.contentSignature), static_cast<unsigned>(position.spineIndex),
           static_cast<unsigned long>(position.visibleTextOffset), static_cast<unsigned long>(position.ordinal));
  return token;
}

inline size_t utf8PrefixBytes(const std::string_view text, const size_t byteLimit) {
  size_t end = std::min(text.size(), byteLimit);
  if (end == text.size()) return end;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) --end;
  return end;
}

// Preserve the stable position token even when the section text exceeds QR
// capacity. Truncating the final payload inside QrUtils would otherwise cut off
// the suffix users need to identify the same reading position.
inline std::string buildQrPayload(const std::string_view sectionText, const Position& position,
                                  const size_t maxPayloadBytes) {
  const std::string token = encode(position);
  constexpr std::string_view separator = "\n\n";
  if (maxPayloadBytes <= token.size()) return token.substr(0, maxPayloadBytes);
  const size_t textBudget = maxPayloadBytes - token.size() - separator.size();
  const size_t prefixBytes = utf8PrefixBytes(sectionText, textBudget);
  std::string payload;
  payload.reserve(prefixBytes + separator.size() + token.size());
  payload.append(sectionText.data(), prefixBytes);
  payload.append(separator);
  payload.append(token);
  return payload;
}

inline bool decode(const std::string_view token, Position& out) {
  unsigned version = 0;
  unsigned long signature = 0;
  unsigned spine = 0;
  unsigned long offset = 0;
  unsigned long ordinal = 0;
  int consumed = 0;
  const std::string owned(token);
  if (sscanf(owned.c_str(), "vnspos:%u:%lx:%u:%lu:%lu%n", &version, &signature, &spine, &offset, &ordinal, &consumed) !=
          5 ||
      consumed != static_cast<int>(owned.size()) || version != SCHEMA_VERSION || signature > UINT32_MAX ||
      spine > UINT16_MAX || offset > UINT32_MAX || ordinal == 0 || ordinal > UINT32_MAX) {
    return false;
  }
  out = {SCHEMA_VERSION, static_cast<uint32_t>(signature), static_cast<uint16_t>(spine), static_cast<uint32_t>(offset),
         static_cast<uint32_t>(ordinal)};
  return true;
}

}  // namespace vns_reference
