#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "TxtReaderActivity.h"
#include "XtcReaderActivity.h"

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    activity = makeUniqueNoThrow<TxtReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

void ReaderActivity::noteReaderInput(const uint32_t atMs) { postVisibleIdleGuard.noteInput(atMs); }

void ReaderActivity::beginReaderTurn(const int direction, const int queueDepth) {
  const uint32_t now = static_cast<uint32_t>(millis());
  const auto before = readerTelemetryPosition();
  const uint32_t sequence = turnTelemetry.input(now, direction, before.first, before.second);
  turnTelemetry.queued(now, queueDepth);
#if defined(ENABLE_SERIAL_LOG) && defined(LOG_LEVEL) && LOG_LEVEL >= 2
  LOG_DBG("PTM", "format=%s phase=input seq=%lu dir=%d queue=%d at_ms=%lu before=%ld:%ld", name.c_str(),
          static_cast<unsigned long>(sequence), direction, queueDepth, static_cast<unsigned long>(now),
          static_cast<long>(before.first), static_cast<long>(before.second));
  LOG_DBG("PTM", "format=%s phase=queued seq=%lu dir=%d queue=%d at_ms=%lu", name.c_str(),
          static_cast<unsigned long>(sequence), direction, queueDepth, static_cast<unsigned long>(now));
#endif
}

void ReaderActivity::updateReaderTurnQueueDepth(const int queueDepth) {
  const uint32_t now = static_cast<uint32_t>(millis());
  turnTelemetry.queueDepth(queueDepth);
#if defined(ENABLE_SERIAL_LOG) && defined(LOG_LEVEL) && LOG_LEVEL >= 2
  const auto trace = turnTelemetry.snapshot();
  LOG_DBG("PTM", "format=%s phase=queued seq=%lu dir=%d queue=%d at_ms=%lu", name.c_str(),
          static_cast<unsigned long>(trace.sequence), trace.direction, queueDepth, static_cast<unsigned long>(now));
#endif
}

bool ReaderActivity::canRunDeferredReaderWork(const uint32_t nowMs) const {
  return postVisibleIdleGuard.canRunDeferredWork(nowMs);
}

uint32_t ReaderActivity::beginReaderIdleWork(const char* kind) const {
  const uint32_t now = static_cast<uint32_t>(millis());
#if defined(ENABLE_SERIAL_LOG) && defined(LOG_LEVEL) && LOG_LEVEL >= 2
  LOG_DBG("PTM", "format=%s phase=idle_work_begin kind=%s at_ms=%lu free=%u largest=%u", name.c_str(), kind,
          static_cast<unsigned long>(now), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#else
  (void)kind;
#endif
  return now;
}

void ReaderActivity::endReaderIdleWork(const char* kind, const uint32_t startedAtMs) const {
#if defined(ENABLE_SERIAL_LOG) && defined(LOG_LEVEL) && LOG_LEVEL >= 2
  const uint32_t now = static_cast<uint32_t>(millis());
  LOG_DBG("PTM", "format=%s phase=idle_work_end kind=%s at_ms=%lu duration_ms=%lu free=%u largest=%u", name.c_str(),
          kind, static_cast<unsigned long>(now), static_cast<unsigned long>(now - startedAtMs),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#else
  (void)kind;
  (void)startedAtMs;
#endif
}

void ReaderActivity::onEnter() {
  Activity::onEnter();
  postVisibleIdleGuard.reset();

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  if (!prepareReaderSettings()) {
    finish();
    return;
  }

  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();

  if (!loadBook()) {
    finish();
    return;
  }

  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
  requestUpdate();
}

void ReaderActivity::onExit() {
  flushReaderState();
  Activity::onExit();

  restoreReaderSettings();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
}

bool ReaderActivity::handleBackNavigation() {
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

void ReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool ReaderActivity::endOfBookMenuActive() const {
  return isAtEndOfBook() && endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive();
}

bool ReaderActivity::handleEndOfBookMenu(const bool suppressConfirmRelease) {
  if (suppressConfirmRelease || !endOfBookMenuActive()) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      onGoHome();
      return true;
    case EndOfBookOptions::Action::LastPage:
      onReturnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }

  return false;
}

bool ReaderActivity::handleEndOfBookPageTurn(const bool prevTriggered, const bool nextTriggered) {
  if (!isAtEndOfBook()) return false;

  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) {
    return true;
  }
  if (nextTriggered) {
    onGoHome();
  } else if (prevTriggered) {
    onReturnFromEndOfBook();
    requestUpdate();
  }
  return true;
}

void ReaderActivity::loop() {
  const uint32_t inputAtMs = static_cast<uint32_t>(millis());
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) noteReaderInput(inputAtMs);
  requestProgressSaveIfDue();
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) return;
  if (handleFormatInput()) return;
  if (handleBackNavigation()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skip =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs >= ReaderUtils::SKIP_HOLD_MS;

  if (prevTriggered) {
    beginReaderTurn(-1);
    if (skip) {
      skipPages(-10);
    } else {
      pageTurn(false);
    }
  } else {
    beginReaderTurn(1);
    if (skip) {
      skipPages(10);
    } else {
      pageTurn(true);
    }
  }
  requestUpdate();
}

