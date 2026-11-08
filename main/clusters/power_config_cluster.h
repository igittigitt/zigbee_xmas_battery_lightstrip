#ifndef POWER_CONFIG_CLUSTER_H
#define POWER_CONFIG_CLUSTER_H

#include "esp_err.h"
#include "esp_zigbee.h"

/**
 * @brief Add the Power Configuration cluster (server) to an endpoint.
 *
 * Attributes BatteryVoltage (0x0020, unit 100 mV) and
 * BatteryPercentageRemaining (0x0021, 0-200 = 0-100 %), both reportable.
 *
 * With battery measurement (APP_BATTERY_ADC: production, or DEBUG with
 * Kconfig ZL_DEBUG_BATTERY_ADC) the battery is measured once here (light is
 * still off) and then every BATTERY_MEASURE_INTERVAL_MS by an esp_timer,
 * see power_config_cluster_update(). Without it (DEBUG default)
 * BatteryPercentageRemaining is APP_DEBUG_BATTERY_PERCENT and BatteryVoltage
 * 0xFF = unknown, which Zigbee2MQTT ignores (zigbee-herdsman-converters
 * fromZigbee.ts). If the measurement fails, both stay 0xFF.
 *
 * Must be called in the Zigbee task after esp_zigbee_init() and before
 * registration (blocks a few milliseconds for the first measurement).
 *
 * @param ep_desc Endpoint descriptor (before registration)
 * @return ESP_OK on success
 */
esp_err_t power_config_cluster_add(ezb_af_ep_desc_t ep_desc);

/**
 * @brief Measure the battery now and update the attributes (goal Z7).
 *
 * BatteryVoltage is always updated; BatteryPercentageRemaining only from a
 * measurement at rest (light off) and smoothed (BATTERY_PERCENT_RISE_MIN).
 * Whether a report is sent depends on the reporting configuration set up by
 * the coordinator.
 *
 * Acquires the Zigbee lock: call from any task except the Zigbee task and its
 * callbacks. Only call with battery measurement enabled (APP_BATTERY_ADC).
 *
 * @return ESP_OK, or the error of the measurement (attributes unchanged)
 */
esp_err_t power_config_cluster_update(void);

#endif // POWER_CONFIG_CLUSTER_H
