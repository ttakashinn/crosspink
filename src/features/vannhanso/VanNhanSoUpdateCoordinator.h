#pragma once

class ActivityManager;
class GfxRenderer;
class MappedInputManager;

class VanNhanSoUpdateCoordinator {
 public:
  VanNhanSoUpdateCoordinator(ActivityManager& activityManager, GfxRenderer& renderer, MappedInputManager& mappedInput);

  void startFirstBootUpdateIfEligible(bool eligible);
  bool interceptSleepForUpdate(bool fromTimeout);
  bool isSleepUpdateInProgress() const { return sleepUpdateInProgress; }
  void markSleepUpdateFinished() { sleepUpdateFinished = true; }
  bool consumeFinishedSleepUpdate(bool& fromTimeout);

 private:
  ActivityManager& activityManager;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  bool sleepUpdateInProgress = false;
  bool sleepUpdateFinished = false;
  bool sleepFromTimeout = false;
  bool skipSleepUpdateOnce = false;
};
