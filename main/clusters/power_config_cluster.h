#ifndef POWER_CONFIG_CLUSTER_H
#define POWER_CONFIG_CLUSTER_H

#include "esp_err.h"
#include "esp_zigbee.h"

/**
 * @brief Add the Power Configuration cluster (server) to an endpoint.
 *
 * Attributes BatteryVoltage (0x0020, unit 100 mV) and
 * BatteryPercentageRemaining (0x0021, 0-200 = 0-100 %) are created with the
 * value 0xFF = invalid/unknown (ZBOSS zb_zcl_power_config.h), which
 * Zigbee2MQTT ignores (zigbee-herdsman-converters fromZigbee.ts). Real values
 * follow with the battery measurement (goal Z7). DEBUG builds report
 * APP_DEBUG_BATTERY_PERCENT as BatteryPercentageRemaining instead.
 *
 * @param ep_desc Endpoint descriptor (before registration)
 * @return ESP_OK on success
 */
esp_err_t power_config_cluster_add(ezb_af_ep_desc_t ep_desc);

#endif // POWER_CONFIG_CLUSTER_H
