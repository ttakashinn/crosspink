#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CrossPointSettings.h"

struct PerBookReaderSettings {
  static constexpr size_t SD_FONT_NAME_CAPACITY = 32;
  static constexpr size_t DICTIONARY_NAME_CAPACITY = 32;

  enum OverrideField : uint16_t {
    OVERRIDE_FONT_FAMILY = 1U << 0,  // built-in/SD family and SD family name
    OVERRIDE_FONT_SIZE = 1U << 1,
    OVERRIDE_LINE_SPACING = 1U << 2,
    OVERRIDE_ALIGNMENT = 1U << 3,
    OVERRIDE_ORIENTATION = 1U << 4,
    OVERRIDE_MARGIN = 1U << 5,
    OVERRIDE_EMBEDDED_STYLE = 1U << 6,
    OVERRIDE_FOCUS_READING = 1U << 7,
    OVERRIDE_HYPHENATION = 1U << 8,
    OVERRIDE_PARAGRAPH_SPACING = 1U << 9,
    OVERRIDE_ANTI_ALIASING = 1U << 10,
    OVERRIDE_IMAGES = 1U << 11,
  };
  static constexpr uint16_t ALL_READER_OVERRIDE_FIELDS = (1U << 12) - 1U;

  bool hasOverrides = false;
  uint16_t overrideMask = 0;
  uint8_t fontFamily = CrossPointSettings::SOURCESERIF;
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
  // Kept independently from hasOverrides: choosing a dictionary for one book
  // must not implicitly freeze all of that book's typography settings.
  bool hasDictionaryOverride = false;
  std::array<char, DICTIONARY_NAME_CAPACITY> dictionaryName{};

  bool operator==(const PerBookReaderSettings&) const = default;
};

inline PerBookReaderSettings captureReaderSettings(const bool hasOverrides = false) {
  PerBookReaderSettings out;
  out.hasOverrides = hasOverrides;
  out.overrideMask = hasOverrides ? PerBookReaderSettings::ALL_READER_OVERRIDE_FIELDS : 0;
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

inline uint16_t readerSettingsOverrideMask(const PerBookReaderSettings& current, const PerBookReaderSettings& global) {
  uint16_t mask = 0;
  if (current.fontFamily != global.fontFamily || current.sdFontFamilyName != global.sdFontFamilyName) {
    mask |= PerBookReaderSettings::OVERRIDE_FONT_FAMILY;
  }
  if (current.fontPointSize != global.fontPointSize) mask |= PerBookReaderSettings::OVERRIDE_FONT_SIZE;
  if (current.lineSpacing != global.lineSpacing) mask |= PerBookReaderSettings::OVERRIDE_LINE_SPACING;
  if (current.paragraphAlignment != global.paragraphAlignment) mask |= PerBookReaderSettings::OVERRIDE_ALIGNMENT;
  if (current.orientation != global.orientation) mask |= PerBookReaderSettings::OVERRIDE_ORIENTATION;
  if (current.screenMargin != global.screenMargin) mask |= PerBookReaderSettings::OVERRIDE_MARGIN;
  if (current.embeddedStyle != global.embeddedStyle) mask |= PerBookReaderSettings::OVERRIDE_EMBEDDED_STYLE;
  if (current.focusReadingEnabled != global.focusReadingEnabled) mask |= PerBookReaderSettings::OVERRIDE_FOCUS_READING;
  if (current.hyphenationEnabled != global.hyphenationEnabled) mask |= PerBookReaderSettings::OVERRIDE_HYPHENATION;
  if (current.extraParagraphSpacing != global.extraParagraphSpacing) {
    mask |= PerBookReaderSettings::OVERRIDE_PARAGRAPH_SPACING;
  }
  if (current.textAntiAliasing != global.textAntiAliasing) mask |= PerBookReaderSettings::OVERRIDE_ANTI_ALIASING;
  if (current.imageRendering != global.imageRendering) mask |= PerBookReaderSettings::OVERRIDE_IMAGES;
  return mask;
}

// Keeps EPUB-only extensions/dictionary selection from `overrides`, while
// allowing every unmodified typography field to continue following General
// Settings. This is the effective record used for the open reader session.
inline PerBookReaderSettings mergeReaderSettings(const PerBookReaderSettings& global,
                                                 const PerBookReaderSettings& overrides) {
  PerBookReaderSettings out = overrides;
  const uint16_t mask = overrides.overrideMask;
  if (!(mask & PerBookReaderSettings::OVERRIDE_FONT_FAMILY)) {
    out.fontFamily = global.fontFamily;
    out.sdFontFamilyName = global.sdFontFamilyName;
  }
  if (!(mask & PerBookReaderSettings::OVERRIDE_FONT_SIZE)) out.fontPointSize = global.fontPointSize;
  if (!(mask & PerBookReaderSettings::OVERRIDE_LINE_SPACING)) out.lineSpacing = global.lineSpacing;
  if (!(mask & PerBookReaderSettings::OVERRIDE_ALIGNMENT)) out.paragraphAlignment = global.paragraphAlignment;
  if (!(mask & PerBookReaderSettings::OVERRIDE_ORIENTATION)) out.orientation = global.orientation;
  if (!(mask & PerBookReaderSettings::OVERRIDE_MARGIN)) out.screenMargin = global.screenMargin;
  if (!(mask & PerBookReaderSettings::OVERRIDE_EMBEDDED_STYLE)) out.embeddedStyle = global.embeddedStyle;
  if (!(mask & PerBookReaderSettings::OVERRIDE_FOCUS_READING)) {
    out.focusReadingEnabled = global.focusReadingEnabled;
  }
  if (!(mask & PerBookReaderSettings::OVERRIDE_HYPHENATION)) out.hyphenationEnabled = global.hyphenationEnabled;
  if (!(mask & PerBookReaderSettings::OVERRIDE_PARAGRAPH_SPACING)) {
    out.extraParagraphSpacing = global.extraParagraphSpacing;
  }
  if (!(mask & PerBookReaderSettings::OVERRIDE_ANTI_ALIASING)) out.textAntiAliasing = global.textAntiAliasing;
  if (!(mask & PerBookReaderSettings::OVERRIDE_IMAGES)) out.imageRendering = global.imageRendering;
  out.hasOverrides = mask != 0;
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
