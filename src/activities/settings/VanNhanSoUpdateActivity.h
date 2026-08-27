#pragma once

#include "activities/Activity.h"

class VanNhanSoUpdateActivity final : public Activity {
 public:
  explicit VanNhanSoUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool automatic = false)
      : Activity("VanNhanSoUpdate", renderer, mappedInput), automatic(automatic) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == AUTO_CONNECTING || state == DOWNLOADING; }
  bool skipLoopDelay() override { return true; }

 private:
  enum State { WIFI_SELECTION, AUTO_CONNECTING, DOWNLOADING, SUCCESS, FAILED, SKIPPED };

  State state = WIFI_SELECTION;
  const bool automatic;
  bool shouldTearDownWifiOnExit = false;
  bool resumeReaderAfterRestart = false;
  unsigned long connectionStartTime = 0;
  uint32_t currentDateKey = 0;

  static constexpr unsigned long AUTO_CONNECTION_TIMEOUT_MS = 10000;

  void onWifiSelectionComplete(bool connected);
  void startAutomaticUpdate();
  void checkAutomaticConnection();
  bool resolveCurrentDate(bool allowNetworkSync);
  bool isCurrentCache() const;
  bool readDateMarker(uint32_t& dateKey) const;
  bool writeDateMarker(uint32_t dateKey) const;
  void downloadSleepScreen();
  bool validateDownloadedFile() const;
  bool installDownloadedFile() const;
};
