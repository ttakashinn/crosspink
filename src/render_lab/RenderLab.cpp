#include "RenderLab.h"

#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <esp_system.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"

namespace {

constexpr char OUTPUT_DIR[] = "/render-lab";
constexpr char PBM_PATH[] = "/render-lab/framebuffer.pbm";
constexpr char PGM_PATH[] = "/render-lab/framebuffer.pgm";
constexpr char RESULT_PATH[] = "/render-lab/result.json";

struct State {
  bool configured = false;
  bool anchorResolved = false;
  bool anchorResolutionRecorded = false;
  bool lsbValid = false;
  bool msbValid = false;
  unsigned long prewarmMs = 0;
  unsigned long bwRenderMs = 0;
  unsigned long totalMs = 0;
  uint16_t physicalWidth = 0;
  uint16_t physicalHeight = 0;
  uint16_t rowBytes = 0;
  std::vector<uint8_t> bw;
  std::vector<uint8_t> lsb;
  std::vector<uint8_t> msb;
  uint16_t tableCount = 0;
  uint16_t tableRowCount = 0;
  uint16_t tableGridRowCount = 0;
  uint16_t tableStackedRowCount = 0;
  uint16_t tableMaxColumns = 0;
  uint16_t tableWrappedCellCount = 0;
  uint16_t tableMaxCellLines = 0;
  uint16_t tablePageSplitRows = 0;
  uint16_t tablePageSplitCount = 0;
  uint16_t currentTableRowCells = 0;
  uint16_t currentTableRowStartPage = 0;
  bool currentTableRowActive = false;
  uint16_t imageCount = 0;
  uint16_t pngImageCount = 0;
  uint16_t jpegImageCount = 0;
  uint16_t scaledDownImageCount = 0;
  uint16_t outOfBoundsImageCount = 0;
  uint16_t imagePageCount = 0;
  uint16_t lastImagePage = UINT16_MAX;
  uint16_t maxImageSourceWidth = 0;
  uint16_t maxImageSourceHeight = 0;
  uint16_t maxImageDisplayWidth = 0;
  uint16_t maxImageDisplayHeight = 0;
};

State state;

const char* envOr(const char* name, const char* fallback = "") {
  const char* value = std::getenv(name);
  return value && value[0] != '\0' ? value : fallback;
}

bool envBool(const char* name, const bool fallback) {
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') return fallback;
  return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "yes") == 0;
}

bool tableMetricsEnabled() { return envBool("CROSSPOINT_RENDER_LAB_TABLE_METRICS", false); }

bool imageMetricsEnabled() { return envBool("CROSSPOINT_RENDER_LAB_IMAGE_METRICS", false); }

int envInt(const char* name, const int fallback) {
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  return end && *end == '\0' ? static_cast<int>(parsed) : fallback;
}

bool physicalBit(const std::vector<uint8_t>& plane, const int x, const int y, const uint16_t rowBytes) {
  if (plane.empty() || x < 0 || y < 0) return false;
  const size_t index = static_cast<size_t>(y) * rowBytes + static_cast<size_t>(x / 8);
  return index < plane.size() && (plane[index] & static_cast<uint8_t>(0x80u >> (x & 7))) != 0;
}

void logicalToPhysical(const GfxRenderer::Orientation orientation, const int logicalX, const int logicalY,
                       const uint16_t panelWidth, const uint16_t panelHeight, int& physicalX, int& physicalY) {
  switch (orientation) {
    case GfxRenderer::Portrait:
      physicalX = logicalY;
      physicalY = panelHeight - 1 - logicalX;
      return;
    case GfxRenderer::LandscapeClockwise:
      physicalX = panelWidth - 1 - logicalX;
      physicalY = panelHeight - 1 - logicalY;
      return;
    case GfxRenderer::PortraitInverted:
      physicalX = panelWidth - 1 - logicalY;
      physicalY = logicalX;
      return;
    case GfxRenderer::LandscapeCounterClockwise:
      physicalX = logicalX;
      physicalY = logicalY;
      return;
  }
}

bool writeAll(HalFile& file, const void* data, const size_t size) { return file.write(data, size) == size; }

bool writePbm(const GfxRenderer& renderer) {
  HalFile file;
  if (!Storage.openFileForWrite("RLB", PBM_PATH, file)) return false;

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const std::string header = "P4\n" + std::to_string(width) + " " + std::to_string(height) + "\n";
  if (!writeAll(file, header.data(), header.size())) return false;

  std::vector<uint8_t> row(static_cast<size_t>((width + 7) / 8));
  for (int y = 0; y < height; y++) {
    std::fill(row.begin(), row.end(), 0);
    for (int x = 0; x < width; x++) {
      int physicalX = 0;
      int physicalY = 0;
      logicalToPhysical(renderer.getOrientation(), x, y, state.physicalWidth, state.physicalHeight, physicalX,
                        physicalY);
      const bool white = physicalBit(state.bw, physicalX, physicalY, state.rowBytes);
      if (!white) row[static_cast<size_t>(x / 8)] |= static_cast<uint8_t>(0x80u >> (x & 7));
    }
    if (!writeAll(file, row.data(), row.size())) return false;
  }
  return file.sync();
}

