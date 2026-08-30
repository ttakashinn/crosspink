#pragma once

#include <Epub.h>
#include <Epub/PageLink.h>
#include <Logging.h>

#include <optional>
#include <vector>

#include "ProgressFile.h"

namespace EpubReaderUtils {

// Persists reader progress for an EPUB to its cache directory. Returns true on success.
inline bool saveProgress(const Epub& epub, int spineIndex, int pageNumber, int pageCount,
                         std::optional<uint32_t> visibleTextOffset = std::nullopt) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  uint8_t data[10];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = pageNumber & 0xFF;
  data[3] = (pageNumber >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  size_t dataSize = 6;
  if (visibleTextOffset.has_value()) {
    data[6] = *visibleTextOffset & 0xFF;
    data[7] = (*visibleTextOffset >> 8) & 0xFF;
    data[8] = (*visibleTextOffset >> 16) & 0xFF;
    data[9] = (*visibleTextOffset >> 24) & 0xFF;
    dataSize = sizeof(data);
  }
  if (!ProgressFile::writeAtomic(epub.getCachePath(), data, dataSize)) {
    return false;
  }
  LOG_DBG("ERS", "Progress saved: spine=%d offset=%u page=%d", spineIndex, visibleTextOffset.value_or(0), pageNumber);
  return true;
}

inline const PageLink* linkAtPoint(const std::vector<PageLink>& links, const int x, const int y, const int marginLeft,
                                   const int marginTop) {
  constexpr int TOUCH_SLOP = 6;
  constexpr int MIN_TOUCH_WIDTH = 28;
  const int pageX = x - marginLeft;
  const int pageY = y - marginTop;
  const PageLink* best = nullptr;
  uint64_t bestScore = UINT64_MAX;
  for (const auto& link : links) {
    const uint64_t score = link.touchScore(pageX, pageY, TOUCH_SLOP, MIN_TOUCH_WIDTH);
    if (score < bestScore) {
      best = &link;
      bestScore = score;
    }
  }
  return best;
}

}  // namespace EpubReaderUtils
