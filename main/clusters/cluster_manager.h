#ifndef CLUSTER_MANAGER_H
#define CLUSTER_MANAGER_H

#include "esp_err.h"

/**
 * @brief Build the data model and register it with the Zigbee stack.
 *
 * Creates endpoint ZB_ENDPOINT as ZHA On/Off Light (device ID 0x0100) with the
 * clusters Basic, Identify, Groups, Scenes, On/Off (from the ZHA template,
 * On/Off extended by StartUpOnOff) plus Power Configuration, and registers
 * the device.
 *
 * Must be called in the Zigbee task after esp_zigbee_init() and before
 * esp_zigbee_start().
 *
 * @return ESP_OK on success
 */
esp_err_t cluster_manager_register_device(void);

#endif // CLUSTER_MANAGER_H