uint8_t grayscaleLevel(const int physicalX, const int physicalY) {
  const bool baseWhite = physicalBit(state.bw, physicalX, physicalY, state.rowBytes);
  if (baseWhite) return 255;

  const bool lsb = state.lsbValid && physicalBit(state.lsb, physicalX, physicalY, state.rowBytes);
  const bool msb = state.msbValid && physicalBit(state.msb, physicalX, physicalY, state.rowBytes);
  if (msb) return lsb ? 96 : 200;
  return lsb ? 96 : 0;
}

bool writePgm(const GfxRenderer& renderer) {
  HalFile file;
  if (!Storage.openFileForWrite("RLB", PGM_PATH, file)) return false;

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const std::string header = "P5\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
  if (!writeAll(file, header.data(), header.size())) return false;

  std::vector<uint8_t> row(static_cast<size_t>(width));
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int physicalX = 0;
      int physicalY = 0;
      logicalToPhysical(renderer.getOrientation(), x, y, state.physicalWidth, state.physicalHeight, physicalX,
                        physicalY);
      row[static_cast<size_t>(x)] = grayscaleLevel(physicalX, physicalY);
    }
    if (!writeAll(file, row.data(), row.size())) return false;
  }
  return file.sync();
}

bool writeJsonResult(const JsonDocument& result) {
  HalFile file;
  if (!Storage.openFileForWrite("RLB", RESULT_PATH, file)) return false;
  std::vector<char> buffer(measureJsonPretty(result) + 1);
  const size_t written = serializeJsonPretty(result, buffer.data(), buffer.size());
  if (written == 0 || !writeAll(file, buffer.data(), written) || !writeAll(file, "\n", 1)) return false;
  return file.sync();
}

[[noreturn]] void failWithResult(const char* message) {
  JsonDocument result;
  result["schema_version"] = 1;
  result["status"] = "error";
  result["message"] = message;
  result["profile"] = envOr("CROSSPOINT_RENDER_LAB_PROFILE");
  result["checkpoint"] = envOr("CROSSPOINT_RENDER_LAB_CHECKPOINT");
  Storage.mkdir(OUTPUT_DIR);
  writeJsonResult(result);
  _exit(2);
}

}  // namespace

