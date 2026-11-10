#ifndef BATTERY_GUARD_H
#define BATTERY_GUARD_H

/*
 * Deep discharge protection (goal Z8), thresholds in battery_config.h:
 *   below BATTERY_WARN_MV      the chain flashes 3x at each battery measurement
 *                              (light off: short flashes, light on: short dark gaps)
 *   below BATTERY_SHUTDOWN_MV  light off, emergency deep sleep; wakeup by the
 *                              button or a timer, resume only from BATTERY_RESUME_MV
 *
 * Active in production and DEBUG level 3 (APP_BATTERY_GUARD). Not in DEBUG
 * level 1/2: there the ADC is optional and used for calibration on USB, also
 * below 3.0 V (README "Akkumessung kalibrieren").
 */

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Check the battery at startup, before the Zigbee stack starts.
 *
 * Call after light_init() (light off, pins latched). Measures once; below
 * BATTERY_SHUTDOWN_MV, or below BATTERY_RESUME_MV after an emergency deep
 * sleep, it goes (back) to emergency deep sleep and does not return.
 *
 * @return ESP_OK (also if the guard is disabled), or the error of the measurement
 */
esp_err_t battery_guard_boot_check(void);

/**
 * @brief Evaluate a regular battery measurement (non-blocking).
 *
 * Starts the warning pattern or the emergency shutdown. Called from the
 * measurement timer (esp_timer task) after each measurement.
 *
 * @param mv Measured battery voltage in mV (at rest or under load)
 */
void battery_guard_check(uint32_t mv);

#endif // BATTERY_GUARD_H
