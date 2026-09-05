#pragma once

#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

enum class PanicStage : uint32_t {
  None = 0,
  ReaderRenderBegin = 1,
  ReaderPageLoaded = 2,
  ReaderScanBegin = 3,
  ReaderScanDone = 4,
  ReaderPrewarmDone = 5,
  ReaderRenderDone = 6,
};

struct PanicBreadcrumb {
  volatile uint32_t stage;
  volatile int32_t spine;
  volatile int32_t page;
};

extern PanicBreadcrumb panicBreadcrumb;

void setPanicBreadcrumb(PanicStage stage, int32_t spine, int32_t page);

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
}  // namespace HalSystem
