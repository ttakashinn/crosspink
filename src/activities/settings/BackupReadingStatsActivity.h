#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class BackupReadingStatsActivity final : public Activity {
 public:
  BackupReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BackupReadingStats", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { CONFIRM, SUCCESS, FAILED };

  State state = State::CONFIRM;
  OptionPopup popup;
  char fileName[64] = {};

  void runBackup();
};
