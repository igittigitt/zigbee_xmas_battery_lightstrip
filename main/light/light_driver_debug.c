/*
 * Light backend for DEBUG level 1: drives the on-board WS2812 (visual feedback
 * on the prototype) AND the production PWM output (LED driver on
 * LIGHT_PWM_GPIO, enable on LIGHT_PWM_EN_GPIO) in parallel, so the real light
 * chain can be tested on the DEBUG build.
 *
 * No hardware fade: the WS2812 has none, so light.c ramps in software via
 * set_level() (LIGHT_SW_RAMP_STEP_MS steps) and both outputs follow the same
 * level. The PWM output therefore ramps in steps instead of the LEDC hardware
 * fade used in production.
 */
#include "esp_log.h"
#include "light_config.h"
#include "light_driver.h"

static const char *TAG = "LIGHT_DEBUG";

static esp_err_t debug_init(void)
{
    esp_err_t err = light_driver_ws2812.init();
    if (err != ESP_OK) {
        return err;
    }
    err = light_driver_pwm.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PWM output init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DEBUG: WS2812 and PWM output driven in parallel");
    return ESP_OK;
}

static esp_err_t debug_power(bool on)
{
    // The WS2812 backend has no power function, only the PWM output stage
    return light_driver_pwm.power(on);
}

static esp_err_t debug_set_level(uint8_t level)
{
    esp_err_t err_ws = light_driver_ws2812.set_level(level);
    esp_err_t err_pwm = light_driver_pwm.set_level(level);
    if (err_pwm != ESP_OK) {
        ESP_LOGE(TAG, "PWM set_level failed: %s", esp_err_to_name(err_pwm));
    }
    return err_ws != ESP_OK ? err_ws : err_pwm;
}

const light_driver_t light_driver_debug = {
    .name = "ws2812+pwm",
    .gpio_mask = (1ULL << LIGHT_WS2812_GPIO) | (1ULL << LIGHT_PWM_GPIO) | (1ULL << LIGHT_PWM_EN_GPIO),
    .init = debug_init,
    .power = debug_power,
    .set_level = debug_set_level,
    .fade = NULL,       // software ramp in light.c
};
