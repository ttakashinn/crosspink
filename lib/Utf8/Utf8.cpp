#include "Utf8.h"

#include <array>

#include "Utf8ComposeTable.h"

namespace {
// Look up the canonical composition of (base + combining mark), or 0 if none.
uint32_t utf8ComposePair(const uint32_t base, const uint32_t mark) {
  if (base > 0xFFFF || mark > 0xFFFF) return 0;
  int lo = 0;
  int hi = kUtf8ComposeTableSize - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const Utf8ComposeEntry& e = kUtf8ComposeTable[mid];
    if (e.base < base || (e.base == base && e.mark < mark)) {
      lo = mid + 1;
    } else if (e.base > base || (e.base == base && e.mark > mark)) {
      hi = mid - 1;
    } else {
      return e.composed;
    }
  }
  return 0;
}

// Canonical combining classes relevant to Vietnamese, with one deliberate
// repair rule: circumflex/breve sort before tone marks even though they share
// class 230. Malformed EPUBs commonly emit "a + acute + circumflex"; strict NFC
// cannot compose that order, while readers still expect the Vietnamese glyph ấ.
uint8_t vietnameseMarkOrder(const uint32_t mark) {
  switch (mark) {
    case 0x031B:  // horn, CCC 216
      return 10;
    case 0x0323:  // dot below, CCC 220
      return 20;
    case 0x0302:  // circumflex, CCC 230 (Vietnamese shape mark)
    case 0x0306:  // breve, CCC 230 (Vietnamese shape mark)
      return 30;
    case 0x0300:  // grave, CCC 230 (tone)
    case 0x0301:  // acute, CCC 230 (tone)
    case 0x0303:  // tilde, CCC 230 (tone)
    case 0x0309:  // hook above, CCC 230 (tone)
      return 40;
    default:
      return 50;
  }
}

bool isLookupBoundary(const uint32_t cp) {
  if (cp <= 0x7F) {
    return !((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'));
  }
  if ((cp >= 0x80 && cp <= 0xA0) || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || (cp >= 0x2028 && cp <= 0x202F) ||
      cp == 0x205F || cp == 0x3000) {
    return true;
  }
  if (cp >= 0x00A1 && cp <= 0x00BF) return true;
  if ((cp >= 0x2000 && cp <= 0x2BFF) || (cp >= 0x2E00 && cp <= 0x2E7F) || (cp >= 0x3000 && cp <= 0x303F) ||
      (cp >= 0xFE10 && cp <= 0xFE6F) || (cp >= 0x1F000 && cp <= 0x1FAFF)) {
    return true;
  }
  if ((cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) ||
      (cp >= 0xFF5B && cp <= 0xFF65) || (cp >= 0xFFE0 && cp <= 0xFFEE)) {
    return true;
  }
  return cp == 0x037E || cp == 0x0387 || (cp >= 0x055A && cp <= 0x055F) || cp == 0x0589 || cp == 0x058A ||
         cp == 0x05BE || cp == 0x05C0 || cp == 0x05C3 || cp == 0x05C6 || (cp >= 0x0609 && cp <= 0x060D) ||
         cp == 0x061B || (cp >= 0x061D && cp <= 0x061F) || (cp >= 0x066A && cp <= 0x066D) || cp == 0x06D4 ||
         (cp >= 0x0700 && cp <= 0x070D) || cp == 0x0964 || cp == 0x0965;
}

bool isLookupCoreCharacter(const uint32_t cp) {
  return cp != 0 && cp != REPLACEMENT_GLYPH && !utf8IsCombiningMark(cp) && !isLookupBoundary(cp);
}
}  // namespace

std::string utf8ComposeNfc(std::string in) {
  // Fast path: NFC composition can only change text that contains a combining
  // diacritical mark U+0300-036F (UTF-8 lead byte 0xCC or 0xCD). Plain ASCII and
  // already-precomposed (NFC) text -- the vast majority of words -- have none, so
  // return them untouched without walking codepoints or allocating. A 0xCD that is
  // actually a non-combining codepoint just falls through to the full pass below.
  bool maybeHasMarks = false;
  for (const unsigned char c : in) {
    if (c == 0xCC || c == 0xCD) {
      maybeHasMarks = true;
      break;
    }
  }
  if (!maybeHasMarks) return in;

  std::string out;
  out.reserve(in.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in.c_str());
  uint32_t base = 0;
  bool haveBase = false;
  std::array<uint32_t, 8> marks{};
  size_t markCount = 0;

  const auto flushCluster = [&]() {
    if (!haveBase) return;

    // Stable insertion sort keeps unrelated marks in source order while
    // repairing the bounded Vietnamese subset above.
    for (size_t i = 1; i < markCount; ++i) {
      const uint32_t mark = marks[i];
      const uint8_t order = vietnameseMarkOrder(mark);
      size_t j = i;
      while (j > 0 && vietnameseMarkOrder(marks[j - 1]) > order) {
        marks[j] = marks[j - 1];
        --j;
      }
      marks[j] = mark;
    }

    std::array<uint32_t, 8> uncomposed{};
    size_t uncomposedCount = 0;
    uint32_t composedBase = base;
    for (size_t i = 0; i < markCount; ++i) {
      const uint32_t composed = utf8ComposePair(composedBase, marks[i]);
      if (composed != 0) {
        composedBase = composed;
      } else {
        uncomposed[uncomposedCount++] = marks[i];
      }
    }
    utf8AppendCodepoint(composedBase, out);
    for (size_t i = 0; i < uncomposedCount; ++i) utf8AppendCodepoint(uncomposed[i], out);
    haveBase = false;
    markCount = 0;
  };

  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (utf8IsCombiningMark(cp)) {
      if (!haveBase) {
        utf8AppendCodepoint(cp, out);
      } else if (markCount < marks.size()) {
        marks[markCount++] = cp;
      } else {
        // Pathological clusters stay bounded without dropping text.
        flushCluster();
        utf8AppendCodepoint(cp, out);
      }
    } else {
      flushCluster();
      base = cp;
      haveBase = true;
    }
  }
  flushCluster();
  return out;
}

