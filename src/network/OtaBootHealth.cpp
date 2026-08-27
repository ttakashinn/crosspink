#include "OtaBootHealth.h"

#include <Logging.h>
#include <esp_ota_ops.h>

#include "activities/Activity.h"
#include "activities/ActivityManager.h"

namespace ota_boot_health {

void confirmPendingImageAfterStartup(ActivityManager& activityManager) {
  const esp_partition_t* runningPartition = esp_ota_get_running_partition();
  if (runningPartition == nullptr) {
    LOG_ERR("OTA", "Cannot identify running partition; leaving image unconfirmed");
    return;
  }

  esp_ota_img_states_t imageState;
  const esp_err_t stateResult = esp_ota_get_state_partition(runningPartition, &imageState);
  if (stateResult != ESP_OK) {
    LOG_ERR("OTA", "Cannot read running image state: %s", esp_err_to_name(stateResult));
    return;
  }
  if (imageState != ESP_OTA_IMG_PENDING_VERIFY) return;

  activityManager.loop();
  activityManager.requestUpdateAndWait();

  const esp_err_t confirmResult = esp_ota_mark_app_valid_cancel_rollback();
  if (confirmResult == ESP_OK) {
    LOG_INF("OTA", "Pending firmware passed startup check and was marked valid");
  } else {
    LOG_ERR("OTA", "Could not mark pending firmware valid: %s", esp_err_to_name(confirmResult));
  }
}

}  // namespace ota_boot_health