namespace render_lab {

bool enabled() { return envBool("CROSSPOINT_RENDER_LAB", false); }

const char* bookPath() { return envOr("CROSSPOINT_RENDER_LAB_BOOK", "/books/render-reference.epub"); }

const char* targetHref() { return envOr("CROSSPOINT_RENDER_LAB_HREF"); }

int targetPageOffset() { return std::max(0, envInt("CROSSPOINT_RENDER_LAB_PAGE_OFFSET", 0)); }

bool requiresFullBuild() { return envBool("CROSSPOINT_RENDER_LAB_FULL_BUILD", false); }

void configureSettings(CrossPointSettings& settings) {
  if (!enabled()) return;

  settings.orientation = CrossPointSettings::PORTRAIT;
  settings.textAntiAliasing = envBool("CROSSPOINT_RENDER_LAB_TEXT_AA", true) ? 1 : 0;
  settings.embeddedStyle = envBool("CROSSPOINT_RENDER_LAB_EMBEDDED_STYLES", true) ? 1 : 0;
  settings.fontFamily = CrossPointSettings::NOTOSERIF;
  settings.fontPointSize = static_cast<uint8_t>(envInt("CROSSPOINT_RENDER_LAB_FONT_SIZE", 14));
  settings.lineSpacing = CrossPointSettings::NORMAL;
  settings.paragraphAlignment = CrossPointSettings::JUSTIFIED;
  settings.screenMargin = static_cast<uint8_t>(envInt("CROSSPOINT_RENDER_LAB_SCREEN_MARGIN", 5));
  settings.extraParagraphSpacing = 1;
  settings.hyphenationEnabled = 0;
  settings.imageRendering = CrossPointSettings::IMAGES_DISPLAY;
  settings.focusReadingEnabled = 0;
  settings.screenInverted = 0;
  settings.fadingFix = 0;
  settings.statusBarClock = CrossPointSettings::STATUS_BAR_CLOCK_HIDE;
  settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
  settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
  settings.statusBarBattery = 0;
  settings.sdFontFamilyName[0] = '\0';
  state.configured = true;
}

void recordAnchorResolution(const char*, const bool resolved) {
  if (!enabled()) return;
  state.anchorResolutionRecorded = true;
  state.anchorResolved = resolved;
}

void recordTableStarted() {
  if (!enabled() || !tableMetricsEnabled()) return;
  state.tableCount++;
  state.currentTableRowActive = false;
  state.currentTableRowCells = 0;
}

void recordTableRowStarted(const uint16_t pageIndex) {
  if (!enabled() || !tableMetricsEnabled()) return;
  state.currentTableRowActive = true;
  state.currentTableRowCells = 0;
  state.currentTableRowStartPage = pageIndex;
}

void recordTableCellStarted() {
  if (!enabled() || !tableMetricsEnabled() || !state.currentTableRowActive) return;
  state.currentTableRowCells++;
}

void recordTableRowFinished(const bool gridLayout, const uint16_t wrappedCells, const uint16_t maxCellLines,
                            const uint16_t pageIndexAfterRow) {
  if (!enabled() || !tableMetricsEnabled() || !state.currentTableRowActive || state.currentTableRowCells == 0) {
    state.currentTableRowActive = false;
    return;
  }

  state.tableRowCount++;
  gridLayout ? state.tableGridRowCount++ : state.tableStackedRowCount++;
  state.tableMaxColumns = std::max(state.tableMaxColumns, state.currentTableRowCells);
  state.tableWrappedCellCount = static_cast<uint16_t>(state.tableWrappedCellCount + wrappedCells);
  state.tableMaxCellLines = std::max(state.tableMaxCellLines, maxCellLines);
  if (pageIndexAfterRow > state.currentTableRowStartPage) {
    state.tablePageSplitRows++;
    state.tablePageSplitCount = static_cast<uint16_t>(
        state.tablePageSplitCount + static_cast<uint16_t>(pageIndexAfterRow - state.currentTableRowStartPage));
  }
  state.currentTableRowActive = false;
}

void recordImageLayout(const bool jpeg, const uint16_t sourceWidth, const uint16_t sourceHeight,
                       const uint16_t displayWidth, const uint16_t displayHeight, const int16_t x, const int16_t y,
                       const uint16_t viewportWidth, const uint16_t viewportHeight, const uint16_t pageIndex) {
  if (!enabled() || !imageMetricsEnabled()) return;
  state.imageCount++;
  jpeg ? state.jpegImageCount++ : state.pngImageCount++;
  if (displayWidth < sourceWidth || displayHeight < sourceHeight) state.scaledDownImageCount++;
  if (x < 0 || y < 0 || static_cast<uint32_t>(x) + displayWidth > viewportWidth ||
      static_cast<uint32_t>(y) + displayHeight > viewportHeight) {
    state.outOfBoundsImageCount++;
  }
  if (state.lastImagePage != pageIndex) {
    state.lastImagePage = pageIndex;
    state.imagePageCount++;
  }
  state.maxImageSourceWidth = std::max(state.maxImageSourceWidth, sourceWidth);
  state.maxImageSourceHeight = std::max(state.maxImageSourceHeight, sourceHeight);
  state.maxImageDisplayWidth = std::max(state.maxImageDisplayWidth, displayWidth);
  state.maxImageDisplayHeight = std::max(state.maxImageDisplayHeight, displayHeight);
}

void beginFrame(const GfxRenderer& renderer) {
  if (!enabled()) return;
  const uint8_t* framebuffer = renderer.getFrameBuffer();
  if (!framebuffer) failWithResult("framebuffer is unavailable");

  state.physicalWidth = renderer.getDisplayWidth();
  state.physicalHeight = renderer.getDisplayHeight();
  state.rowBytes = renderer.getDisplayWidthBytes();
  const size_t size = static_cast<size_t>(state.rowBytes) * state.physicalHeight;
  state.bw.assign(framebuffer, framebuffer + size);
  state.lsb.assign(size, 0);
  state.msb.assign(size, 0);
  state.lsbValid = false;
  state.msbValid = false;
}

void captureGrayscalePlaneStrip(const bool lsbPlane, const uint8_t* rows, const uint16_t yStart, const uint16_t numRows,
                                const uint16_t rowBytes) {
  if (!enabled() || !rows || rowBytes != state.rowBytes || yStart >= state.physicalHeight) return;
  const size_t copiedRows = std::min<size_t>(numRows, state.physicalHeight - yStart);
  auto& target = lsbPlane ? state.lsb : state.msb;
  if (target.empty()) return;
  std::memcpy(target.data() + static_cast<size_t>(yStart) * rowBytes, rows, copiedRows * rowBytes);
  (lsbPlane ? state.lsbValid : state.msbValid) = true;
}

void recordTimings(const unsigned long prewarmMs, const unsigned long bwRenderMs, const unsigned long totalMs) {
  if (!enabled()) return;
  state.prewarmMs = prewarmMs;
  state.bwRenderMs = bwRenderMs;
  state.totalMs = totalMs;
}

[[noreturn]] void complete(const GfxRenderer& renderer, const int spineIndex, const int pageIndex, const int pageCount,
                           const uint32_t visibleTextOffset) {
  if (!state.configured) failWithResult("render lab settings were not configured");
  if (!state.anchorResolutionRecorded || !state.anchorResolved) failWithResult("checkpoint anchor was not resolved");
  if (state.bw.empty()) failWithResult("no logical framebuffer was captured");

  Storage.mkdir(OUTPUT_DIR);
  if (!writePbm(renderer) || !writePgm(renderer)) failWithResult("failed to write framebuffer artifacts");

  JsonDocument result;
  result["schema_version"] = 1;
  result["status"] = "ok";
  result["profile"] = envOr("CROSSPOINT_RENDER_LAB_PROFILE");
  result["checkpoint"] = envOr("CROSSPOINT_RENDER_LAB_CHECKPOINT");
  result["href"] = targetHref();
  if (targetPageOffset() > 0) result["page_offset"] = targetPageOffset();
  result["cache_state"] = envOr("CROSSPOINT_RENDER_LAB_CACHE_STATE", "cold");
  result["firmware_version"] = CROSSPOINT_VERSION;
  result["epub_sha256"] = envOr("CROSSPOINT_RENDER_LAB_EPUB_SHA256");
  result["orientation"] = "portrait";
  result["logical_width"] = renderer.getScreenWidth();
  result["logical_height"] = renderer.getScreenHeight();
  result["physical_width"] = state.physicalWidth;
  result["physical_height"] = state.physicalHeight;
  result["spine_index"] = spineIndex;
  result["page_index"] = pageIndex;
  result["page_count"] = pageCount;
  result["visible_text_offset"] = visibleTextOffset;
  result["font_family"] = "notoserif";
  result["font_point_size"] = envInt("CROSSPOINT_RENDER_LAB_FONT_SIZE", 14);
  result["text_antialiasing"] = envBool("CROSSPOINT_RENDER_LAB_TEXT_AA", true);
  result["embedded_styles"] = envBool("CROSSPOINT_RENDER_LAB_EMBEDDED_STYLES", true);
  result["grayscale_lsb_captured"] = state.lsbValid;
  result["grayscale_msb_captured"] = state.msbValid;
  result["prewarm_ms"] = state.prewarmMs;
  result["bw_render_ms"] = state.bwRenderMs;
  result["total_render_ms"] = state.totalMs;
  result["reported_min_free_heap"] = ESP.getMinFreeHeap();
  result["reported_max_alloc_heap"] = ESP.getMaxAllocHeap();
  result["framebuffer_pbm"] = "framebuffer.pbm";
  result["framebuffer_pgm"] = "framebuffer.pgm";
  if (state.tableCount > 0) {
    JsonObject tableLayout = result["table_layout"].to<JsonObject>();
    tableLayout["tables"] = state.tableCount;
    tableLayout["rows"] = state.tableRowCount;
    tableLayout["grid_rows"] = state.tableGridRowCount;
    tableLayout["stacked_rows"] = state.tableStackedRowCount;
    tableLayout["max_columns"] = state.tableMaxColumns;
    tableLayout["wrapped_cells"] = state.tableWrappedCellCount;
    tableLayout["max_cell_lines"] = state.tableMaxCellLines;
    tableLayout["page_split_rows"] = state.tablePageSplitRows;
    tableLayout["page_splits"] = state.tablePageSplitCount;
  }
  if (state.imageCount > 0) {
    JsonObject imageLayout = result["image_layout"].to<JsonObject>();
    imageLayout["images"] = state.imageCount;
    imageLayout["png_images"] = state.pngImageCount;
    imageLayout["jpeg_images"] = state.jpegImageCount;
    imageLayout["scaled_down"] = state.scaledDownImageCount;
    imageLayout["out_of_bounds"] = state.outOfBoundsImageCount;
    imageLayout["image_pages"] = state.imagePageCount;
    imageLayout["max_source_width"] = state.maxImageSourceWidth;
    imageLayout["max_source_height"] = state.maxImageSourceHeight;
    imageLayout["max_display_width"] = state.maxImageDisplayWidth;
    imageLayout["max_display_height"] = state.maxImageDisplayHeight;
  }

  if (!writeJsonResult(result)) failWithResult("failed to write result manifest");
  _exit(0);
}

}  // namespace render_lab

#endif
