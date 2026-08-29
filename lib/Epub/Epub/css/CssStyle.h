#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Matches order of PARAGRAPH_ALIGNMENT in CrossPointSettings
enum class CssTextAlign : uint8_t { Justify = 0, Left = 1, Center = 2, Right = 3, None = 4 };
enum class CssUnit : uint8_t { Pixels = 0, Em = 1, Rem = 2, Points = 3, Percent = 4 };
enum class CssTextDirection : uint8_t { Ltr = 0, Rtl = 1 };

// Represents a CSS length value with its unit, allowing deferred resolution to pixels
struct CssLength {
  float value = 0.0f;
  CssUnit unit = CssUnit::Pixels;

  CssLength() = default;
  CssLength(const float v, const CssUnit u) : value(v), unit(u) {}

  // Convenience constructor for pixel values (most common case)
  explicit CssLength(const float pixels) : value(pixels) {}

  // Returns true if this length can be resolved to pixels with the given context.
  // Percentage units require a non-zero containerWidth to resolve.
  [[nodiscard]] bool isResolvable(const float containerWidth = 0) const {
    return unit != CssUnit::Percent || containerWidth > 0;
  }

  // Resolve to pixels given the current em size (font line height)
  // containerWidth is needed for percentage units (e.g. viewport width)
  [[nodiscard]] float toPixels(const float emSize, const float containerWidth = 0) const {
    switch (unit) {
      case CssUnit::Em:
      case CssUnit::Rem:
        return value * emSize;
      case CssUnit::Points:
        return value * 1.33f;  // Approximate pt to px conversion
      case CssUnit::Percent:
        return value * containerWidth / 100.0f;
      default:
        return value;
    }
  }

  // Resolve to int16_t pixels (for BlockStyle fields)
  [[nodiscard]] int16_t toPixelsInt16(const float emSize, const float containerWidth = 0) const {
    const float pixels = toPixels(emSize, containerWidth);
    if (!std::isfinite(pixels)) return 0;
    return static_cast<int16_t>(std::clamp(pixels, static_cast<float>(std::numeric_limits<int16_t>::min()),
                                           static_cast<float>(std::numeric_limits<int16_t>::max())));
  }
};

// Font style options matching CSS font-style property
enum class CssFontStyle : uint8_t { Normal = 0, Italic = 1 };

// Font weight options - CSS supports 100-900, we simplify to normal/bold
enum class CssFontWeight : uint8_t { Normal = 0, Bold = 1 };

// CSS font-variant / font-variant-caps values supported by the renderer.
enum class CssFontVariantCaps : uint8_t { Normal = 0, SmallCaps = 1 };

// Text decoration options. Values are bit flags so CSS can combine multiple line decorations.
enum class CssTextDecoration : uint8_t { None = 0, Underline = 1, LineThrough = 2 };

