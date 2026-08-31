#pragma once

class ActivityManager;
class GfxRenderer;
class MappedInputManager;

class VanNhanSoUpdateCoordinator {
 public:
  VanNhanSoUpdateCoordinator(ActivityManager& activityManager, GfxRenderer& renderer, MappedInputManager& mappedInput);

  void startDailyUpdateIfEligible(bool eligible);

 private:
  ActivityManager& activityManager;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
};
