#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "Epub/css/CssStyle.h"

/**
 * BlockStyle - Block-level styling properties
 */
struct BlockStyle {
  // Upper bound (in em) for any single side's horizontal margin or padding.
  // Some EPUBs apply huge em-based insets to chapter-opener classes; without a
  // cap, effectiveWidth collapses to 1-2 words per line and justification dumps
  // the remaining space into a single gap.
  static constexpr float MAX_HORIZONTAL_INSET_EM = 2.0f;
  static constexpr float MAX_VERTICAL_SPACING_EM = 4.0f;

  CssTextAlign alignment = CssTextAlign::Justify;

  // Spacing (in pixels)
  int16_t marginTop = 0;
  int16_t marginBottom = 0;
  int16_t marginLeft = 0;
  int16_t marginRight = 0;
  int16_t paddingTop = 0;     // treated same as margin for rendering
  int16_t paddingBottom = 0;  // treated same as margin for rendering
  int16_t paddingLeft = 0;    // treated same as margin for rendering
  int16_t paddingRight = 0;   // treated same as margin for rendering
  int16_t textIndent = 0;
  bool textIndentDefined = false;  // true if text-indent was explicitly set in CSS
  bool textAlignDefined = false;   // true if text-align was explicitly set in CSS
  bool isRtl = false;              // true if resolved direction is RTL
  bool directionDefined = false;   // true if direction was explicitly set in CSS/HTML

  // Set when this block was created by a <br> element. Used by startNewTextBlock to inject
  // a full line-height gap when the <br> block stays empty (section-break use case).
  // NOT propagated through getCombinedBlockStyle so it can't leak into sibling blocks.
  bool fromBrElement = false;

  struct HorizontalLayout {
    int16_t xOffset = 0;
    uint16_t contentWidth = 0;
  };

  // Combined insets (margin + padding)
  // Use a wider type here: nested block styles can legitimately add several
  // int16_t values, and wrapping a positive inset negative makes text render
  // outside the viewport.
  [[nodiscard]] int32_t leftInset() const {
    return static_cast<int32_t>(marginLeft) + static_cast<int32_t>(paddingLeft);
  }
  [[nodiscard]] int32_t rightInset() const {
    return static_cast<int32_t>(marginRight) + static_cast<int32_t>(paddingRight);
  }
  [[nodiscard]] int32_t totalHorizontalInset() const { return leftInset() + rightInset(); }
  [[nodiscard]] int32_t topInset() const { return static_cast<int32_t>(marginTop) + static_cast<int32_t>(paddingTop); }
  [[nodiscard]] int32_t bottomInset() const {
    return static_cast<int32_t>(marginBottom) + static_cast<int32_t>(paddingBottom);
  }

  // Resolve author-provided horizontal spacing to a viewport-safe content box.
  // Negative margins are not allowed to move glyphs off-screen. If nested CSS
  // requests more inset than the viewport can afford, preserve the left/right
  // ratio while retaining minimumContentWidth for readable text.
  [[nodiscard]] HorizontalLayout resolveHorizontalLayout(const uint16_t viewportWidth,
                                                         const uint16_t minimumContentWidth = 1) const {
    if (viewportWidth == 0) return {};

    const uint32_t minimumWidth = std::clamp<uint32_t>(minimumContentWidth, 1, viewportWidth);
    const uint32_t insetBudget = static_cast<uint32_t>(viewportWidth) - minimumWidth;
    uint32_t left = static_cast<uint32_t>(std::max<int32_t>(0, leftInset()));
    uint32_t right = static_cast<uint32_t>(std::max<int32_t>(0, rightInset()));
    const uint32_t total = left + right;

    if (total > insetBudget) {
      if (total == 0) {
        left = 0;
        right = 0;
      } else {
        left = static_cast<uint32_t>((static_cast<uint64_t>(left) * insetBudget + total / 2) / total);
        right = insetBudget - left;
      }
    }

    HorizontalLayout layout;
    layout.xOffset = static_cast<int16_t>(std::min<uint32_t>(left, std::numeric_limits<int16_t>::max()));
    layout.contentWidth = static_cast<uint16_t>(static_cast<uint32_t>(viewportWidth) - left - right);
    return layout;
  }

  // Return a copy with bottom margins/padding zeroed out.
  [[nodiscard]] BlockStyle withoutBottom() const {
    BlockStyle result = *this;
    result.marginBottom = 0;
    result.paddingBottom = 0;
    return result;
  }

  // Return a copy with top margins/padding zeroed out.
  [[nodiscard]] BlockStyle withoutTop() const {
    BlockStyle result = *this;
    result.marginTop = 0;
    result.paddingTop = 0;
    return result;
  }

  // Return a copy with bottom margins/padding collapsed (max) with the source's.
  // Uses CSS margin collapsing: adjacent parent-child margins resolve to the larger value.
  [[nodiscard]] BlockStyle addBottom(const BlockStyle& source) const {
    BlockStyle result = *this;
    result.marginBottom = std::max(marginBottom, source.marginBottom);
    result.paddingBottom = saturatedAdd(paddingBottom, source.paddingBottom);
    return result;
  }

  enum class CombineAxis : uint8_t {
    Horizontal = 1,  // margins left/right, padding left/right, text-align, text-indent
    Vertical = 2,    // margins top/bottom, padding top/bottom
  };