std::string utf8CleanLookupWord(const std::string& text) {
  const auto* begin = reinterpret_cast<const unsigned char*>(text.c_str());
  const auto* cursor = begin;
  size_t firstCore = std::string::npos;
  size_t lastKeptEnd = 0;

  while (*cursor) {
    const auto* codepointStart = cursor;
    const uint32_t cp = utf8NextCodepoint(&cursor);
    if (isLookupCoreCharacter(cp)) {
      if (firstCore == std::string::npos) firstCore = static_cast<size_t>(codepointStart - begin);
      lastKeptEnd = static_cast<size_t>(cursor - begin);
    } else if (firstCore != std::string::npos && utf8IsCombiningMark(cp) &&
               static_cast<size_t>(codepointStart - begin) == lastKeptEnd) {
      lastKeptEnd = static_cast<size_t>(cursor - begin);
    }
  }

  if (firstCore == std::string::npos) return {};
  return utf8ComposeNfc(text.substr(firstCore, lastKeptEnd - firstCore));
}

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const unsigned char lead = **string;
  const int bytes = utf8CodepointLen(lead);
  const uint8_t* chr = *string;

  // Invalid lead byte (stray continuation byte 0x80-0xBF, or 0xFE/0xFF)
  if (bytes == 1 && lead >= 0x80) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  if (bytes == 1) {
    (*string)++;
    return chr[0];
  }

  // Validate continuation bytes before consuming them
  for (int i = 1; i < bytes; i++) {
    if ((chr[i] & 0xC0) != 0x80) {
      // Missing or invalid continuation byte — skip all bytes consumed so far
      *string += i;
      return REPLACEMENT_GLYPH;
    }
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  // Reject overlong encodings, surrogates, and out-of-range values
  const bool overlong = (bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) || (bytes == 4 && cp < 0x10000);
  const bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);
  if (overlong || surrogate || cp > 0x10FFFF) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  *string += bytes;

  return cp;
}

void utf8AppendCodepoint(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

int utf8SafeTruncateBuffer(const char* buf, int len) {
  if (len <= 0) return 0;

  // Walk back past continuation bytes (10xxxxxx) to find the lead byte
  int leadPos = len - 1;
  while (leadPos > 0 && (static_cast<uint8_t>(buf[leadPos]) & 0xC0) == 0x80) {
    leadPos--;
  }

  // Determine expected length of the sequence starting at leadPos
  int expectedLen = utf8CodepointLen(static_cast<unsigned char>(buf[leadPos]));
  int actualLen = len - leadPos;

  if (actualLen < expectedLen && leadPos > 0) {
    // Incomplete UTF-8 sequence at the end — exclude it
    return leadPos;
  }
  return len;
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

// Truncate string by removing N UTF-8 characters from the end
void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

bool utf8IsValid(const std::string_view text) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
  size_t offset = 0;
  while (offset < text.size()) {
    const uint8_t lead = bytes[offset++];
    if (lead < 0x80) continue;
    size_t count = 0;
    uint32_t codepoint = 0;
    uint32_t minimum = 0;
    if ((lead & 0xE0) == 0xC0) {
      count = 1;
      codepoint = lead & 0x1F;
      minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      count = 2;
      codepoint = lead & 0x0F;
      minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      count = 3;
      codepoint = lead & 0x07;
      minimum = 0x10000;
    } else {
      return false;
    }
    if (count > text.size() - offset) return false;
    for (size_t i = 0; i < count; ++i) {
      const uint8_t continuation = bytes[offset++];
      if ((continuation & 0xC0) != 0x80) return false;
      codepoint = (codepoint << 6) | (continuation & 0x3F);
    }
    if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;
  }
  return true;
}
