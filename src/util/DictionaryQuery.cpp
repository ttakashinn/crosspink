#include "DictionaryQuery.h"

#include <Utf8.h>

#include <algorithm>
#include <array>

namespace DictionaryQuery {

std::string clean(const std::string_view text) {
  const std::string normalized = utf8CleanLookupWord(std::string(text));
  if (normalized.empty()) return {};

  std::string result;
  result.reserve(normalized.size());
  const auto* cursor = reinterpret_cast<const unsigned char*>(normalized.c_str());
  while (*cursor) utf8AppendCodepoint(utf8LowerVietnamese(utf8NextCodepoint(&cursor)), result);
  return result;
}

uint8_t editDistance(const std::string_view left, const std::string_view right, const uint8_t maxDistance) {
  static constexpr size_t MAX_CODEPOINTS = 64;
  std::array<uint32_t, MAX_CODEPOINTS> leftCodepoints{};
  std::array<uint32_t, MAX_CODEPOINTS> rightCodepoints{};
  const auto decode = [](const std::string_view text, auto& output, size_t& count) {
    if (!utf8IsValid(text)) return false;
    size_t offset = 0;
    while (offset < text.size()) {
      if (count >= output.size()) return false;
      const uint8_t lead = static_cast<uint8_t>(text[offset++]);
      uint32_t codepoint = lead;
      uint8_t continuationCount = 0;
      if ((lead & 0xE0) == 0xC0) {
        codepoint = lead & 0x1F;
        continuationCount = 1;
      } else if ((lead & 0xF0) == 0xE0) {
        codepoint = lead & 0x0F;
        continuationCount = 2;
      } else if ((lead & 0xF8) == 0xF0) {
        codepoint = lead & 0x07;
        continuationCount = 3;
      }
      for (uint8_t i = 0; i < continuationCount; ++i) {
        codepoint = (codepoint << 6) | (static_cast<uint8_t>(text[offset++]) & 0x3F);
      }
      output[count++] = utf8LowerVietnamese(codepoint);
    }
    return true;
  };
  size_t leftCount = 0;
  size_t rightCount = 0;
  if (!decode(left, leftCodepoints, leftCount) || !decode(right, rightCodepoints, rightCount)) {
    return static_cast<uint8_t>(maxDistance + 1);
  }
  const size_t lengthDelta = leftCount > rightCount ? leftCount - rightCount : rightCount - leftCount;
  if (lengthDelta > maxDistance) return static_cast<uint8_t>(maxDistance + 1);

  std::array<uint8_t, MAX_CODEPOINTS + 1> previous{};
  std::array<uint8_t, MAX_CODEPOINTS + 1> current{};
  for (size_t j = 0; j <= rightCount; ++j) previous[j] = static_cast<uint8_t>(j);
  for (size_t i = 1; i <= leftCount; ++i) {
    current[0] = static_cast<uint8_t>(i);
    uint8_t rowMinimum = current[0];
    for (size_t j = 1; j <= rightCount; ++j) {
      const uint8_t substitution =
          static_cast<uint8_t>(previous[j - 1] + (leftCodepoints[i - 1] == rightCodepoints[j - 1] ? 0 : 1));
      current[j] =
          std::min({static_cast<uint8_t>(previous[j] + 1), static_cast<uint8_t>(current[j - 1] + 1), substitution});
      rowMinimum = std::min(rowMinimum, current[j]);
    }
    if (rowMinimum > maxDistance) return static_cast<uint8_t>(maxDistance + 1);
    previous.swap(current);
  }
  return previous[rightCount] <= maxDistance ? previous[rightCount] : static_cast<uint8_t>(maxDistance + 1);
}

}  // namespace DictionaryQuery
