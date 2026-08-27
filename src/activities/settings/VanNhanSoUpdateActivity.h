#pragma once

#include <CrossPointState.h>

#include <atomic>

#include "activities/Activity.h"
#include "network/HttpDownloader.h"

class VanNhanSoUpdateActivity final : public Activity {
 public:
  explicit VanNhanSoUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool automatic = false,
                                   bool sleepAfterUpdate = false)
      : Activity("VanNhanSoUpdate", renderer, mappedInput), automatic(automatic), sleepAfterUpdate(sleepAfterUpdate) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == AUTO_CONNECTING || state == DOWNLOADING; }
  bool skipLoopDelay() override { return state == AUTO_CONNECTING || state == DOWNLOADING; }

 private:
  enum State {
    STATUS,
    WIFI_SELECTION,
    AUTO_CONNECTING,
    DOWNLOADING,
    VERIFYING,
    INSTALLING,
    SUCCESS,
    FAILED,
    CANCELLED,
    SKIPPED,
  };

  std::atomic<State> state{STATUS};
  const bool automatic;
  const bool sleepAfterUpdate;
  bool shouldTearDownWifiOnExit = false;
  bool cancelDownload = false;
  unsigned long connectionStartTime = 0;
  uint32_t currentDateKey = 0;
  uint16_t currentMinute = UINT16_MAX;
  std::atomic<size_t> downloadedBytes{0};
  std::atomic<size_t> totalBytes{0};

  static constexpr unsigned long AUTO_CONNECTION_TIMEOUT_MS = 8000;
  static constexpr uint32_t AUTOMATIC_DOWNLOAD_TIMEOUT_MS = 5000;
  static constexpr uint32_t MANUAL_DOWNLOAD_TIMEOUT_MS = 15000;

  void onWifiSelectionComplete(bool connected);
  void beginManualUpdate();
  void startAutomaticUpdate();
  void checkAutomaticConnection();
  bool resolveCurrentDate(bool allowNetworkSync);
  bool isCurrentCache() const;
  bool isBackoffActive() const;
  bool readDateMarker(uint32_t& dateKey) const;
  bool readProfileMarker(char* profile, size_t profileSize) const;
  void downloadSleepScreen();
  void recordAttempt();
  void recordSuccess();
  void recordCancelled();
  void fail(CrossPointState::VanNhanSoUpdateError error);
  void failDownload(HttpDownloader::DownloadError result, const HttpDownloader::ResponseInfo& responseInfo);
  bool validateChecksum(const std::string& expectedChecksum) const;
  const char* errorText(CrossPointState::VanNhanSoUpdateError error) const;
};
