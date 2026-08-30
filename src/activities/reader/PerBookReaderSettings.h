#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CrossPointSettings.h"

struct PerBookReaderSettings {
  static constexpr size_t SD_FONT_NAME_CAPACITY = 32;

  bool hasOverrides = false;
  uint8_t fontFamily = CrossPointSettings::NOTOSERIF;
  uint8_t fontPointSize = CrossPointSettings::DEFAULT_FONT_POINT_SIZE;
  uint8_t lineSpacing = CrossPointSettings::NORMAL;
  uint8_t paragraphAlignment = CrossPointSettings::JUSTIFIED;
  uint8_t orientation = CrossPointSettings::PORTRAIT;
  uint8_t screenMargin = CrossPointSettings::SCREEN_MARGIN_MIN;
  uint8_t embeddedStyle = 1;
  uint8_t focusReadingEnabled = 0;
  uint8_t hyphenationEnabled = 0;
  uint8_t extraParagraphSpacing = 1;
  uint8_t textAntiAliasing = 1;
  uint8_t imageRendering = CrossPointSettings::IMAGES_DISPLAY;
  // EPUB-only extensions remain in the per-book record instead of changing
  // global reader defaults for unrelated books.
  uint8_t wordSpacing = 0;            // 0=normal, 1=relaxed, 2=wide
  uint8_t repairParagraphIndent = 0;  // only paragraphs whose computed indent is zero
  uint16_t autoPageTurnSeconds = 0;   // 0=off, otherwise 5..120
  uint8_t preferredRenderMode = 0;    // EpubRenderMode raw value
  uint8_t lastWorkingFallback = UINT8_MAX;
  uint32_t fallbackRenderSignature = 0;
  std::array<char, SD_FONT_NAME_CAPACITY> sdFontFamilyName{};

  bool operator==(const PerBookReaderSettings&) const = default;
};

inline PerBookReaderSettings captureReaderSettings(const bool hasOverrides = false) {
  PerBookReaderSettings out;
  out.hasOverrides = hasOverrides;
  out.fontFamily = SETTINGS.fontFamily;
  out.fontPointSize = SETTINGS.fontPointSize;
  out.lineSpacing = SETTINGS.lineSpacing;
  out.paragraphAlignment = SETTINGS.paragraphAlignment;
  out.orientation = SETTINGS.orientation;
  out.screenMargin = SETTINGS.screenMargin;
  out.embeddedStyle = SETTINGS.embeddedStyle;
  out.focusReadingEnabled = SETTINGS.focusReadingEnabled;
  out.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  out.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  out.textAntiAliasing = SETTINGS.textAntiAliasing;
  out.imageRendering = SETTINGS.imageRendering;
  std::strncpy(out.sdFontFamilyName.data(), SETTINGS.sdFontFamilyName, out.sdFontFamilyName.size() - 1);
  return out;
}

inline void applyReaderSettings(const PerBookReaderSettings& settings) {
  SETTINGS.fontFamily = settings.fontFamily;
  SETTINGS.fontPointSize = settings.fontPointSize;
  SETTINGS.lineSpacing = settings.lineSpacing;
  SETTINGS.paragraphAlignment = settings.paragraphAlignment;
  SETTINGS.orientation = settings.orientation;
  SETTINGS.screenMargin = settings.screenMargin;
  SETTINGS.embeddedStyle = settings.embeddedStyle;
  SETTINGS.focusReadingEnabled = settings.focusReadingEnabled;
  SETTINGS.hyphenationEnabled = settings.hyphenationEnabled;
  SETTINGS.extraParagraphSpacing = settings.extraParagraphSpacing;
  SETTINGS.textAntiAliasing = settings.textAntiAliasing;
  SETTINGS.imageRendering = settings.imageRendering;
  std::strncpy(SETTINGS.sdFontFamilyName, settings.sdFontFamilyName.data(), sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
}

inline uint32_t readerSettingsRenderSignature(const PerBookReaderSettings& settings) {
  uint32_t hash = 2166136261U;
  const auto mix = [&hash](const uint32_t value) {
    hash ^= value;
    hash *= 16777619U;
  };
  mix(settings.fontFamily);
  mix(settings.fontPointSize);
  mix(settings.lineSpacing);
  mix(settings.paragraphAlignment);
  mix(settings.orientation);
  mix(settings.screenMargin);
  mix(settings.embeddedStyle);
  mix(settings.focusReadingEnabled);
  mix(settings.hyphenationEnabled);
  mix(settings.extraParagraphSpacing);
  mix(settings.imageRendering);
  mix(settings.wordSpacing);
  mix(settings.repairParagraphIndent);
  for (const char ch : settings.sdFontFamilyName) mix(static_cast<uint8_t>(ch));
  return hash;
}
