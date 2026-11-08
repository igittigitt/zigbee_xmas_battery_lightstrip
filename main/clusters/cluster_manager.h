#ifndef CLUSTER_MANAGER_H
#define CLUSTER_MANAGER_H

#include "esp_err.h"
#include "battery_model.h"

/**
 * @brief Build the data model and register it with the Zigbee stack.
 *
 * Creates endpoint ZB_ENDPOINT as ZHA Dimmable Light (device ID 0x0101) with the
 * clusters Basic, Identify, Groups, Scenes, On/Off (from the ZHA template,
 * On/Off extended by StartUpOnOff) plus Power Configuration and the
 * manufacturer-specific remaining-time cluster 0xFC00, and registers the device.
 *
 * Must be called in the Zigbee task after esp_zigbee_init() and before
 * esp_zigbee_start().
 *
 * @return ESP_OK on success
 */
esp_err_t cluster_manager_register_device(void);

/**
 * @brief Write the remaining time to the Zigbee attributes (goal Z14).
 *
 * Takes the Zigbee lock, may be called from any task except the Zigbee task
 * (battery_model_cb_t of the battery model). The model's first update comes
 * BATTERY_MODEL_INTERVAL_MS after boot, after cluster_manager_register_device().
 *
 * @param runtime New remaining time
 */
void cluster_manager_update_runtime(const battery_runtime_t *runtime);

#endif // CLUSTER_MANAGER_H
