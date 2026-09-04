#pragma once

#include <CrossPointState.h>

#include <atomic>

#include "activities/Activity.h"
#include "features/vannhanso/VanNhanSoUpdatePolicy.h"
#include "network/HttpDownloader.h"

class VanNhanSoUpdateActivity final : public Activity {
 public:
  explicit VanNhanSoUpdateActivity(
      GfxRenderer& renderer, MappedInputManager& mappedInput,
      vannhanso_update_policy::UpdateTrigger trigger = vannhanso_update_policy::UpdateTrigger::MANUAL,
      bool returnToVanNhanSoSettings = false)
      : Activity("VanNhanSoUpdate", renderer, mappedInput),
        trigger(trigger),
        dailyInteractive(vannhanso_update_policy::isDailyInteractive(trigger)),
        returnToVanNhanSoSettings(returnToVanNhanSoSettings) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == DOWNLOADING; }
  bool skipLoopDelay() override { return state == DOWNLOADING; }

 private:
  enum State {
    STATUS,
    WIFI_SELECTION,
    DOWNLOADING,
    VERIFYING,
    INSTALLING,
    SUCCESS,
    FAILED,
    CANCELLED,
    SKIPPED,
  };

  std::atomic<State> state{STATUS};
  const vannhanso_update_policy::UpdateTrigger trigger;
  const bool dailyInteractive;
  const bool returnToVanNhanSoSettings;
  // Văn Nhân Số is always shown by SleepActivity on the portrait canvas. An
  // updater entered from a landscape reader must use that same cache profile
  // and UI canvas, then restore the reader orientation on exit.
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;
  bool shouldTearDownWifiOnExit = false;
  bool cancelDownload = false;
  bool pendingProfileRequired = false;
  bool successScreenRendered = false;
  uint32_t currentProfileHash = 0;
  uint32_t currentDateKey = 0;
  uint32_t successRenderedAtMs = 0;
  uint16_t currentMinute = UINT16_MAX;
  std::atomic<size_t> downloadedBytes{0};
  std::atomic<size_t> totalBytes{0};

  static constexpr uint32_t MANUAL_DOWNLOAD_TIMEOUT_MS = 20000;

  void onWifiSelectionComplete(bool connected);
  void beginInteractiveUpdate();
  void startDailyInteractiveUpdate();
  bool resolveCurrentDate(bool allowNetworkSync);
  void syncPendingProfile();
  void clearPendingProfile();
  bool isCurrentCache() const;
  bool isBackoffActive() const;
  bool readDateMarker(uint32_t& dateKey) const;
  bool readProfileMarker(char* profile, size_t profileSize) const;
  void downloadSleepScreen();
  void recordAttempt();
  void recordSuccess();
  void completeSuccessfulUpdate();
  void recordCancelled(bool returnToStatus = false);
  void fail(CrossPointState::VanNhanSoUpdateError error);
  void failDownload(HttpDownloader::DownloadError result, const HttpDownloader::ResponseInfo& responseInfo);
  bool validateChecksum(const char* path, const std::string& expectedChecksum, bool logMismatch = true) const;
  const char* errorText(CrossPointState::VanNhanSoUpdateError error) const;
};
