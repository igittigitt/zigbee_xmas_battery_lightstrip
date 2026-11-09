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
#include "radio_trace.h"
#include "debug_marker.h"
#include "battery_model.h"
#include "cluster_manager.h"

static const char *TAG = "MAIN";

/* Prominent banner in DEBUG builds, so a DEBUG firmware is not mistaken for production. */
static void log_debug_banner(void)
{
    if (!APP_DEBUG) {
        return;
    }
    ESP_LOGW(TAG, "*************************************");
    ESP_LOGW(TAG, "***     DEBUG BUILD (LEVEL %d)     ***", APP_DEBUG_LEVEL);
    ESP_LOGW(TAG, "*************************************");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Zigbee light chain starting (DEBUG level %d)", APP_DEBUG_LEVEL);
    log_debug_banner();     // first, before DEBUG level 3 silences the log

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

    // DEBUG level 3 only: PPK2 event markers on GPIOs, log silenced afterwards
    if (debug_marker_init() != ESP_OK) {
        ESP_LOGE(TAG, "PPK2 markers not available");
    }

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

    // DEBUG only (ZL_DEBUG_RADIO_TRACE): log beacons/join frames with LQI/RSSI
    if (radio_trace_init() != ESP_OK) {
        ESP_LOGE(TAG, "Radio trace not available");
    }

    xTaskCreate(zigbee_device_task, "Zigbee_main", ZB_TASK_STACK_SIZE, NULL, ZB_TASK_PRIORITY, NULL);
}
