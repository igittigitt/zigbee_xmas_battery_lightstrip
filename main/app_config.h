#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "sdkconfig.h"

/**
 * @brief Primary build switch DEBUG (Kconfig: CONFIG_ZL_DEBUG, menu "Zigbee Light").
 *
 * APP_DEBUG is always defined as 0 or 1, so it can be used both in
 * preprocessor conditions (`#if APP_DEBUG`) and in regular C code
 * (`if (APP_DEBUG) { ... }`, keeps both branches compiled and type-checked).
 *
 * Currently selected by APP_DEBUG:
 *   - light driver backend: 1 = on-board WS2812, 0 = external PWM LED driver
 *   - light sleep: 1 = off unless ZL_DEBUG_LIGHT_SLEEP (APP_LIGHT_SLEEP),
 *     GPIO sleep report in the log at startup
 *   - battery: 1 = emulated BatteryPercentageRemaining (APP_DEBUG_BATTERY_PERCENT)
 */
#if CONFIG_ZL_DEBUG
#define APP_DEBUG 1
#else
#define APP_DEBUG 0
#endif

/**
 * Automatic light sleep allowed: always in production, in DEBUG only with
 * Kconfig ZL_DEBUG_LIGHT_SLEEP (otherwise USB may stop responding, see Kconfig help).
 */
#if !CONFIG_ZL_DEBUG || CONFIG_ZL_DEBUG_LIGHT_SLEEP
#define APP_LIGHT_SLEEP 1
#else
#define APP_LIGHT_SLEEP 0
#endif

/**
 * Emulated battery level in % for DEBUG builds (Kconfig ZL_DEBUG_BATTERY_PERCENT),
 * always defined so it can be used in `if (APP_DEBUG)` code. 0 in production.
 */
#if CONFIG_ZL_DEBUG
#define APP_DEBUG_BATTERY_PERCENT CONFIG_ZL_DEBUG_BATTERY_PERCENT
#else
#define APP_DEBUG_BATTERY_PERCENT 0
#endif

#endif // APP_CONFIG_H
