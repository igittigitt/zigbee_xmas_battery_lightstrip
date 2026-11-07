#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

/** GPIOs reported in the DEBUG sleep GPIO report, in addition to the light backend pins. */
#define SLEEP_REPORT_EXTRA_GPIO_MASK (1ULL << GPIO_NUM_9)   // BOOT button (goal Z5)

/**
 * @brief Enable automatic light sleep (Sleepy End Device, goal Z1).
 *
 * Configures esp_pm with a fixed CPU frequency (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
 * no DFS) and light_sleep_enable, as in the esp-zigbee-sdk 2.0.5 light sleep
 * example. The Zigbee stack then sleeps automatically between keepalives;
 * there is no application sleep call in esp-zigbee-lib 2.x.
 * Requires CONFIG_PM_ENABLE and CONFIG_FREERTOS_USE_TICKLESS_IDLE.
 * Call before starting the Zigbee task.
 *
 * @return ESP_OK on success
 */
esp_err_t power_management_init(void);

/**
 * @brief Log the sleep setup (DEBUG builds: GPIO behaviour in light sleep).
 *
 * Call once after all peripherals (light module) are initialised.
 */
void power_management_report(void);

/**
 * @brief true if automatic light sleep is currently only simulated
 *        (DEBUG build and USB Serial/JTAG connected, see Kconfig ZL_DEBUG).
 */
bool power_management_sleep_is_simulated(void);

#endif // POWER_MANAGEMENT_H
