#ifndef BATTERY_RUNTIME_CLUSTER_H
#define BATTERY_RUNTIME_CLUSTER_H

#include "esp_err.h"
#include "esp_zigbee.h"
#include "battery_model.h"

/** Manufacturer-specific cluster ID of the remaining-time cluster (goal Z14). */
#define BATTERY_RUNTIME_CLUSTER_ID                  0xFC00

/** RemainingOnTime: uint16, 0.1 h, light-on time at the current "on" level, 0xFFFF = unknown. */
#define BATTERY_RUNTIME_ATTR_REMAINING_ON_TIME_ID   0x0000

/** RemainingDays: uint16, 0.1 d, at the average daily use, 0xFFFF = unknown. */
#define BATTERY_RUNTIME_ATTR_REMAINING_DAYS_ID      0x0001

/**
 * @brief Add the remaining-time cluster (server, ID 0xFC00) to an endpoint.
 *
 * Both attributes are read-only and reportable, without manufacturer code
 * (EZB_ZCL_STD_MANUF_CODE). Initial values come from battery_model_get().
 * Zigbee2MQTT needs the external converter in z2m/ to show them.
 *
 * @param ep_desc Endpoint descriptor (before registration)
 * @return ESP_OK on success
 */
esp_err_t battery_runtime_cluster_add(ezb_af_ep_desc_t ep_desc);

/**
 * @brief Write the remaining-time attributes (reports follow the configured reporting).
 *
 * Must be called in the Zigbee task or with the Zigbee lock held.
 *
 * @param runtime New remaining time
 * @return ESP_OK on success
 */
esp_err_t battery_runtime_cluster_update(const battery_runtime_t *runtime);

#endif // BATTERY_RUNTIME_CLUSTER_H
