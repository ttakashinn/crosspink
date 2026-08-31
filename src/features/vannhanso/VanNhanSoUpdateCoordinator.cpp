#include "VanNhanSoUpdateCoordinator.h"

#include <CrossPointSettings.h>
#include <Logging.h>
#include <Memory.h>

#include "activities/ActivityManager.h"
#include "activities/settings/VanNhanSoUpdateActivity.h"
#include "features/vannhanso/VanNhanSoUpdatePolicy.h"

VanNhanSoUpdateCoordinator::VanNhanSoUpdateCoordinator(ActivityManager& activityManager, GfxRenderer& renderer,
                                                       MappedInputManager& mappedInput)
    : activityManager(activityManager), renderer(renderer), mappedInput(mappedInput) {}

void VanNhanSoUpdateCoordinator::startDailyUpdateIfEligible(const bool eligible) {
  if (!eligible || SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::VANNHANSO) {
    return;
  }

  // Commit the destination screen first. The updater is then invisible and
  // cancellable, so it never blocks entry to the reader/home screen.
  activityManager.loop();
  activityManager.requestUpdateAndWait();
  auto updateActivity = makeUniqueNoThrow<VanNhanSoUpdateActivity>(
      renderer, mappedInput, vannhanso_update_policy::UpdateTrigger::FIRST_START_OF_DAY);
  if (updateActivity) {
    activityManager.pushActivity(std::move(updateActivity));
  } else {
    LOG_ERR("VNS", "OOM: automatic update activity");
  }
}
