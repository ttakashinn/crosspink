#pragma once

#include <algorithm>
#include <cstddef>

namespace dictionary_review {

// Give the current section every line that remains after reserving a label,
// one body line and the inter-section gap for each later section that actually
// has content. This lets the last populated section consume the whole page.
inline int availableLinesForSection(const int currentY, const int bottomY, const int labelHeight, const int lineHeight,
                                    const int sectionGap, const size_t laterPopulatedSections) {
  if (lineHeight <= 0 || labelHeight < 0 || bottomY <= currentY) return 0;

  const int safeGap = std::max(0, sectionGap);
  const long long reservedForLater =
      static_cast<long long>(laterPopulatedSections) * (static_cast<long long>(labelHeight) + lineHeight + safeGap);
  const long long bodyHeight = static_cast<long long>(bottomY) - currentY - labelHeight - reservedForLater;
  return bodyHeight > 0 ? static_cast<int>(bodyHeight / lineHeight) : 0;
}

}  // namespace dictionary_review
