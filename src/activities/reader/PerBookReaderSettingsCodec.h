#pragma once

#include <Utf8.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "PerBookReaderSettings.h"

namespace PerBookReaderSettingsCodec {

constexpr std::array<uint8_t, 4> MAGIC = {'V', 'N', 'S', 'R'};
constexpr uint8_t VERSION = 3;
constexpr uint16_t V1_PAYLOAD_SIZE = 45;
constexpr uint16_t V2_PAYLOAD_SIZE = 55;
constexpr uint16_t PAYLOAD_SIZE = 88;
constexpr size_t VERSION_OFFSET = 4;
constexpr size_t LENGTH_OFFSET = 5;
constexpr size_t CRC_OFFSET = 7;
constexpr size_t PAYLOAD_OFFSET = 11;
constexpr size_t ENCODED_SIZE = PAYLOAD_OFFSET + PAYLOAD_SIZE;
using Encoded = std::array<uint8_t, ENCODED_SIZE>;

enum class DecodeStatus : uint8_t {
  OK,
  TRUNCATED,
  WRONG_SIZE,
  BAD_MAGIC,
  NEWER_VERSION,
  UNSUPPORTED_VERSION,
  BAD_LENGTH,
  BAD_CRC,
  INVALID_VALUE,
};

inline uint16_t readU16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}
inline uint32_t readU32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}
inline void writeU16(uint8_t* bytes, const uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}
inline void writeU32(uint8_t* bytes, const uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

inline uint32_t crc32(const uint8_t* bytes, const size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

inline bool validFontName(const std::array<char, PerBookReaderSettings::SD_FONT_NAME_CAPACITY>& name) {
  size_t length = 0;
  while (length < name.size() && name[length] != '\0') ++length;
  return length < name.size() && utf8IsValid({name.data(), length}) &&
         std::all_of(
             name.begin() + static_cast<std::ptrdiff_t>(length), name.end(),
             [](const char value) { return value == '\0'; });
}

inline bool validDictionaryName(const std::array<char, PerBookReaderSettings::DICTIONARY_NAME_CAPACITY>& name) {
  size_t length = 0;
  while (length < name.size() && name[length] != '\0') ++length;
  if (length >= name.size() || !utf8IsValid({name.data(), length})) return false;
  if (length > 0 && name[0] == '.') return false;
  for (size_t i = 0; i < length; ++i) {
    if (name[i] == '/' || name[i] == '\\') return false;
  }
  return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(length), name.end(),
                     [](const char value) { return value == '\0'; });
}

inline bool isValid(const PerBookReaderSettings& settings) {
  const auto toggle = [](const uint8_t value) { return value <= 1; };
  return settings.fontFamily < CrossPointSettings::FONT_FAMILY_COUNT && settings.fontPointSize >= 8 &&
         settings.fontPointSize <= 40 && settings.lineSpacing < CrossPointSettings::LINE_COMPRESSION_COUNT &&
         settings.paragraphAlignment < CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT &&
         settings.orientation < CrossPointSettings::ORIENTATION_COUNT &&
         settings.screenMargin >= CrossPointSettings::SCREEN_MARGIN_MIN &&
         settings.screenMargin <= CrossPointSettings::SCREEN_MARGIN_MAX &&
         (settings.screenMargin - CrossPointSettings::SCREEN_MARGIN_MIN) % CrossPointSettings::SCREEN_MARGIN_STEP ==
             0 &&
         toggle(settings.embeddedStyle) && toggle(settings.focusReadingEnabled) &&
         toggle(settings.hyphenationEnabled) && toggle(settings.extraParagraphSpacing) &&
         toggle(settings.textAntiAliasing) && settings.imageRendering < CrossPointSettings::IMAGE_RENDERING_COUNT &&
         settings.wordSpacing <= 2 && toggle(settings.repairParagraphIndent) &&
         (settings.autoPageTurnSeconds == 0 ||
          (settings.autoPageTurnSeconds >= 5 && settings.autoPageTurnSeconds <= 120)) &&
         settings.preferredRenderMode <= 2 &&
         (settings.lastWorkingFallback == UINT8_MAX || settings.lastWorkingFallback <= 2) &&
         validFontName(settings.sdFontFamilyName) && validDictionaryName(settings.dictionaryName);
}

inline bool encode(const PerBookReaderSettings& settings, Encoded& encoded) {
  if (!isValid(settings)) return false;
  encoded.fill(0);
  std::copy(MAGIC.begin(), MAGIC.end(), encoded.begin());
  encoded[VERSION_OFFSET] = VERSION;
  writeU16(encoded.data() + LENGTH_OFFSET, PAYLOAD_SIZE);
  uint8_t* payload = encoded.data() + PAYLOAD_OFFSET;
  payload[0] = settings.hasOverrides ? 1 : 0;
  payload[1] = settings.fontFamily;
  payload[2] = settings.fontPointSize;
  payload[3] = settings.lineSpacing;
  payload[4] = settings.paragraphAlignment;
  payload[5] = settings.orientation;
  payload[6] = settings.screenMargin;
  payload[7] = settings.embeddedStyle;
  payload[8] = settings.focusReadingEnabled;
  payload[9] = settings.hyphenationEnabled;
  payload[10] = settings.extraParagraphSpacing;
  payload[11] = settings.textAntiAliasing;
  payload[12] = settings.imageRendering;
  std::memcpy(payload + 13, settings.sdFontFamilyName.data(), settings.sdFontFamilyName.size());
  payload[45] = settings.wordSpacing;
  payload[46] = settings.repairParagraphIndent;
  writeU16(payload + 47, settings.autoPageTurnSeconds);
  payload[49] = settings.preferredRenderMode;
  payload[50] = settings.lastWorkingFallback;
  writeU32(payload + 51, settings.fallbackRenderSignature);
  payload[55] = settings.hasDictionaryOverride ? 1 : 0;
  std::memcpy(payload + 56, settings.dictionaryName.data(), settings.dictionaryName.size());
  writeU32(encoded.data() + CRC_OFFSET, crc32(payload, PAYLOAD_SIZE));
  return true;
}

inline DecodeStatus decode(const uint8_t* bytes, const size_t length, PerBookReaderSettings& settings) {
  if (!bytes || length < PAYLOAD_OFFSET) return DecodeStatus::TRUNCATED;
  if (!std::equal(MAGIC.begin(), MAGIC.end(), bytes)) return DecodeStatus::BAD_MAGIC;
  const uint8_t version = bytes[VERSION_OFFSET];
  if (version > VERSION) return DecodeStatus::NEWER_VERSION;
  if (version != 1 && version != 2 && version != VERSION) return DecodeStatus::UNSUPPORTED_VERSION;
  const uint16_t expectedPayloadSize = version == 1 ? V1_PAYLOAD_SIZE : (version == 2 ? V2_PAYLOAD_SIZE : PAYLOAD_SIZE);
  const size_t expectedSize = PAYLOAD_OFFSET + expectedPayloadSize;
  if (length < expectedSize) return DecodeStatus::TRUNCATED;
  if (length != expectedSize) return DecodeStatus::WRONG_SIZE;
  if (readU16(bytes + LENGTH_OFFSET) != expectedPayloadSize) return DecodeStatus::BAD_LENGTH;
  const uint8_t* payload = bytes + PAYLOAD_OFFSET;
  if (readU32(bytes + CRC_OFFSET) != crc32(payload, expectedPayloadSize)) return DecodeStatus::BAD_CRC;
  if (payload[0] > 1) return DecodeStatus::INVALID_VALUE;

  PerBookReaderSettings decoded;
  decoded.hasOverrides = payload[0] != 0;
  decoded.fontFamily = payload[1];
  decoded.fontPointSize = payload[2];
  decoded.lineSpacing = payload[3];
  decoded.paragraphAlignment = payload[4];
  decoded.orientation = payload[5];
  decoded.screenMargin = payload[6];
  decoded.embeddedStyle = payload[7];
  decoded.focusReadingEnabled = payload[8];
  decoded.hyphenationEnabled = payload[9];
  decoded.extraParagraphSpacing = payload[10];
  decoded.textAntiAliasing = payload[11];
  decoded.imageRendering = payload[12];
  std::memcpy(decoded.sdFontFamilyName.data(), payload + 13, decoded.sdFontFamilyName.size());
  if (version >= 2) {
    decoded.wordSpacing = payload[45];
    decoded.repairParagraphIndent = payload[46];
    decoded.autoPageTurnSeconds = readU16(payload + 47);
    decoded.preferredRenderMode = payload[49];
    decoded.lastWorkingFallback = payload[50];
    decoded.fallbackRenderSignature = readU32(payload + 51);
  }
  if (version >= 3) {
    if (payload[55] > 1) return DecodeStatus::INVALID_VALUE;
    decoded.hasDictionaryOverride = payload[55] != 0;
    std::memcpy(decoded.dictionaryName.data(), payload + 56, decoded.dictionaryName.size());
  }
  if (!isValid(decoded)) return DecodeStatus::INVALID_VALUE;
  settings = decoded;
  return DecodeStatus::OK;
}

}  // namespace PerBookReaderSettingsCodec