constexpr CssTextDecoration operator|(const CssTextDecoration a, const CssTextDecoration b) {
  return static_cast<CssTextDecoration>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr CssTextDecoration operator&(const CssTextDecoration a, const CssTextDecoration b) {
  return static_cast<CssTextDecoration>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr uint8_t CSS_TEXT_DECORATION_MASK =
    static_cast<uint8_t>(CssTextDecoration::Underline) | static_cast<uint8_t>(CssTextDecoration::LineThrough);

// Display options - only None and Block are relevant for e-ink rendering
enum class CssDisplay : uint8_t { Block = 0, None = 1 };

// Vertical alignment options for inline elements (e.g. superscript/subscript)
enum class CssVerticalAlign : uint8_t { Baseline = 0, Super = 1, Sub = 2 };
enum class CssPageBreak : uint8_t { Auto = 0, Always = 1 };

// Bitmask for tracking which properties have been explicitly set
struct CssPropertyFlags {
  uint16_t textAlign : 1;
  uint16_t fontStyle : 1;
  uint16_t fontWeight : 1;
  uint16_t textDecoration : 1;
  uint16_t textIndent : 1;
  uint16_t marginTop : 1;
  uint16_t marginBottom : 1;
  uint16_t marginLeft : 1;
  uint16_t marginRight : 1;
  uint16_t paddingTop : 1;
  uint16_t paddingBottom : 1;
  uint16_t paddingLeft : 1;
  uint16_t paddingRight : 1;
  uint16_t imageHeight : 1;
  uint16_t imageWidth : 1;
  uint16_t display : 1;
  uint16_t direction : 1;
  uint16_t verticalAlign : 1;
  uint16_t pageBreakBefore : 1;
  uint16_t pageBreakAfter : 1;
  uint16_t fontVariantCaps : 1;

  CssPropertyFlags()
      : textAlign(0),
        fontStyle(0),
        fontWeight(0),
        textDecoration(0),
        textIndent(0),
        marginTop(0),
        marginBottom(0),
        marginLeft(0),
        marginRight(0),
        paddingTop(0),
        paddingBottom(0),
        paddingLeft(0),
        paddingRight(0),
        imageHeight(0),
        imageWidth(0),
        display(0),
        direction(0),
        verticalAlign(0),
        pageBreakBefore(0),
        pageBreakAfter(0),
        fontVariantCaps(0) {}

  [[nodiscard]] bool anySet() const {
    return textAlign || fontStyle || fontWeight || textDecoration || textIndent || marginTop || marginBottom ||
           marginLeft || marginRight || paddingTop || paddingBottom || paddingLeft || paddingRight || imageHeight ||
           imageWidth || display || direction || verticalAlign || pageBreakBefore || pageBreakAfter || fontVariantCaps;
  }

  void clearAll() {
    textAlign = fontStyle = fontWeight = textDecoration = textIndent = 0;
    marginTop = marginBottom = marginLeft = marginRight = 0;
    paddingTop = paddingBottom = paddingLeft = paddingRight = 0;
    imageHeight = imageWidth = display = direction = verticalAlign = pageBreakBefore = pageBreakAfter =
        fontVariantCaps = 0;
  }
};

// Cache serializes defined flags as uint32_t with bit indices 0..20.
static_assert(sizeof(CssPropertyFlags) <= sizeof(uint32_t),
              "CssPropertyFlags exceeds 32 bits; update cache read/write in CssParser.cpp");

// Represents a collection of CSS style properties
// Only stores properties relevant to e-ink text rendering
// Length values are stored as CssLength (value + unit) for deferred resolution
struct CssStyle {
  CssTextAlign textAlign = CssTextAlign::Left;
  CssFontStyle fontStyle = CssFontStyle::Normal;
  CssFontWeight fontWeight = CssFontWeight::Normal;
  CssFontVariantCaps fontVariantCaps = CssFontVariantCaps::Normal;
  CssTextDecoration textDecoration = CssTextDecoration::None;
  CssTextDirection direction = CssTextDirection::Ltr;

  CssLength textIndent;     // First-line indent (deferred resolution)
  CssLength marginTop;      // Vertical spacing before block
  CssLength marginBottom;   // Vertical spacing after block
  CssLength marginLeft;     // Horizontal spacing left of block
  CssLength marginRight;    // Horizontal spacing right of block
  CssLength paddingTop;     // Padding before
  CssLength paddingBottom;  // Padding after
  CssLength paddingLeft;    // Padding left
  CssLength paddingRight;   // Padding right
  CssLength imageHeight;    // Height for img (e.g. 2em) – width derived from aspect ratio when only height set
  CssLength imageWidth;     // Width for img when both or only width set
  CssDisplay display = CssDisplay::Block;                       // display property (Block or None)
  CssVerticalAlign verticalAlign = CssVerticalAlign::Baseline;  // vertical-align (super/sub positioning)
  CssPageBreak pageBreakBefore = CssPageBreak::Auto;
  CssPageBreak pageBreakAfter = CssPageBreak::Auto;

  CssPropertyFlags defined;  // Tracks which properties were explicitly set
  // Same property indices as the CSS cache (0..20). Keeping importance in one
  // mask adds only 4 bytes per interned style while making the cascade correct.
  uint32_t importantBits = 0;

  [[nodiscard]] bool isImportant(const uint8_t property) const {
    return property < 32 && (importantBits & (uint32_t{1} << property)) != 0;
  }

  void setImportant(const uint8_t property, const bool important) {
    if (property >= 32) return;
    const uint32_t bit = uint32_t{1} << property;
    importantBits = important ? importantBits | bit : importantBits & ~bit;
  }

  // Apply properties from another style, only overwriting if the other style
  // has that property explicitly defined
  void applyOver(const CssStyle& base) {
    if (base.hasTextAlign() && (base.isImportant(0) || !isImportant(0))) {
      textAlign = base.textAlign;
      defined.textAlign = 1;
      setImportant(0, base.isImportant(0));
    }
    if (base.hasFontStyle() && (base.isImportant(1) || !isImportant(1))) {
      fontStyle = base.fontStyle;
      defined.fontStyle = 1;
      setImportant(1, base.isImportant(1));
    }
    if (base.hasFontWeight() && (base.isImportant(2) || !isImportant(2))) {
      fontWeight = base.fontWeight;
      defined.fontWeight = 1;
      setImportant(2, base.isImportant(2));
    }
    if (base.hasFontVariantCaps() && (base.isImportant(20) || !isImportant(20))) {
      fontVariantCaps = base.fontVariantCaps;
      defined.fontVariantCaps = 1;
      setImportant(20, base.isImportant(20));
    }
    if (base.hasTextDecoration() && (base.isImportant(3) || !isImportant(3))) {
      textDecoration = base.textDecoration;
      defined.textDecoration = 1;
      setImportant(3, base.isImportant(3));
    }
    if (base.hasTextIndent() && (base.isImportant(4) || !isImportant(4))) {
      textIndent = base.textIndent;
      defined.textIndent = 1;
      setImportant(4, base.isImportant(4));
    }
    if (base.hasMarginTop() && (base.isImportant(5) || !isImportant(5))) {
      marginTop = base.marginTop;
      defined.marginTop = 1;
      setImportant(5, base.isImportant(5));
    }
    if (base.hasMarginBottom() && (base.isImportant(6) || !isImportant(6))) {
      marginBottom = base.marginBottom;
      defined.marginBottom = 1;
      setImportant(6, base.isImportant(6));
    }
    if (base.hasMarginLeft() && (base.isImportant(7) || !isImportant(7))) {
      marginLeft = base.marginLeft;
      defined.marginLeft = 1;
      setImportant(7, base.isImportant(7));
    }
    if (base.hasMarginRight() && (base.isImportant(8) || !isImportant(8))) {
      marginRight = base.marginRight;
      defined.marginRight = 1;
      setImportant(8, base.isImportant(8));
    }
    if (base.hasPaddingTop() && (base.isImportant(9) || !isImportant(9))) {
      paddingTop = base.paddingTop;
      defined.paddingTop = 1;
      setImportant(9, base.isImportant(9));
    }
    if (base.hasPaddingBottom() && (base.isImportant(10) || !isImportant(10))) {
      paddingBottom = base.paddingBottom;
      defined.paddingBottom = 1;
      setImportant(10, base.isImportant(10));
    }
    if (base.hasPaddingLeft() && (base.isImportant(11) || !isImportant(11))) {
      paddingLeft = base.paddingLeft;
      defined.paddingLeft = 1;
      setImportant(11, base.isImportant(11));
    }
    if (base.hasPaddingRight() && (base.isImportant(12) || !isImportant(12))) {
      paddingRight = base.paddingRight;
      defined.paddingRight = 1;
      setImportant(12, base.isImportant(12));
    }
    if (base.hasImageHeight() && (base.isImportant(13) || !isImportant(13))) {
      imageHeight = base.imageHeight;
      defined.imageHeight = 1;
      setImportant(13, base.isImportant(13));
    }
    if (base.hasImageWidth() && (base.isImportant(14) || !isImportant(14))) {
      imageWidth = base.imageWidth;
      defined.imageWidth = 1;
      setImportant(14, base.isImportant(14));
    }
    if (base.hasDisplay() && (base.isImportant(15) || !isImportant(15))) {
      display = base.display;
      defined.display = 1;
      setImportant(15, base.isImportant(15));
    }
    if (base.hasDirection() && (base.isImportant(16) || !isImportant(16))) {
      direction = base.direction;
      defined.direction = 1;
      setImportant(16, base.isImportant(16));
    }
    if (base.hasVerticalAlign() && (base.isImportant(17) || !isImportant(17))) {
      verticalAlign = base.verticalAlign;
      defined.verticalAlign = 1;
      setImportant(17, base.isImportant(17));
    }
    if (base.hasPageBreakBefore() && (base.isImportant(18) || !isImportant(18))) {
      pageBreakBefore = base.pageBreakBefore;
      defined.pageBreakBefore = 1;
      setImportant(18, base.isImportant(18));
    }
    if (base.hasPageBreakAfter() && (base.isImportant(19) || !isImportant(19))) {
      pageBreakAfter = base.pageBreakAfter;
      defined.pageBreakAfter = 1;
      setImportant(19, base.isImportant(19));
    }
  }

  [[nodiscard]] bool hasTextAlign() const { return defined.textAlign; }
  [[nodiscard]] bool hasFontStyle() const { return defined.fontStyle; }
  [[nodiscard]] bool hasFontWeight() const { return defined.fontWeight; }
  [[nodiscard]] bool hasFontVariantCaps() const { return defined.fontVariantCaps; }
  [[nodiscard]] bool hasTextDecoration() const { return defined.textDecoration; }
  [[nodiscard]] bool hasTextIndent() const { return defined.textIndent; }
  [[nodiscard]] bool hasMarginTop() const { return defined.marginTop; }
  [[nodiscard]] bool hasMarginBottom() const { return defined.marginBottom; }
  [[nodiscard]] bool hasMarginLeft() const { return defined.marginLeft; }
  [[nodiscard]] bool hasMarginRight() const { return defined.marginRight; }
  [[nodiscard]] bool hasPaddingTop() const { return defined.paddingTop; }
  [[nodiscard]] bool hasPaddingBottom() const { return defined.paddingBottom; }
  [[nodiscard]] bool hasPaddingLeft() const { return defined.paddingLeft; }
  [[nodiscard]] bool hasPaddingRight() const { return defined.paddingRight; }
  [[nodiscard]] bool hasImageHeight() const { return defined.imageHeight; }
  [[nodiscard]] bool hasImageWidth() const { return defined.imageWidth; }
  [[nodiscard]] bool hasDisplay() const { return defined.display; }
  [[nodiscard]] bool hasDirection() const { return defined.direction; }
  [[nodiscard]] bool hasVerticalAlign() const { return defined.verticalAlign; }
  [[nodiscard]] bool hasPageBreakBefore() const { return defined.pageBreakBefore; }
  [[nodiscard]] bool hasPageBreakAfter() const { return defined.pageBreakAfter; }

  void reset() {
    textAlign = CssTextAlign::Left;
    fontStyle = CssFontStyle::Normal;
    fontWeight = CssFontWeight::Normal;
    fontVariantCaps = CssFontVariantCaps::Normal;
    textDecoration = CssTextDecoration::None;
    direction = CssTextDirection::Ltr;
    textIndent = CssLength{};
    marginTop = marginBottom = marginLeft = marginRight = CssLength{};
    paddingTop = paddingBottom = paddingLeft = paddingRight = CssLength{};
    imageHeight = imageWidth = CssLength{};
    display = CssDisplay::Block;
    verticalAlign = CssVerticalAlign::Baseline;
    pageBreakBefore = CssPageBreak::Auto;
    pageBreakAfter = CssPageBreak::Auto;
    defined.clearAll();
    importantBits = 0;
  }
};
