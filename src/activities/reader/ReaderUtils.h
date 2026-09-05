#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <Memory.h>
#include <components/bars/tap-zones.h>
#include <esp_system.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "util/AdaptiveGrayscaleStrip.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

enum ReaderTouchAction : freeink::ui::ActionId {
  READER_TOUCH_PREV = 1,
  READER_TOUCH_NEXT = 3,
};

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedBack();
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const auto pageButtonTriggered = [&](const MappedInputManager::Button button) {
    if (usePress) return input.wasPressed(button);
    return input.wasLongPressed(button, SKIP_HOLD_MS) || input.wasReleased(button);
  };
  const bool prev =
      tiltPrev || (pageButtonTriggered(MappedInputManager::Button::PageBack) || pageButtonTriggered(prevButton));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = tiltNext || pageButtonTriggered(MappedInputManager::Button::PageForward) || powerTurn ||
                    pageButtonTriggered(nextButton);
  return {prev, next, tiltPrev || tiltNext};
}

struct TouchPageTurn {
  bool prev;
  bool next;
  unsigned long heldMs;
};

inline TouchPageTurn detectTouchPageTurn(GfxRenderer& renderer, const MappedInputManager& input) {
  TouchPageTurn result{false, false, 0};
  if (!SETTINGS.touchReaderControls || !input.hasTouch()) {
    return result;
  }

  if (SETTINGS.touchReaderControls == CrossPointSettings::TOUCH_READER_SWIPE) {
    // Horizontal swipes turn pages; taps remain free for the centered reader-menu
    // zone. A slow swipe never becomes a long-press chapter skip.
    const auto dir = input.wasSwipe();
    if (dir == MappedInputManager::SwipeDir::Left) {
      result.next = true;
    } else if (dir == MappedInputManager::SwipeDir::Right) {
      result.prev = true;
    }
    return result;
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) {
    return result;
  }

  const int16_t width = static_cast<int16_t>(renderer.getScreenWidth());
  const int16_t height = static_cast<int16_t>(renderer.getScreenHeight());
  // Outer thirds only: the center column contains the reader-menu tap target
  // (isTouchMenuTap below), so it must not double as a page turn.
  const int16_t zoneWidth = width / 3;
  const bool inverted = SETTINGS.touchReaderControls == CrossPointSettings::TOUCH_READER_INVERTED_TAP;
  const freeink::ui::TapZone zones[] = {
      {freeink::ui::Rect{0, 0, zoneWidth, height}, inverted ? READER_TOUCH_NEXT : READER_TOUCH_PREV},
      {freeink::ui::Rect{static_cast<int16_t>(width - zoneWidth), 0, zoneWidth, height},
       inverted ? READER_TOUCH_PREV : READER_TOUCH_NEXT},
  };

  for (const auto& zone : zones) {
    if (!zone.enabled || !zone.rect.contains(static_cast<int16_t>(x), static_cast<int16_t>(y))) continue;
    result.prev = zone.action == READER_TOUCH_PREV;
    result.next = zone.action == READER_TOUCH_NEXT;
    break;
  }
  result.heldMs = gpio.lastTouchHeldMs();
  return result;
}

// Tap in the center third of the screen: the tap path into the reader menu on
// every touch board. The page-turn tap zones are the outer horizontal thirds,
// so the centered rectangle remains free in tap mode. The Off/Swipe Up
// alternatives are only surfaced on home-key boards (SettingsList), where the
// menu stays reachable through the key's long-press function.
inline bool isTouchMenuTap(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (SETTINGS.showReaderMenu != CrossPointSettings::READER_MENU_TAP) return false;
  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) return false;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int zoneWidth = width / 3;
  const int zoneHeight = height / 3;
  return x >= zoneWidth && x < width - zoneWidth && y >= zoneHeight && y < height - zoneHeight;
}

