#ifndef POWER_CONFIG_CLUSTER_H
#define POWER_CONFIG_CLUSTER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_zigbee.h"

/**
 * @brief Add the Power Configuration cluster (server) to an endpoint.
 *
 * Attributes BatteryVoltage (0x0020, unit 100 mV, rounded down) and
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
 * BatteryVoltage is always updated; BatteryPercentageRemaining from a
 * measurement at rest (light off, BATTERY_OCV_TABLE) or with the light on at
 * full brightness (BATTERY_LOAD_TABLE), smoothed (BATTERY_PERCENT_RISE_MIN).
 * Dimmed or during a ramp the state of charge is kept.
 * Whether a report is sent depends on the reporting configuration set up by
 * the coordinator.
 *
 * Acquires the Zigbee lock: call from any task except the Zigbee task and its
 * callbacks. Only call with battery measurement enabled (APP_BATTERY_ADC).
 *
 * @return ESP_OK, or the error of the measurement (attributes unchanged)
 */
esp_err_t power_config_cluster_update(void);

/**
 * @brief Measure the battery now and update the attributes, in the Zigbee task.
 *
 * Same as power_config_cluster_update(), but without taking the Zigbee lock:
 * for the Zigbee task and its callbacks (read requests, read_request_handler.c).
 * Blocks for the measurement (a few ms).
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE without battery measurement (DEBUG
 *         without ADC, or ADC init failed), or the error of the measurement
 *         (attributes unchanged)
 */
esp_err_t power_config_cluster_refresh(void);

/**
 * @brief Last measured battery voltage in mV (exact, not in 100 mV steps).
 *
 * Used as initial value of the BatteryVoltageMv attribute in cluster 0xFC00,
 * so power_config_cluster_add() must run before battery_runtime_cluster_add().
 *
 * @return Voltage in mV, or BATTERY_UNKNOWN_U16 (0xFFFF) if not measured yet
 *         (no ADC in DEBUG, or measurement failed)
 */
uint16_t power_config_cluster_get_voltage_mv(void);

#endif // POWER_CONFIG_CLUSTER_H
