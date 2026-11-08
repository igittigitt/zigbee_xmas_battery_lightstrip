#ifndef LIGHT_H
#define LIGHT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialise the light module and its hardware backend.
 *
 * The backend is selected by the DEBUG switch (APP_DEBUG, app_config.h):
 * DEBUG = on-board WS2812, otherwise external PWM LED driver.
 * Starts the light task. The light is off afterwards.
 *
 * @return ESP_OK on success
 */
esp_err_t light_init(void);

/**
 * @brief Fade to a level within ramp_ms (non-blocking).
 *
 * Safe to call from the Zigbee task and its callbacks: the request is only
 * queued, the ramp is executed by the light task. A newer request replaces
 * a pending one.
 *
 * @param level   Target level 0..LIGHT_LEVEL_MAX
 * @param ramp_ms Ramp duration for a full 0 <-> 100 % transition; 0 = immediate
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if not initialised
 */
esp_err_t light_fade_to(uint8_t level, uint32_t ramp_ms);

/**
 * @brief Switch on/off with the default soft-start/-stop ramp (goal Z3).
 *
 * @param on true = "on" level (light_set_on_level(), default LIGHT_LEVEL_ON), false = 0
 * @return see light_fade_to()
 */
esp_err_t light_set_on(bool on);

/**
 * @brief Set the brightness used for "on" (dimming, non-blocking).
 *
 * If the light is on, it fades to the new level with the soft ramp
 * (LIGHT_RAMP_MS, goal Z3); if it is off, the level is only stored and used
 * by the next light_set_on(true) / light_pattern_then(..., true).
 *
 * @param level 1..LIGHT_LEVEL_MAX (0 is treated as 1)
 * @return see light_fade_to()
 */
esp_err_t light_set_on_level(uint8_t level);

/**
 * @brief Flash pattern: "on" phase at level, then "off" phase, repeated.
 *
 * Phases use short ramps (LIGHT_BLINK_RAMP_MS) instead of hard switching.
 */
typedef struct {
    uint8_t level;      /**< Level of the "on" phases, 0..LIGHT_LEVEL_MAX */
    uint32_t on_ms;     /**< Duration of an "on" phase, >= LIGHT_BLINK_RAMP_MS */
    uint32_t off_ms;    /**< Duration of an "off" phase, >= LIGHT_BLINK_RAMP_MS;
                             0 only with count = 1 (steady pulse, then directly to the final state) */
    uint32_t count;     /**< Number of flashes, 0 = endless (until the next command) */
} light_pattern_t;

/**
 * @brief Run a flash pattern, then return to the current state (non-blocking).
 *
 * Afterwards the level last set via light_fade_to()/light_set_on() is
 * restored. Any later light_*() call aborts the pattern. Used for Identify
 * (goal Z11). Safe to call from the Zigbee task and its callbacks.
 *
 * @param pattern Pattern (copied)
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_ERR_INVALID_STATE if not initialised
 */
esp_err_t light_pattern_start(const light_pattern_t *pattern);

/**
 * @brief Run a flash pattern, then switch on/off with the soft ramp (non-blocking).
 *
 * Like light_pattern_start(), but the state after the pattern is on/off
 * (becomes the new light state). With count = 0 the pattern runs until the
 * next light_*() call. Used for the connection indication (goals Z10, Z12).
 *
 * @param pattern Pattern (copied)
 * @param on      State after the pattern
 * @return see light_pattern_start()
 */
esp_err_t light_pattern_then(const light_pattern_t *pattern, bool on);

/**
 * @brief Last requested target level (not the momentary ramp value).
 */
uint8_t light_get_level(void);

/**
 * @brief Bit mask of the GPIOs used by the active backend (0 before init).
 */
uint64_t light_get_gpio_mask(void);

#endif // LIGHT_H
