#pragma once

#include <cstdint>

struct CrossPointSettings {
  enum { SOURCESERIF = 0, SOURCESANS = 1, FONT_FAMILY_COUNT = 2 };
  enum { TIGHT = 0, NORMAL = 1, WIDE = 2, EXTRA_WIDE = 3, LINE_COMPRESSION_COUNT = 4 };
  enum {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT = 5
  };
  enum { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3, ORIENTATION_COUNT = 4 };
  enum { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT = 3 };
  static constexpr uint8_t DEFAULT_FONT_POINT_SIZE = 14;
  static constexpr uint8_t SCREEN_MARGIN_MIN = 5;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 40;
  static constexpr uint8_t SCREEN_MARGIN_STEP = 5;

  uint8_t fontFamily = 0;
  uint8_t fontPointSize = 14;
  uint8_t lineSpacing = 1;
  uint8_t paragraphAlignment = 0;
  uint8_t orientation = 0;
  uint8_t screenMargin = 5;
  uint8_t embeddedStyle = 1;
  uint8_t focusReadingEnabled = 0;
  uint8_t hyphenationEnabled = 0;
  uint8_t extraParagraphSpacing = 1;
  uint8_t textAntiAliasing = 1;
  uint8_t imageRendering = 0;
  char sdFontFamilyName[32] = {};
};

extern CrossPointSettings SETTINGS;
