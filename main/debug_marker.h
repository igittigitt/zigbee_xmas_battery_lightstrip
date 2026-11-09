#ifndef DEBUG_MARKER_H
#define DEBUG_MARKER_H

/*
 * PPK2 event markers (DEBUG level 3, APP_DEBUG_MARKERS).
 *
 * GPIOs that go to the logic inputs D0-D3 of the Nordic PPK2, so current peaks
 * can be assigned to firmware events (docs/power-measurement.md, section
 * "Ereignismarker"). Wiring: PPK2 logic VCC -> 3V3 of the board (level
 * reference), logic GND -> GND, D0-D3 -> the GPIOs below.
 *
 * In all other DEBUG levels the functions are empty inline stubs, the pins
 * stay untouched.
 */

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "app_config.h"

/** D0: high = awake, low = in automatic light sleep (esp_pm sleep callbacks). */
#define DEBUG_MARKER_GPIO_AWAKE     GPIO_NUM_2
/**
 * D1: toggles at every Zigbee signal and every ZCL action (debug_marker_toggle()).
 * A pulse would be too short: the handlers only queue work (a few us), the PPK2
 * samples every 10 us at 100 kS/s. Each edge = one event.
 */
#define DEBUG_MARKER_GPIO_ZIGBEE    GPIO_NUM_3
/** D2: high while the light task changes the level (incl. the PWM fade). */
#define DEBUG_MARKER_GPIO_LIGHT     GPIO_NUM_4
/** D3: high during a battery measurement (ADC unit created ... deleted). */
#define DEBUG_MARKER_GPIO_BATTERY   GPIO_NUM_5

/** Event markers that the application sets (D0 is driven by the sleep callbacks). */
typedef enum {
    DEBUG_MARKER_ZIGBEE = DEBUG_MARKER_GPIO_ZIGBEE,
    DEBUG_MARKER_LIGHT = DEBUG_MARKER_GPIO_LIGHT,
    DEBUG_MARKER_BATTERY = DEBUG_MARKER_GPIO_BATTERY,
} debug_marker_t;

#if APP_DEBUG_MARKERS

/**
 * @brief Configure the marker GPIOs, register the light sleep callbacks and
 *        silence the application log.
 *
 * Call after power_management_init() (esp_pm configured). The marker pins keep
 * their level in light sleep (gpio_sleep_sel_dis()).
 *
 * @return ESP_OK on success
 */
esp_err_t debug_marker_init(void);

/**
 * @brief Set an event marker (high = event active).
 *
 * Cheap enough to be called from the Zigbee task and the light task; not
 * nested per marker.
 */
static inline void debug_marker_set(debug_marker_t marker, bool active)
{
    gpio_set_level((gpio_num_t)marker, active ? 1 : 0);
}

/**
 * @brief Toggle an event marker (one edge per event), for events that are too
 *        short for a visible pulse. Not for concurrent use from several tasks.
 */
void debug_marker_toggle(debug_marker_t marker);

#else

static inline void debug_marker_toggle(debug_marker_t marker)
{
    (void)marker;
}

static inline esp_err_t debug_marker_init(void)
{
    return ESP_OK;
}

static inline void debug_marker_set(debug_marker_t marker, bool active)
{
    (void)marker;
    (void)active;
}

#endif // APP_DEBUG_MARKERS

#endif // DEBUG_MARKER_H