void ReaderActivity::render(RenderLock&&) {
  if (isAtEndOfBook()) {
    if (!endOfBookOptions) {
      endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
      if (!endOfBookOptions) LOG_ERR("READER", "OOM: EndOfBookOptions");
    }
    renderer.clearScreen();
    if (endOfBookOptions) {
      endOfBookOptions->loadOnce(bookPath);
      // Release-publish AFTER loadOnce() so the main task's acquire load can't
      // observe an object whose names/selector are still being populated.
      endOfBookOptionsReady.store(true, std::memory_order_release);
      endOfBookOptions->render(renderer, mappedInput);
    }
    renderer.displayBuffer();
    onEndOfBookRendered();
    return;
  }

  const uint32_t renderBeginAtMs = static_cast<uint32_t>(millis());
  turnTelemetry.renderBegin(renderBeginAtMs);
#if defined(ENABLE_SERIAL_LOG) && defined(LOG_LEVEL) && LOG_LEVEL >= 2
  {
    const auto trace = turnTelemetry.snapshot();
    LOG_DBG("PTM", "format=%s phase=render_begin seq=%lu dir=%d queue=%d at_ms=%lu", name.c_str(),
            static_cast<unsigned long>(trace.sequence), trace.direction, trace.queueDepth,
            static_cast<unsigned long>(renderBeginAtMs));
  }
#endif
  renderBook();
  if (!renderedReadingPageThisFrame()) return;
  const uint32_t visibleAtMs = static_cast<uint32_t>(millis());
  postVisibleIdleGuard.pageVisible(visibleAtMs);
  const auto after = readerTelemetryPosition();
  const auto trace = turnTelemetry.visible(visibleAtMs, after.first, after.second);
#if defined(ENABLE_SERIAL_LOG) && defined(LOG_LEVEL) && LOG_LEVEL >= 2
  const unsigned long inputVisibleMs = trace.inputAtMs == 0 ? 0 : visibleAtMs - trace.inputAtMs;
  const unsigned long queuedVisibleMs = trace.queuedAtMs == 0 ? 0 : visibleAtMs - trace.queuedAtMs;
  const unsigned long renderVisibleMs = trace.renderBeginAtMs == 0 ? 0 : visibleAtMs - trace.renderBeginAtMs;
  LOG_DBG("PTM",
          "format=%s phase=visible seq=%lu dir=%d queue=%d at_ms=%lu input_visible_ms=%lu "
          "queued_visible_ms=%lu render_visible_ms=%lu before=%ld:%ld after=%ld:%ld free=%u largest=%u",
          name.c_str(), static_cast<unsigned long>(trace.sequence), trace.direction, trace.queueDepth,
          static_cast<unsigned long>(visibleAtMs), inputVisibleMs, queuedVisibleMs, renderVisibleMs,
          static_cast<long>(trace.beforePrimary), static_cast<long>(trace.beforeSecondary),
          static_cast<long>(trace.afterPrimary), static_cast<long>(trace.afterSecondary),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#else
  (void)trace;
#endif
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}
