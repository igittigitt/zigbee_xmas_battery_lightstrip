/*
 * Light backend for production builds: external constant current LED driver
 * dimmed by LEDC PWM, supplied by a boost converter with enable pin.
 *
 * - Clock RC_FAST + LEDC_SLEEP_MODE_KEEP_ALIVE: PWM keeps running in light
 *   sleep (IDF v5.5.1 LEDC docs, "Power Management"). The driver itself then
 *   calls gpio_sleep_sel_dis() for the PWM pin (esp_driver_ledc/src/ledc.c).
 * - Fades use the LEDC hardware fade, no CPU wakeups during the ramp.
 * - LED off: PWM output stopped, timer paused, boost disabled (goal Z9).
 *
 * Not yet verified on hardware (LED driver and boost module still [OFFEN]).
 */
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "light_config.h"
#include "light_driver.h"

static const char *TAG = "LIGHT_PWM";

#define PWM_MODE        LEDC_LOW_SPEED_MODE
#define PWM_TIMER       LEDC_TIMER_0
#define PWM_CHANNEL     LEDC_CHANNEL_0
#define PWM_DUTY_MAX    ((1U << LIGHT_PWM_DUTY_RESOLUTION) - 1)

static uint32_t level_to_duty(uint8_t level)
{
    return ((uint32_t)level * PWM_DUTY_MAX) / LIGHT_LEVEL_MAX;
}

static esp_err_t pwm_power(bool on)
{
    if (on) {
        ESP_RETURN_ON_ERROR(gpio_set_level(LIGHT_PWM_EN_GPIO, 1), TAG, "EN on");
        ESP_RETURN_ON_ERROR(ledc_timer_resume(PWM_MODE, PWM_TIMER), TAG, "timer resume");
        vTaskDelay(pdMS_TO_TICKS(LIGHT_PWM_EN_SETTLE_MS));
    } else {
        ESP_RETURN_ON_ERROR(ledc_stop(PWM_MODE, PWM_CHANNEL, 0), TAG, "ledc_stop");
        ESP_RETURN_ON_ERROR(ledc_timer_pause(PWM_MODE, PWM_TIMER), TAG, "timer pause");
        ESP_RETURN_ON_ERROR(gpio_set_level(LIGHT_PWM_EN_GPIO, 0), TAG, "EN off");
    }
    return ESP_OK;
}

static esp_err_t pwm_set_level(uint8_t level)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(PWM_MODE, PWM_CHANNEL, level_to_duty(level)), TAG, "set_duty");
    return ledc_update_duty(PWM_MODE, PWM_CHANNEL);
}

static esp_err_t pwm_fade(uint8_t target, uint32_t duration_ms)
{
    ESP_RETURN_ON_ERROR(ledc_set_fade_with_time(PWM_MODE, PWM_CHANNEL, level_to_duty(target),
                                                (int)duration_ms), TAG, "set_fade");
    return ledc_fade_start(PWM_MODE, PWM_CHANNEL, LEDC_FADE_WAIT_DONE);
}

static esp_err_t pwm_init(void)
{
    const gpio_config_t en_cfg = {
        .pin_bit_mask = 1ULL << LIGHT_PWM_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&en_cfg), TAG, "EN gpio_config");
    ESP_RETURN_ON_ERROR(gpio_set_level(LIGHT_PWM_EN_GPIO, 0), TAG, "EN off");
    // Keep EN driven during light sleep (CONFIG_PM_SLP_DISABLE_GPIO would release it)
    ESP_RETURN_ON_ERROR(gpio_sleep_sel_dis(LIGHT_PWM_EN_GPIO), TAG, "EN sleep_sel_dis");

    const ledc_timer_config_t timer_cfg = {
        .speed_mode = PWM_MODE,
        .duty_resolution = LIGHT_PWM_DUTY_RESOLUTION,
        .timer_num = PWM_TIMER,
        .freq_hz = LIGHT_PWM_FREQ_HZ,
        .clk_cfg = LEDC_USE_RC_FAST_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "timer_config");

    const ledc_channel_config_t chan_cfg = {
        .gpio_num = LIGHT_PWM_GPIO,
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&chan_cfg), TAG, "channel_config");
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "fade_func_install");

    ESP_LOGI(TAG, "PWM on GPIO %d (%d Hz), EN on GPIO %d",
             LIGHT_PWM_GPIO, LIGHT_PWM_FREQ_HZ, LIGHT_PWM_EN_GPIO);
    return pwm_power(false);
}

const light_driver_t light_driver_pwm = {
    .name = "pwm",
    .gpio_mask = (1ULL << LIGHT_PWM_GPIO) | (1ULL << LIGHT_PWM_EN_GPIO),
    .init = pwm_init,
    .power = pwm_power,
    .set_level = pwm_set_level,
    .fade = pwm_fade,
};
