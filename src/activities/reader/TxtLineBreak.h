#pragma once

#include <Utf8.h>

#include <cstddef>
#include <cstdint>

namespace txt_layout {

struct LineSlice {
  size_t length = 0;
  size_t consumed = 0;
};

struct LogicalLine {
  size_t contentLength = 0;
  size_t delimiterLength = 0;
  bool complete = false;
};

inline bool isContinuationByte(const char value) { return (static_cast<uint8_t>(value) & 0xC0U) == 0x80U; }

inline size_t previousCodepointBoundary(const char* text, size_t boundary) {
  if (boundary == 0) return 0;
  --boundary;
  while (boundary > 0 && isContinuationByte(text[boundary])) --boundary;
  return boundary;
}

inline size_t firstCodepointLength(const char* text, const size_t length) {
  if (length == 0) return 0;
  size_t end = 1;
  while (end < length && isContinuationByte(text[end])) ++end;
  return end;
}

inline size_t safeReadChunkLength(const char* text, size_t length, const bool atEof) {
  if (atEof || length == 0) return length;
  return static_cast<size_t>(utf8SafeTruncateBuffer(text, static_cast<int>(length)));
}

// Finds one logical line without assuming a single newline convention. A CR
// at the end of a non-final read is accepted as a complete delimiter; the
// caller can consume a following LF with a one-byte lookahead before recording
// the next page offset.
inline LogicalLine scanLogicalLine(const char* text, const size_t length, const bool atEof) {
  for (size_t i = 0; i < length; ++i) {
    if (text[i] == '\n') return {i, 1, true};
    if (text[i] == '\r') {
      const size_t delimiterLength = (i + 1 < length && text[i + 1] == '\n') ? 2U : 1U;
      return {i, delimiterLength, true};
    }
  }
  return {length, 0, atEof};
}

// Returns one display slice and the number of source bytes to consume. The
// caller supplies a span-aware width function, so the firmware can measure a
// temporarily NUL-terminated view of its read buffer without allocating a
// substring on every candidate break.
template <typename Measure>
LineSlice nextLine(const char* text, const size_t length, const int maxWidth, Measure&& measure) {
  if (length == 0) return {};
  if (measure(text, length) <= maxWidth) return {length, length};

  size_t breakPos = length;
  while (breakPos > 0 && measure(text, breakPos) > maxWidth) {
    size_t spacePos = breakPos;
    while (spacePos > 0 && text[spacePos - 1] != ' ') --spacePos;
    if (spacePos > 1) {
      breakPos = spacePos - 1;
    } else {
      breakPos = previousCodepointBoundary(text, breakPos);
    }
  }

  if (breakPos == 0) breakPos = firstCodepointLength(text, length);
  size_t consumed = breakPos;
  if (consumed < length && text[consumed] == ' ') ++consumed;
  return {breakPos, consumed};
}

}  // namespace txt_layout
