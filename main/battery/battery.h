#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialise the battery measurement (ADC calibration).
 *
 * Resolves the ADC channel of BATTERY_ADC_GPIO and creates the eFuse based
 * calibration (curve fitting). The ADC unit itself is only created for the
 * duration of each measurement (see battery_measure_mv()).
 *
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if the chip has no ADC
 *         calibration data in eFuse (measurement unavailable)
 */
esp_err_t battery_init(void);

/**
 * @brief Measure the battery voltage (blocking, a few milliseconds).
 *
 * Creates the ADC oneshot unit, averages BATTERY_ADC_SAMPLES calibrated
 * samples, deletes the unit again and scales by the voltage divider.
 * The unit is deleted after every measurement because an existing unit keeps
 * the TOP and MODEM power domains on in light sleep when
 * CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP is set (IDF 5.5.4
 * components/esp_adc/adc_oneshot.c, adc_oneshot_new_unit / _del_unit).
 *
 * Not thread-safe: call from one task only. Does not use Zigbee APIs.
 *
 * @param[out] mv Battery voltage in mV
 * @return ESP_OK, ESP_ERR_INVALID_STATE if not initialised, or an ADC error
 */
esp_err_t battery_measure_mv(uint32_t *mv);

/**
 * @brief Estimate the state of charge from an open circuit voltage.
 *
 * Piecewise linear interpolation in BATTERY_OCV_TABLE (battery_config.h),
 * clamped to 0..100. Only meaningful for a voltage measured at rest (LED off).
 *
 * @param mv Battery voltage in mV
 * @return State of charge in percent, 0..100
 */
uint8_t battery_percent_from_mv(uint32_t mv);

/**
 * @brief Estimate the state of charge from a voltage measured under load.
 *
 * Like battery_percent_from_mv(), but with BATTERY_LOAD_TABLE, which applies
 * to the light on at full brightness (LIGHT_LEVEL_MAX).
 *
 * @param mv Battery voltage in mV, measured with the light on at full brightness
 * @return State of charge in percent, 0..100
 */
uint8_t battery_percent_from_mv_load(uint32_t mv);

#endif // BATTERY_H
