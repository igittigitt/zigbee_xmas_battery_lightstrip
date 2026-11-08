#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "sdkconfig.h"

/**
 * @brief Primary build switch DEBUG level (Kconfig: ZL_DEBUG_LEVEL, menu "Zigbee Light").
 *
 * APP_DEBUG_LEVEL is 0 (production), 1 (general tests on USB) or 2 (energy
 * saving tests). APP_DEBUG is 1 for any DEBUG level. Both are always defined,
 * so they can be used both in preprocessor conditions and in regular C code
 * (`if (APP_DEBUG) { ... }`, keeps both branches compiled and type-checked).
 *
 * Currently selected:
 *   - light driver backend: level 1 = on-board WS2812 + PWM (APP_DEBUG_LIGHT),
 *     otherwise external PWM LED driver only
 *   - light sleep: blocked in level 1 only (APP_LIGHT_SLEEP)
 *   - diagnostic logs (GPIO sleep report, TX power): APP_DEBUG
 *   - battery: APP_DEBUG = emulated BatteryPercentageRemaining
 *     (APP_DEBUG_BATTERY_PERCENT) unless ZL_DEBUG_BATTERY_ADC (APP_BATTERY_ADC)
 */
#define APP_DEBUG_LEVEL CONFIG_ZL_DEBUG_LEVEL

#if CONFIG_ZL_DEBUG
#define APP_DEBUG 1
#else
#define APP_DEBUG 0
#endif

/** On-board WS2812 driven in parallel to the PWM output (DEBUG level 1 only). */
#define APP_DEBUG_LIGHT (APP_DEBUG_LEVEL == 1)

/**
 * Automatic light sleep allowed: in production and DEBUG level 2. Level 1
 * blocks it so USB flashing and the console always work.
 */
#if CONFIG_ZL_DEBUG_LEVEL != 1
#define APP_LIGHT_SLEEP 1
#else
#define APP_LIGHT_SLEEP 0
#endif

/**
 * Battery measurement via ADC (goal Z7): always in production, in DEBUG only
 * with Kconfig ZL_DEBUG_BATTERY_ADC (USB powered prototype usually has no divider).
 */
#if !CONFIG_ZL_DEBUG || CONFIG_ZL_DEBUG_BATTERY_ADC
#define APP_BATTERY_ADC 1
#else
#define APP_BATTERY_ADC 0
#endif

/**
 * Emulated battery level in % for DEBUG builds without ADC measurement
 * (Kconfig ZL_DEBUG_BATTERY_PERCENT), always defined so it can be used in
 * regular C code. 0 otherwise.
 */
#if CONFIG_ZL_DEBUG && !CONFIG_ZL_DEBUG_BATTERY_ADC
#define APP_DEBUG_BATTERY_PERCENT CONFIG_ZL_DEBUG_BATTERY_PERCENT
#else
#define APP_DEBUG_BATTERY_PERCENT 0
#endif

#endif // APP_CONFIG_H
