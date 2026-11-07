#ifndef LIGHT_DRIVER_H
#define LIGHT_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Hardware backend of the light module.
 *
 * All functions are called from the light task only (never concurrently),
 * so they may block. Levels are in the range 0..LIGHT_LEVEL_MAX.
 */
typedef struct {
    /** Backend name for logging. */
    const char *name;

    /** Bit mask of the GPIOs used by this backend (for diagnostics). */
    uint64_t gpio_mask;

    /** Initialise hardware; output must be off afterwards. */
    esp_err_t (*init)(void);

    /**
     * Switch the output stage (e.g. boost converter, PWM generator) on/off.
     * Called with true before ramping up from level 0, and with false after
     * the level has reached 0. May be NULL if not needed.
     */
    esp_err_t (*power)(bool on);

    /** Set output level immediately. */
    esp_err_t (*set_level)(uint8_t level);

    /**
     * Fade from the current to the target level within duration_ms and
     * return when done. May be NULL: the light module then ramps in
     * software using set_level().
     */
    esp_err_t (*fade)(uint8_t target, uint32_t duration_ms);
} light_driver_t;

/** Backend: on-board WS2812 via RMT (DEBUG). */
extern const light_driver_t light_driver_ws2812;

/** Backend: external LED driver via LEDC PWM + enable pin (production). */
extern const light_driver_t light_driver_pwm;

#endif // LIGHT_DRIVER_H
