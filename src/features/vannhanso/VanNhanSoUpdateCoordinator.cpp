#include "VanNhanSoUpdateCoordinator.h"

#include <CrossPointSettings.h>
#include <Logging.h>
#include <Memory.h>

#include "activities/ActivityManager.h"
#include "activities/settings/VanNhanSoUpdateActivity.h"

VanNhanSoUpdateCoordinator::VanNhanSoUpdateCoordinator(ActivityManager& activityManager, GfxRenderer& renderer,
                                                       MappedInputManager& mappedInput)
    : activityManager(activityManager), renderer(renderer), mappedInput(mappedInput) {}

void VanNhanSoUpdateCoordinator::startFirstBootUpdateIfEligible(const bool eligible) {
  if (!eligible || SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::VANNHANSO ||
      SETTINGS.vanNhanSoUpdateMode != CrossPointSettings::VANNHANSO_UPDATE_MODE::VANNHANSO_UPDATE_FIRST_BOOT) {
    return;
  }

  // Commit the destination screen first. The updater is then invisible and
  // cancellable, so it never blocks entry to the reader/home screen.
  activityManager.loop();
  activityManager.requestUpdateAndWait();
  auto updateActivity = makeUniqueNoThrow<VanNhanSoUpdateActivity>(renderer, mappedInput, true);
  if (updateActivity) {
    activityManager.pushActivity(std::move(updateActivity));
  } else {
    LOG_ERR("VNS", "OOM: automatic update activity");
  }
}

bool VanNhanSoUpdateCoordinator::interceptSleepForUpdate(const bool fromTimeout) {
  if (sleepUpdateInProgress) return true;

  const bool shouldUpdate =
      !skipSleepUpdateOnce && SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::VANNHANSO &&
      SETTINGS.vanNhanSoUpdateMode == CrossPointSettings::VANNHANSO_UPDATE_MODE::VANNHANSO_UPDATE_ON_SLEEP;
  skipSleepUpdateOnce = false;
  if (!shouldUpdate) return false;

  auto updateActivity = makeUniqueNoThrow<VanNhanSoUpdateActivity>(renderer, mappedInput, true, true);
  if (!updateActivity) {
    LOG_ERR("VNS", "OOM: update-before-sleep activity; sleeping with cached image");
    return false;
  }

  sleepUpdateInProgress = true;
  sleepFromTimeout = fromTimeout;
  activityManager.pushActivity(std::move(updateActivity));
  return true;
}

bool VanNhanSoUpdateCoordinator::consumeFinishedSleepUpdate(bool& fromTimeout) {
  if (!sleepUpdateFinished) return false;

  sleepUpdateFinished = false;
  sleepUpdateInProgress = false;
  skipSleepUpdateOnce = true;
  fromTimeout = sleepFromTimeout;
  return true;
}
