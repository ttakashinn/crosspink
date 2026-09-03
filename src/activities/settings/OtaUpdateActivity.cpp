#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

namespace {
struct OtaActionRects {
  Rect cancel;
  Rect update;
};

OtaActionRects getOtaActionRects(const GfxRenderer& renderer) {
  const int top = renderer.getScreenHeight() - 80;
  const int width = renderer.getScreenWidth() / 2;
  return {Rect{0, top, width, 80}, Rect{width, top, renderer.getScreenWidth() - width, 80}};
}

bool contains(const Rect& rect, const int x, const int y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

const char* otaErrorDetail(const OtaUpdater::OtaUpdaterError error) {
  switch (error) {
    case OtaUpdater::WRONG_DEVICE_ERROR:
      return tr(STR_FIRMWARE_WRONG_DEVICE);
    case OtaUpdater::CHECKSUM_ERROR:
    case OtaUpdater::INVALID_IMAGE_ERROR:
    case OtaUpdater::JSON_PARSE_ERROR:
      return tr(STR_INVALID_FIRMWARE);
    case OtaUpdater::STORAGE_ERROR:
      return tr(STR_FIRMWARE_FILE_OPEN_FAILED);
    case OtaUpdater::HTTP_ERROR:
      return tr(STR_DOWNLOAD_FAILED);
    case OtaUpdater::INTERNAL_UPDATE_ERROR:
      return tr(STR_FIRMWARE_WRITE_FAILED);
    default:
      return nullptr;
  }
}
}  // namespace
void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("OTA", "WiFi connected, checking for update");

  {
    RenderLock lock(*this);
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  const auto res = updater.checkForUpdate();
  // NO_UPDATE here means the release carries no firmware asset for this board
  // (expected until per-board assets are published) — not a failure.
  if (res == OtaUpdater::NO_UPDATE) {
    LOG_DBG("OTA", "No firmware asset for this board in latest release");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = otaErrorDetail(res);
      state = FAILED;
    }
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
  const char* options[] = {tr(STR_CANCEL), tr(STR_UPDATE)};
  // Updating firmware is destructive enough that a fresh confirmation must
  // actively move away from Cancel. InputReleaseGate also blocks the release
  // that selected the Wi-Fi network from crossing into this popup.
  confirmPopup.show(tr(STR_NEW_UPDATE), options, 2, 0, [this](const int idx) {
    if (idx == 1) {
      runUpdateInstall();
    } else {
      finish();
    }
  });
  requestUpdate();
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  // Turn on WiFi immediately
  LOG_DBG("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  // Success path reboots via the SHUTTING_DOWN state's plain ESP.restart()
  // (loop() above) so the new firmware boots normally. Back-out paths land
  // here with wifi still active; silent-restart to free the LWIP/mbedTLS
  // fragmentation, same as the other wifi activities.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    const size_t totalSize = updater.getTotalSize();
    updaterProgress =
        totalSize > 0 ? static_cast<float>(updater.getProcessedSize()) / static_cast<float>(totalSize) : 0;
    const unsigned int currentPercentage = static_cast<unsigned int>(updaterProgress * 100);
    const auto currentPhase = updater.getInstallPhase();
    // OtaUpdater already limits callbacks to 5% steps. Suppress only exact
    // duplicates here; phase transitions at the same percentage must render.
    if (currentPercentage == lastUpdaterPercentage && currentPhase == lastInstallPhase) {
      return;
    }
    lastUpdaterPercentage = currentPercentage;
    lastInstallPhase = currentPhase;
  }

  if (state == CHECKING_FOR_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_UPDATE));
  } else if (state == WAITING_CONFIRMATION) {
    // Version info sits in the upper part of the screen so the centered
    // Cancel/Update popup doesn't cover it (same layout as ConfirmationActivity).
    const int infoTop = pageHeight / 6;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop,
                      (std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION).c_str());
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop + height + metrics.verticalSpacing,
                      (std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion()).c_str());

    if (confirmPopup.processRender(renderer, mappedInput)) return;
  } else if (state == UPDATE_IN_PROGRESS) {
    const char* phaseText = tr(STR_UPDATING);
    if (updater.getInstallPhase() == OtaUpdater::InstallPhase::DOWNLOADING) phaseText = tr(STR_DOWNLOADING);
    if (updater.getInstallPhase() == OtaUpdater::InstallPhase::VERIFYING) phaseText = tr(STR_VALIDATING_FIRMWARE);
    renderer.drawCenteredText(UI_10_FONT_ID, top, phaseText);

    int y = top + height + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(updaterProgress * 100), 100);

    // BaseTheme::drawProgressBar already renders the percentage below the bar.
    // Do not draw a second percentage label here.
  } else if (state == NO_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NO_UPDATE), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (failedDetail != nullptr) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + metrics.verticalSpacing, failedDetail);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height + metrics.verticalSpacing, tr(STR_POWER_ON_HINT));
  }

  renderer.displayBuffer();
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  {
    RenderLock lock(*this);
    state = UPDATE_IN_PROGRESS;
    failedDetail = nullptr;
    lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
    lastInstallPhase = OtaUpdater::InstallPhase::IDLE;
  }
  requestUpdateAndWait();
  const auto res = updater.installUpdate(
      [](void* ctx) {
        // immediate=true notifies the render task directly. The default deferred path only
        // sets a flag consumed at the end of ActivityManager::loop(), which never runs while
        // installUpdate() blocks this task.
        static_cast<OtaUpdateActivity*>(ctx)->requestUpdate(true);
      },
      this);

  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = otaErrorDetail(res);
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  // Hold the completion screen briefly so the user sees it, then restart.
  delay(3000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::retryAfterFailure() {
  failedDetail = nullptr;
  lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  lastInstallPhase = OtaUpdater::InstallPhase::IDLE;
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }
  state = WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::loop() {
  if (state == WAITING_CONFIRMATION) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    // Popup dismissed without a selection (Back button or tap outside): cancel.
    finish();
    return;
  }

  if (state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      retryAfterFailure();
    } else if (mappedInput.wasScreenTapped(x, y)) {
      const auto rects = getOtaActionRects(renderer);
      if (contains(rects.update, x, y)) {
        retryAfterFailure();
      } else if (contains(rects.cancel, x, y)) {
        finish();
      }
    }
    return;
  }

  if (state == NO_UPDATE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