  // Combine this style's properties with a child style along the specified axis.
  // Properties on the other axis are kept from the child unchanged.
  [[nodiscard]] BlockStyle getCombinedBlockStyle(const BlockStyle& child, CombineAxis axis) const {
    BlockStyle result = child;

    if (axis == CombineAxis::Horizontal) {
      result.marginLeft = saturatedAdd(child.marginLeft, marginLeft);
      result.marginRight = saturatedAdd(child.marginRight, marginRight);
      result.paddingLeft = saturatedAdd(child.paddingLeft, paddingLeft);
      result.paddingRight = saturatedAdd(child.paddingRight, paddingRight);
      if (!child.textIndentDefined && textIndentDefined) {
        result.textIndent = textIndent;
        result.textIndentDefined = true;
      }
      if (!child.textAlignDefined && textAlignDefined) {
        result.alignment = alignment;
        result.textAlignDefined = true;
      }
    } else {
      result.marginTop = std::max(child.marginTop, marginTop);
      result.marginBottom = std::max(child.marginBottom, marginBottom);
      result.paddingTop = saturatedAdd(child.paddingTop, paddingTop);
      result.paddingBottom = saturatedAdd(child.paddingBottom, paddingBottom);
    }

    // Direction is not axis-specific. Inherit from parent when child doesn't define it.
    if (!child.directionDefined && directionDefined) {
      result.isRtl = isRtl;
      result.directionDefined = true;
    }

    // fromBrElement is consumed by startNewTextBlock when an empty <br> block
    // is merged with the following paragraph; never propagate it further.
    result.fromBrElement = false;
    return result;
  }

 private:
  [[nodiscard]] static int16_t saturatedAdd(const int16_t lhs, const int16_t rhs) {
    const int32_t sum = static_cast<int32_t>(lhs) + static_cast<int32_t>(rhs);
    return static_cast<int16_t>(
        std::clamp<int32_t>(sum, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()));
  }

 public:
  // Create a BlockStyle from CSS style properties, resolving CssLength values to pixels
  // emSize is the current font line height, used for em/rem unit conversion
  // paragraphAlignment is the user's paragraphAlignment setting preference
  static BlockStyle fromCssStyle(const CssStyle& cssStyle, const float emSize, const CssTextAlign paragraphAlignment,
                                 const uint16_t viewportWidth = 0) {
    BlockStyle blockStyle;
    const float vw = viewportWidth;
    const auto maxHorizontalInsetPx = static_cast<int16_t>(emSize * MAX_HORIZONTAL_INSET_EM);
    const auto maxVerticalSpacingPx = static_cast<int16_t>(emSize * MAX_VERTICAL_SPACING_EM);
    // Resolve all CssLength values to pixels using the current font's em size and viewport width
    blockStyle.marginTop = std::clamp(cssStyle.marginTop.toPixelsInt16(emSize, vw),
                                      static_cast<int16_t>(-maxVerticalSpacingPx), maxVerticalSpacingPx);
    blockStyle.marginBottom = std::clamp(cssStyle.marginBottom.toPixelsInt16(emSize, vw),
                                         static_cast<int16_t>(-maxVerticalSpacingPx), maxVerticalSpacingPx);
    blockStyle.marginLeft = std::clamp(cssStyle.marginLeft.toPixelsInt16(emSize, vw),
                                       static_cast<int16_t>(-maxHorizontalInsetPx), maxHorizontalInsetPx);
    blockStyle.marginRight = std::clamp(cssStyle.marginRight.toPixelsInt16(emSize, vw),
                                        static_cast<int16_t>(-maxHorizontalInsetPx), maxHorizontalInsetPx);

    blockStyle.paddingTop =
        std::clamp(cssStyle.paddingTop.toPixelsInt16(emSize, vw), static_cast<int16_t>(0), maxVerticalSpacingPx);
    blockStyle.paddingBottom =
        std::clamp(cssStyle.paddingBottom.toPixelsInt16(emSize, vw), static_cast<int16_t>(0), maxVerticalSpacingPx);
    blockStyle.paddingLeft =
        std::clamp(cssStyle.paddingLeft.toPixelsInt16(emSize, vw), static_cast<int16_t>(0), maxHorizontalInsetPx);
    blockStyle.paddingRight =
        std::clamp(cssStyle.paddingRight.toPixelsInt16(emSize, vw), static_cast<int16_t>(0), maxHorizontalInsetPx);

    // For textIndent: if it's a percentage we can't resolve (no viewport width),
    // leave textIndentDefined=false so the space-width fallback in resolveFirstLineIndent() is used
    if (cssStyle.hasTextIndent() && cssStyle.textIndent.isResolvable(vw)) {
      blockStyle.textIndent = std::clamp(cssStyle.textIndent.toPixelsInt16(emSize, vw),
                                         static_cast<int16_t>(-maxHorizontalInsetPx), maxHorizontalInsetPx);
      blockStyle.textIndentDefined = true;
    }
    blockStyle.textAlignDefined = cssStyle.hasTextAlign();
    // User setting overrides CSS, unless "Book's Style" alignment setting is selected
    if (paragraphAlignment == CssTextAlign::None) {
      blockStyle.alignment = blockStyle.textAlignDefined ? cssStyle.textAlign : CssTextAlign::Justify;
    } else {
      blockStyle.alignment = paragraphAlignment;
    }
    // RTL direction from CSS/HTML
    if (cssStyle.hasDirection()) {
      blockStyle.isRtl = (cssStyle.direction == CssTextDirection::Rtl);
      blockStyle.directionDefined = true;
    }
    return blockStyle;
  }
};
