#pragma once

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)

#include <cstdint>

class CrossPointSettings;
class GfxRenderer;

namespace render_lab {

bool enabled();
const char* bookPath();
const char* targetHref();

void configureSettings(CrossPointSettings& settings);
void recordAnchorResolution(const char* anchor, bool resolved);
void beginFrame(const GfxRenderer& renderer);
void captureGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows,
                                uint16_t rowBytes);
void recordTimings(unsigned long prewarmMs, unsigned long bwRenderMs, unsigned long totalMs);

[[noreturn]] void complete(const GfxRenderer& renderer, int spineIndex, int pageIndex, int pageCount,
                           uint32_t visibleTextOffset);

}  // namespace render_lab

#endif
