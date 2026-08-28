#pragma once

#include <cstdint>

namespace smallcaps {

// Return the single-codepoint uppercase form used to synthesize small caps.
// Multi-codepoint expansions (for example German sharp s) are intentionally
// excluded: replacing one source codepoint with multiple glyphs would break
// EPUB offsets and the renderer's fixed per-codepoint layout contract.
inline bool uppercaseCodepoint(const uint32_t cp, uint32_t& uppercase) {
  if (cp >= 'a' && cp <= 'z') {
    uppercase = cp - ('a' - 'A');
    return true;
  }

  // Latin-1 Supplement. U+00F7 is division, not a letter.
  if ((cp >= 0x00E0 && cp <= 0x00F6) || (cp >= 0x00F8 && cp <= 0x00FE)) {
    uppercase = cp - 0x20;
    return true;
  }
  if (cp == 0x00FF) {
    uppercase = 0x0178;  // ÿ -> Ÿ
    return true;
  }

  // Latin Extended-A has several parity changes, so do not treat the whole
  // block as alternating odd/even pairs. These ranges cover Vietnamese ă/đ
  // and the other simple one-codepoint Latin pairs shipped by the fonts.
  if ((cp >= 0x0101 && cp <= 0x012F && (cp & 1u) != 0) || (cp >= 0x0133 && cp <= 0x0137 && (cp & 1u) != 0) ||
      (cp >= 0x013A && cp <= 0x0148 && (cp & 1u) == 0) || cp == 0x014B ||
      (cp >= 0x014D && cp <= 0x0177 && (cp & 1u) != 0) || cp == 0x017A || cp == 0x017C || cp == 0x017E) {
    uppercase = cp - 1;
    return true;
  }
  if (cp == 0x0131) {
    uppercase = 'I';  // dotless i
    return true;
  }

  // Vietnamese horn letters are outside Extended-A and have opposite parity.
  if (cp == 0x01A1) {
    uppercase = 0x01A0;  // ơ -> Ơ
    return true;
  }
  if (cp == 0x01B0) {
    uppercase = 0x01AF;  // ư -> Ư
    return true;
  }

  // Latin Extended Additional contains every precomposed Vietnamese tone
  // pair from ạ/Ạ through ỹ/Ỹ; lowercase codepoints are odd.
  if (cp >= 0x1EA1 && cp <= 0x1EF9 && (cp & 1u) != 0) {
    uppercase = cp - 1;
    return true;
  }

  // Greek and Cyrillic simple pairs retained from CrossInk's implementation.
  if (cp == 0x03C2) {
    uppercase = 0x03A3;  // final sigma
    return true;
  }
  if (cp >= 0x03B1 && cp <= 0x03C9 && cp != 0x03C2) {
    uppercase = cp - 0x20;
    return true;
  }
  if (cp >= 0x0430 && cp <= 0x044F) {
    uppercase = cp - 0x20;
    return true;
  }
  if (cp >= 0x0450 && cp <= 0x045F) {
    uppercase = cp - 0x50;
    return true;
  }
  if (cp == 0x0491) {
    uppercase = 0x0490;
    return true;
  }

  uppercase = cp;
  return false;
}

inline int32_t scaleAdvance(const int32_t advanceFp) {
  return advanceFp >= 0 ? (advanceFp * 3 + 2) / 4 : (advanceFp * 3 - 2) / 4;
}

inline int scaleMetric(const int value) { return value * 3 / 4; }
inline int scaleExtent(const int value) { return (value * 3 + 3) / 4; }

}  // namespace smallcaps