// Reader menu opens on the menu edge-swipe or a center-third tap. On home-key
// boards a long press of the capacitive key runs the user-selected long-press
// function instead (SETTINGS.longPressMenuFunction), not the menu.
// With touch reader controls Off the reading surface ignores touch entirely,
// menu included, so a stray brush of the screen can't open it; the menu stays
// reachable via the Confirm button.
inline bool isTouchMenuGesture(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!SETTINGS.touchReaderControls) return false;
  if (!input.hasTouch()) return false;
  if (input.wasMenuGesture()) return true;
  // Bottom-edge up-swipe variant: only selectable on home-key boards, where
  // Home is the capacitive key and the bottom edge is otherwise unused.
  if (SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_SWIPE_UP && input.wasReaderMenuSwipeUp()) {
    return true;
  }
  return isTouchMenuTap(renderer, input);
}

// One helper, blocking or deferred: the async form starts the refresh and
// returns so the caller can overlap CPU work with the panel's refresh time.
// Async callers must not touch the framebuffer until
// renderer.waitRefreshComplete() and must rebuild the differential baseline
// before the next page turn (the tiled grayscale cleanup does).
inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh, bool async = false) {
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  if (async) {
    renderer.displayBufferAsync(mode);
  } else {
    renderer.displayBuffer(mode);
  }
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
}

// Display the B/W base of a page whose grayscale pass follows. Panels that
// combine the base (Paper Mono) defer the activation so base + gray planes go
// out as one waveform — displaying the base separately makes the gray pass
// re-drive the whole text body (a visible flash). Other panels display
// normally. Same refresh-cadence bookkeeping as displayWithRefreshCycle.
inline void displayBaseWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (!renderer.combinesGrayscaleBase()) {
    displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
    return;
  }
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  renderer.displayGrayscaleBase(mode);
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
}

// Finish any B/W base refresh before controller RAM is repurposed for gray
// selector planes. Request the X3 settle hook only when planes will follow and
// the panel did not defer/combine its own base waveform. Other panels keep
// their established driver path because preconditionGrayscale() is a no-op
// there.
inline void prepareGrayscalePlanes(const GfxRenderer& renderer, const bool planesAvailable, const bool combinedBase) {
  renderer.waitRefreshComplete();
  if (grayscale_pass::shouldPrecondition(planesAvailable, combinedBase)) {
    renderer.preconditionGrayscale();
  }
}

