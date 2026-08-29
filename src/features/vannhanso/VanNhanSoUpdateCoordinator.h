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
  void markSleepUpdateFinished(bool continueSleeping) {
    sleepUpdateFinished = true;
    continueSleepingAfterUpdate = continueSleeping;
  }
  bool consumeFinishedSleepUpdate(bool& fromTimeout, bool& continueSleeping);

 private:
  ActivityManager& activityManager;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  bool sleepUpdateInProgress = false;
  bool sleepUpdateFinished = false;
  bool sleepFromTimeout = false;
  bool continueSleepingAfterUpdate = true;
  bool skipSleepUpdateOnce = false;
};
