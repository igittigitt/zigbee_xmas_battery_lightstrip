#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "sdkconfig.h"

/**
 * @brief Primary build switch DEBUG level (Kconfig: ZL_DEBUG_LEVEL, menu "Zigbee Light").
 *
 * APP_DEBUG_LEVEL is 0 (production), 1 (general tests on USB), 2 (energy
 * saving tests, UART log) or 3 (PPK2 current measurement: GPIO markers, no
 * log). APP_DEBUG is 1 for any DEBUG level. Both are always defined,
 * so they can be used both in preprocessor conditions and in regular C code
 * (`if (APP_DEBUG) { ... }`, keeps both branches compiled and type-checked).
 *
 * Currently selected:
 *   - light driver backend: level 1 = on-board WS2812 + PWM (APP_DEBUG_LIGHT),
 *     otherwise external PWM LED driver only
 *   - light sleep: blocked in level 1 only (APP_LIGHT_SLEEP)
 *   - diagnostic logs: GPIO sleep report APP_DEBUG; Zigbee diagnostics
 *     (neighbor table, TX power, join LQI per attempt, radio trace) level 1
 *     only (APP_DEBUG_ZIGBEE) - level 2 measures current and logs little
 *   - battery: DEBUG level 1/2 = emulated BatteryPercentageRemaining
 *     (APP_DEBUG_BATTERY_PERCENT) unless ZL_DEBUG_BATTERY_ADC; production and
 *     level 3 measure via ADC (APP_BATTERY_ADC)
 *   - level 3: PPK2 event markers on GPIOs, application log silenced
 *     (APP_DEBUG_MARKERS, main/debug_marker.h)
 */
#define APP_DEBUG_LEVEL CONFIG_ZL_DEBUG_LEVEL

#if CONFIG_ZL_DEBUG
#define APP_DEBUG 1
#else
#define APP_DEBUG 0
#endif

/**
 * Zigbee diagnostics (DEBUG level 1 = general and Zigbee debugging): whole
 * neighbor table, TX power, join LQI after every failed attempt. Level 2
 * (energy measurement) logs like production to keep wake time low.
 */
#define APP_DEBUG_ZIGBEE (APP_DEBUG_LEVEL == 1)

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
 * DEBUG: own active scan after each failed join attempt (Kconfig
 * ZL_DEBUG_DIAG_SCAN, default off so it cannot interfere with commissioning).
 */
#if CONFIG_ZL_DEBUG_DIAG_SCAN
#define APP_DEBUG_DIAG_SCAN 1
#else
#define APP_DEBUG_DIAG_SCAN 0
#endif

/**
 * DEBUG level 2: print PM locks and sleep statistics periodically (Kconfig
 * ZL_DEBUG_PM_DUMP, selects PM_PROFILING), see power_management.c.
 */
#if CONFIG_ZL_DEBUG_PM_DUMP
#define APP_DEBUG_PM_DUMP 1
#else
#define APP_DEBUG_PM_DUMP 0
#endif

/**
 * PPK2 event markers on GPIOs and silenced application log (DEBUG level 3,
 * current measurement), see main/debug_marker.h.
 */
#define APP_DEBUG_MARKERS (APP_DEBUG_LEVEL == 3)

/**
 * Battery measurement via ADC (goal Z7): in production and DEBUG level 3
 * (measures like production), in level 1/2 only with Kconfig
 * ZL_DEBUG_BATTERY_ADC (USB powered prototype usually has no divider).
 */
#if !CONFIG_ZL_DEBUG || CONFIG_ZL_DEBUG_LEVEL_3 || CONFIG_ZL_DEBUG_BATTERY_ADC
#define APP_BATTERY_ADC 1
#else
#define APP_BATTERY_ADC 0
#endif

/**
 * Emulated battery level in % for DEBUG builds without ADC measurement
 * (Kconfig ZL_DEBUG_BATTERY_PERCENT), always defined so it can be used in
 * regular C code. 0 otherwise.
 */
#if CONFIG_ZL_DEBUG && !CONFIG_ZL_DEBUG_LEVEL_3 && !CONFIG_ZL_DEBUG_BATTERY_ADC
#define APP_DEBUG_BATTERY_PERCENT CONFIG_ZL_DEBUG_BATTERY_PERCENT
#else
#define APP_DEBUG_BATTERY_PERCENT 0
#endif

#endif // APP_CONFIG_H
