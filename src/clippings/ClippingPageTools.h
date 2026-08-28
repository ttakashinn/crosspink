#pragma once

#include <Epub/Page.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ClippingCodec.h"

class GfxRenderer;

namespace ClippingPageTools {

inline constexpr size_t MAX_WORDS_PER_PAGE = 512;

struct WordRef {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  uint16_t row = 0;
  uint16_t pageIndex = 0;
  const char* text = nullptr;
  uint16_t textLength = 0;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
};

bool isSelectableToken(const char* text, size_t length);
void collectWords(const Page& page, GfxRenderer& renderer, int fontId, int marginLeft, int marginTop,
                  std::vector<WordRef>& words, uint16_t& rowCount);
bool selectionText(const std::vector<WordRef>& words, uint16_t start, uint16_t end, std::string& text);

struct HighlightLine {
  int16_t left = 0;
  int16_t right = 0;
  int16_t top = 0;
  int16_t bottom = 0;
};

struct HighlightPlan {
  static constexpr size_t MAX_LINES = 128;
  std::array<HighlightLine, MAX_LINES> lines{};
  size_t count = 0;
  bool truncated = false;

  void drawUnderline(const GfxRenderer& renderer) const;
  void clearGrayscale(const GfxRenderer& renderer) const;
};

HighlightPlan buildHighlightPlan(GfxRenderer& renderer, const Page& page, int fontId, int marginLeft, int marginTop,
                                 const std::vector<ClippingCodec::Record>& records, uint16_t spineIndex,
                                 uint16_t pageIndex, uint32_t pageVisibleOffset);

}  // namespace ClippingPageTools
