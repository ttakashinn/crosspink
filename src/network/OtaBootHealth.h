#pragma once

class ActivityManager;

namespace ota_boot_health {

// Confirm a pending OTA image only after the selected activity has completed
// one physical display refresh. Leaving it pending lets the bootloader roll
// back an image that crashes before the UI is usable.
void confirmPendingImageAfterStartup(ActivityManager& activityManager);

}  // namespace ota_boot_health
