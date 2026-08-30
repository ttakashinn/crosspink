#pragma once
#include <cstdint>

#include "EpubRenderMode.h"

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on every field: a section file built
// with a different spec is discarded and rebuilt.
//
// Build one via CrossPointSettings::readerRenderSpec(width, height), which
// fills every field: the settings-derived ones from the store, the viewport
// from the caller. Taking the viewport as arguments is what keeps a spec from
// existing in a half-filled state — the 0 defaults below are a last-resort
// backstop (a 0x0 viewport lays out nothing), not an invitation to omit it.
struct ReaderRenderSpec {
  int fontId = 0;
  float lineCompression = 1.0f;
  bool extraParagraphSpacing = false;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  uint8_t wordSpacing = 0;
  bool repairParagraphIndent = false;
  EpubRenderMode renderMode = EpubRenderMode::Standard;
};

inline ReaderRenderSpec applyEpubRenderMode(ReaderRenderSpec spec, const EpubRenderMode mode) {
  spec.renderMode = mode;
  if (mode == EpubRenderMode::Safe) {
    spec.embeddedStyle = false;
    spec.imageRendering = 1;  // Alt-text placeholder: skip extraction/decode during the recovery build.
  }
  return spec;
}
