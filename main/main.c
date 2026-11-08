#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "zigbee_config.h"
#include "zigbee_device.h"
#include "power_management.h"
#include "light.h"
#include "button.h"
#include "battery_model.h"
#include "cluster_manager.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Zigbee light chain starting (DEBUG level %d)", APP_DEBUG_LEVEL);

    // NVS also holds the Zigbee datasets (ZB_STORAGE_PARTITION_NAME)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Erasing also removes the network pairing
        ESP_LOGW(TAG, "NVS layout changed, erasing (Zigbee pairing is lost)");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Automatic light sleep; a failure only costs battery life, keep running
    power_management_init();

    // Remaining-time model (goal Z14), before the light books its first level;
    // the first update runs after BATTERY_MODEL_INTERVAL_MS, long after the stack start.
    if (battery_model_init(cluster_manager_update_runtime) != ESP_OK) {
        ESP_LOGE(TAG, "Battery model not started, remaining time stays at its start value");
    }

    // Light chain driver (backend selected by DEBUG), light is off afterwards
    ESP_ERROR_CHECK(light_init());

    // BOOT button (goal Z5), active in every state incl. light sleep; without it
    // a device stuck in rejoin could only be re-paired by erasing the flash
    if (button_init() != ESP_OK) {
        ESP_LOGE(TAG, "Button not available");
    }
    power_management_report();

    xTaskCreate(zigbee_device_task, "Zigbee_main", ZB_TASK_STACK_SIZE, NULL, ZB_TASK_PRIORITY, NULL);
}
