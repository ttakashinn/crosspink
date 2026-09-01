#pragma once

#include <algorithm>

namespace dictionary_ui {

struct DefinitionHeaderLayout {
  int titleX = 0;
  int titleWidth = 0;
  int counterX = 0;
};

// Keep the matched headword inside the safe content band and reserve a fixed
// gap before the optional page counter. The caller truncates the headword to
// titleWidth using the renderer's UTF-8-safe truncation path.
constexpr DefinitionHeaderLayout definitionHeaderLayout(const int safeX, const int safeWidth, const int sidePadding,
                                                        const int counterWidth, const int titleCounterGap) {
  const int titleX = safeX + sidePadding;
  const int contentRight = safeX + std::max(0, safeWidth - sidePadding);
  const int counterX = std::max(titleX, contentRight - std::max(0, counterWidth));
  const int titleRight = counterWidth > 0 ? std::max(titleX, counterX - std::max(0, titleCounterGap)) : contentRight;
  return {titleX, std::max(0, titleRight - titleX), counterX};
}

}  // namespace dictionary_ui
