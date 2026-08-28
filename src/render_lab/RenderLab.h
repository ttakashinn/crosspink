#pragma once

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)

#include <cstdint>

class CrossPointSettings;
class GfxRenderer;

namespace render_lab {

bool enabled();
const char* bookPath();
const char* targetHref();
int targetPageOffset();
bool requiresFullBuild();

void configureSettings(CrossPointSettings& settings);
void recordAnchorResolution(const char* anchor, bool resolved);
void recordTableStarted();
void recordTableRowStarted(uint16_t pageIndex);
void recordTableCellStarted();
void recordTableRowFinished(bool gridLayout, uint16_t wrappedCells, uint16_t maxCellLines, uint16_t pageIndexAfterRow);
void recordImageLayout(bool jpeg, uint16_t sourceWidth, uint16_t sourceHeight, uint16_t displayWidth,
                       uint16_t displayHeight, int16_t x, int16_t y, uint16_t viewportWidth, uint16_t viewportHeight,
                       uint16_t pageIndex);
void beginFrame(const GfxRenderer& renderer);
void captureGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows,
                                uint16_t rowBytes);
void recordTimings(unsigned long prewarmMs, unsigned long bwRenderMs, unsigned long totalMs);

[[noreturn]] void complete(const GfxRenderer& renderer, int spineIndex, int pageIndex, int pageCount,
                           uint32_t visibleTextOffset);

}  // namespace render_lab

#endif
