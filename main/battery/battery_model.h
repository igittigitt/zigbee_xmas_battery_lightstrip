#ifndef BATTERY_MODEL_H
#define BATTERY_MODEL_H

#include <stdint.h>
#include "esp_err.h"

/** Value for "unknown" in the uint16 remaining-time fields (ZCL invalid value for uint16). */
#define BATTERY_UNKNOWN_U16     0xFFFF

/**
 * @brief Remaining time from the consumption model (goal Z14), in ZCL units.
 */
typedef struct {
    uint16_t on_time_x10h;      /**< Remaining light-on time at the current "on" level, 0.1 h units */
    uint16_t days_x10;          /**< Remaining days at the average daily use, 0.1 d units,
                                     BATTERY_UNKNOWN_U16 until one day of data exists */
    uint8_t percent;            /**< Remaining usable charge of the model in % (log only; the
                                     reported BatteryPercentageRemaining comes from the voltage, Z7) */
} battery_runtime_t;

/**
 * @brief Callback for a new model state (esp_timer task context).
 *
 * Zigbee APIs must be wrapped in esp_zigbee_lock_acquire()/_release() there.
 */
typedef void (*battery_model_cb_t)(const battery_runtime_t *runtime);

/**
 * @brief Initialise the consumption model (goal Z14).
 *
 * Loads the counters from NVS (nvs_flash_init() must have run). Without stored
 * counters it starts with a full battery, in DEBUG builds without ADC at
 * APP_DEBUG_BATTERY_PERCENT. Starts the periodic update timer
 * (BATTERY_MODEL_INTERVAL_MS, the only periodic wake-up of the model).
 * Call before the light and the Zigbee task start.
 *
 * @param cb Called after every periodic update, may be NULL
 * @return ESP_OK on success
 */
esp_err_t battery_model_init(battery_model_cb_t cb);

/**
 * @brief Report the first battery voltage measured at rest after boot.
 *
 * A battery change always means a reboot. If mv >= BATTERY_FULL_MV the cell
 * counts as freshly charged and the consumption counter is reset (and saved).
 *
 * @param mv Battery voltage in mV, measured with the light off
 */
void battery_model_boot_voltage(uint32_t mv);

/**
 * @brief Report a new output level of the light (light task).
 *
 * The time since the last call is booked with the previous current. Only
 * arithmetic under a spinlock, safe to call per ramp step.
 *
 * @param level Light output level 0..LIGHT_LEVEL_MAX
 */
void battery_model_set_load(uint8_t level);

/**
 * @brief Remaining time computed now.
 *
 * @param[out] runtime Remaining time in ZCL units
 */
void battery_model_get(battery_runtime_t *runtime);

#endif // BATTERY_MODEL_H