// Complete grayscale output when drawText() produced both selector planes in
// the same pass as the B/W base. The base framebuffer stays intact, so no B/W
// snapshot/restore is needed and the established driver waveform is unchanged.
inline bool displayCapturedTextAntiAliased(GfxRenderer& renderer) {
  prepareGrayscalePlanes(renderer, true, renderer.combinesGrayscaleBase());
  renderer.copyCapturedTextGrayscaleBuffers();
  renderer.displayGrayBuffer();
  renderer.cleanupGrayscaleWithFrameBuffer();
  return true;
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
bool renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (renderer.supportsStripGrayscale()) {
    const int displayHeight = renderer.getDisplayHeight();
    const int rowBytes = renderer.getDisplayWidthBytes();
    const uint32_t freeBefore = ESP.getFreeHeap();
    const uint32_t largestBefore = ESP.getMaxAllocHeap();
    uint16_t stripRows = 0;
    auto scratch = adaptive_grayscale_strip::allocate(static_cast<size_t>(rowBytes), stripRows, [](const size_t bytes) {
      return makeUniqueNoThrow<uint8_t[]>(bytes);
    });

    prepareGrayscalePlanes(renderer, scratch != nullptr, renderer.combinesGrayscaleBase());
    if (!scratch) {
      LOG_ERR("READER", "Grayscale applied=0 path=strip reason=scratch-oom free=%u largest=%u min_rows=%u min_bytes=%u",
              static_cast<unsigned>(freeBefore), static_cast<unsigned>(largestBefore),
              static_cast<unsigned>(adaptive_grayscale_strip::ROW_CANDIDATES.back()),
              static_cast<unsigned>(rowBytes * adaptive_grayscale_strip::ROW_CANDIDATES.back()));
      // A combined-base panel may still hold a deferred B/W activation; flush
      // it so the page reaches the panel even after every bounded retry fails.
      if (renderer.combinesGrayscaleBase()) renderer.cleanupGrayscaleWithFrameBuffer();
      return false;
    }

    if (stripRows < adaptive_grayscale_strip::DEFAULT_ROWS) {
      LOG_INF("READER", "Grayscale applied=1 path=strip rows=%u bytes=%u free=%u largest=%u degraded_memory=1",
              static_cast<unsigned>(stripRows), static_cast<unsigned>(rowBytes * stripRows),
              static_cast<unsigned>(freeBefore), static_cast<unsigned>(largestBefore));
    } else {
      LOG_DBG("READER", "Grayscale applied=1 path=strip rows=%u bytes=%u free=%u largest=%u",
              static_cast<unsigned>(stripRows), static_cast<unsigned>(rowBytes * stripRows),
              static_cast<unsigned>(freeBefore), static_cast<unsigned>(largestBefore));
    }

    const auto renderPlane = [&](const bool lsbPlane) {
      renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < displayHeight; y += stripRows) {
        const int rows = std::min<int>(stripRows, displayHeight - y);
        renderer.beginStripTarget(scratch.get(), y, rows);
        renderer.clearScreen(0x00);
        renderFn();
        renderer.endStripTarget();
        renderer.writeGrayscalePlaneStrip(lsbPlane, scratch.get(), y, rows);
      }
    };

    renderPlane(true);
    renderPlane(false);
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.displayGrayBuffer();
    renderer.cleanupGrayscaleWithFrameBuffer();
    return true;
  }

  const uint32_t freeBefore = ESP.getFreeHeap();
  const uint32_t largestBefore = ESP.getMaxAllocHeap();
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Grayscale applied=0 path=framebuffer-copy reason=bw-store-oom free=%u largest=%u",
            static_cast<unsigned>(freeBefore), static_cast<unsigned>(largestBefore));
    // A combined-base panel may still hold a deferred B/W activation; flush it
    // so the page reaches the panel even without its grays.
    if (renderer.combinesGrayscaleBase()) renderer.cleanupGrayscaleWithFrameBuffer();
    return false;
  }

  prepareGrayscalePlanes(renderer, true, renderer.combinesGrayscaleBase());

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
  LOG_DBG("READER", "Grayscale applied=1 path=framebuffer-copy free=%u largest=%u", static_cast<unsigned>(freeBefore),
          static_cast<unsigned>(largestBefore));
  return true;
}

struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};

// Returns true if the back button was consumed (caller should return).
// Long press (>= GO_BACK_OR_HOME_MS):
// - default: go to file browser
// - with backShortToFileBrowser: go home
// Short press (< GO_BACK_OR_HOME_MS):
// - default: go home
// - with backShortToFileBrowser: go to file browser.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager,
                                 const char* filePath, BackNavCallback goHome) {
  // The reading surface deliberately has no left-edge swipe-to-exit path: in
  // swipe page-turn mode a right swipe must page back instead. Home remains
  // available through the board's dedicated Home gesture/key. Back swipes stay
  // available in menus and other activities; only this reader-surface handler
  // ignores them. Physical Back buttons are unaffected: isPressed() is
  // button-only, and this guard skips just the gesture's own release frame.
  if (mappedInput.wasBackGesture()) {
    return false;
  }

  const bool backTriggered = mappedInput.wasLongPressed(MappedInputManager::Button::Back, GO_BACK_OR_HOME_MS) ||
                             mappedInput.wasReleased(MappedInputManager::Button::Back);
  if (!backTriggered) return false;

  const bool longPress = mappedInput.getHeldTime() >= GO_BACK_OR_HOME_MS;
  if (longPress != SETTINGS.backShortToFileBrowser) {
    activityManager.goToFileBrowser(filePath);
  } else {
    goHome.fn(goHome.ctx);
  }
  return true;
}

}  // namespace ReaderUtils
