/*
 * Light backend for production builds: external constant current LED driver
 * dimmed by LEDC PWM, supplied by a boost converter with enable pin.
 *
 * - LEDC_SLEEP_MODE_NO_ALIVE_ALLOW_PD: LEDC registers are saved by the sleep
 *   retention, so the peripheral (TOP) power domain may power down in light
 *   sleep. LEDC_SLEEP_MODE_KEEP_ALIVE kept the domain and RC_FAST on even
 *   with the light off and cost about 250 uA (docs/known-issues.md, 18);
 *   with IDF 5.5.4 there is no way back from KEEP_ALIVE once configured.
 * - The PWM does not run in light sleep. Static states are "parked" instead:
 *   off (CE and EN low) and full brightness (CE and EN high). The PWM is
 *   stopped at the matching idle level and both pins are latched with
 *   gpio_hold_en() (GPIO 10/11 are LP pads, the hold survives the domain
 *   power-down; driver/gpio.h). Light sleep is allowed in both states.
 *   A floating CE would switch the LED driver fully on (module pull-up,
 *   docs/hardware.md).
 * - Ramps (LEDC hardware fade, 1 s on/off) and dimmed levels need the running
 *   PWM: PM lock ESP_PM_NO_LIGHT_SLEEP while not parked. The chain is normally
 *   used at full brightness only (overall brightness via the potentiometer on
 *   the LED driver), so the chip is awake only during the ramps.
 *   Measured 2026-11-09 (without LED chain): awake ~10 mA, asleep with the
 *   light off ~127 uA.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_pm.h"
#include "app_config.h"
#include "light_config.h"
#include "light_driver.h"

static const char *TAG = "LIGHT_PWM";

#if APP_DEBUG_DIAG_NO_LEDC

/*
 * Sleep current diagnosis (Kconfig ZL_DEBUG_DIAG_NO_LEDC): no LEDC at all.
 * PWM pin and enable pin are plain outputs held low (also in light sleep),
 * so the LED driver stays off and only the LEDC part differs from a normal build.
 */
static esp_err_t pwm_power(bool on)
{
    (void)on;
    return ESP_OK;
}

static esp_err_t pwm_set_level(uint8_t level)
{
    (void)level;
    return ESP_OK;
}

static esp_err_t pwm_fade(uint8_t target, uint32_t duration_ms)
{
    (void)target;
    (void)duration_ms;
    return ESP_OK;
}

static esp_err_t pwm_init(void)
{
    const gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << LIGHT_PWM_GPIO) | (1ULL << LIGHT_PWM_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_cfg), TAG, "gpio_config");
    ESP_RETURN_ON_ERROR(gpio_set_level(LIGHT_PWM_GPIO, 0), TAG, "PWM pin low");
    ESP_RETURN_ON_ERROR(gpio_set_level(LIGHT_PWM_EN_GPIO, 0), TAG, "EN off");
    ESP_RETURN_ON_ERROR(gpio_sleep_sel_dis(LIGHT_PWM_GPIO), TAG, "PWM pin sleep_sel_dis");
    ESP_RETURN_ON_ERROR(gpio_sleep_sel_dis(LIGHT_PWM_EN_GPIO), TAG, "EN sleep_sel_dis");
    ESP_LOGW(TAG, "DIAGNOSIS: LEDC not used, GPIO %d and %d held low, light stays off",
             LIGHT_PWM_GPIO, LIGHT_PWM_EN_GPIO);
    return ESP_OK;
}

#else

#define PWM_MODE        LEDC_LOW_SPEED_MODE
#define PWM_TIMER       LEDC_TIMER_0
#define PWM_CHANNEL     LEDC_CHANNEL_0
#define PWM_DUTY_MAX    ((1U << LIGHT_PWM_DUTY_RESOLUTION) - 1)

static uint32_t level_to_duty(uint8_t level)
{
    return ((uint32_t)level * PWM_DUTY_MAX) / LIGHT_LEVEL_MAX;
}

/*
 * Output state:
 *   ACTIVE       PWM running (ramp or dimmed level), PM lock held, no light sleep
 *   PARKED_LOW   off: PWM stopped at idle level 0, EN low, both pins held
 *   PARKED_HIGH  full brightness: PWM stopped at idle level 1 (CE constantly
 *                high, 100 % instead of the 4095/4096 duty), EN high, both
 *                pins held; light sleep allowed while the chain is lit
 */
typedef enum {
    PWM_STATE_ACTIVE,
    PWM_STATE_PARKED_LOW,
    PWM_STATE_PARKED_HIGH,
} pwm_state_t;

static pwm_state_t s_state = PWM_STATE_ACTIVE;
static esp_pm_lock_handle_t s_pm_lock;     // held in PWM_STATE_ACTIVE only

/** Stop the PWM at a static level, latch CE and EN, allow light sleep. */
static esp_err_t pwm_park(bool high)
{
    const uint32_t level = high ? 1 : 0;

    ESP_RETURN_ON_ERROR(ledc_stop(PWM_MODE, PWM_CHANNEL, level), TAG, "ledc_stop");
    // Let the timer run until the stop (idle level) is applied, otherwise the
    // output can freeze at the last PWM level when the timer is paused (light_config.h)
    vTaskDelay(pdMS_TO_TICKS(LIGHT_PWM_STOP_LATCH_MS) + 1);
    ESP_RETURN_ON_ERROR(ledc_timer_pause(PWM_MODE, PWM_TIMER), TAG, "timer pause");
    ESP_RETURN_ON_ERROR(gpio_set_level(LIGHT_PWM_EN_GPIO, level), TAG, "EN level");

    // Re-latch: a pad that is still held (e.g. from before a reset, hold
    // survives it, driver/gpio.h) would keep its old level otherwise.
    ESP_RETURN_ON_ERROR(gpio_hold_dis(LIGHT_PWM_GPIO), TAG, "PWM hold_dis");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(LIGHT_PWM_EN_GPIO), TAG, "EN hold_dis");
    ESP_RETURN_ON_ERROR(gpio_hold_en(LIGHT_PWM_GPIO), TAG, "PWM hold_en");
    ESP_RETURN_ON_ERROR(gpio_hold_en(LIGHT_PWM_EN_GPIO), TAG, "EN hold_en");

    if (s_state == PWM_STATE_ACTIVE) {
        ESP_RETURN_ON_ERROR(esp_pm_lock_release(s_pm_lock), TAG, "PM lock release");
    }
    s_state = high ? PWM_STATE_PARKED_HIGH : PWM_STATE_PARKED_LOW;
    return ESP_OK;
}

/** Back to PWM operation: keep the chip awake, enable the output stage, release the pins. */
static esp_err_t pwm_unpark(void)
{
    if (s_state == PWM_STATE_ACTIVE) {
        return ESP_OK;
    }
    const bool from_off = (s_state == PWM_STATE_PARKED_LOW);

    ESP_RETURN_ON_ERROR(esp_pm_lock_acquire(s_pm_lock), TAG, "PM lock acquire");
    s_state = PWM_STATE_ACTIVE;
    ESP_RETURN_ON_ERROR(gpio_set_level(LIGHT_PWM_EN_GPIO, 1), TAG, "EN on");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(LIGHT_PWM_EN_GPIO), TAG, "EN hold_dis");
    // PWM pin: LEDC idle level equals the held level, no glitch on release
    ESP_RETURN_ON_ERROR(gpio_hold_dis(LIGHT_PWM_GPIO), TAG, "PWM hold_dis");
    ESP_RETURN_ON_ERROR(ledc_timer_resume(PWM_MODE, PWM_TIMER), TAG, "timer resume");
    if (from_off) {
        vTaskDelay(pdMS_TO_TICKS(LIGHT_PWM_EN_SETTLE_MS));
    }
    return ESP_OK;
}

/** After a level change: full brightness is held statically so the chip can sleep. */
static esp_err_t pwm_level_reached(uint8_t level)
{
    return (level == LIGHT_LEVEL_MAX) ? pwm_park(true) : ESP_OK;
}

static esp_err_t pwm_power(bool on)
{
    // Off: park low. On: only leave the parked state; the level follows via
    // set_level()/fade(). Level 0 stays ACTIVE until power(false).
    return on ? pwm_unpark() : pwm_park(false);
}

static esp_err_t pwm_set_level(uint8_t level)
{
    ESP_RETURN_ON_ERROR(pwm_unpark(), TAG, "unpark");
    ESP_RETURN_ON_ERROR(ledc_set_duty(PWM_MODE, PWM_CHANNEL, level_to_duty(level)), TAG, "set_duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(PWM_MODE, PWM_CHANNEL), TAG, "update_duty");
    return pwm_level_reached(level);
}

static esp_err_t pwm_fade(uint8_t target, uint32_t duration_ms)
{
    ESP_RETURN_ON_ERROR(pwm_unpark(), TAG, "unpark");
    ESP_RETURN_ON_ERROR(ledc_set_fade_with_time(PWM_MODE, PWM_CHANNEL, level_to_duty(target),
                                                (int)duration_ms), TAG, "set_fade");
    ESP_RETURN_ON_ERROR(ledc_fade_start(PWM_MODE, PWM_CHANNEL, LEDC_FADE_WAIT_DONE), TAG, "fade_start");
    return pwm_level_reached(target);
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
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "light_pwm", &s_pm_lock),
                        TAG, "PM lock create");
    // Start in PWM_STATE_ACTIVE (lock held); pwm_power(false) below parks low
    ESP_RETURN_ON_ERROR(esp_pm_lock_acquire(s_pm_lock), TAG, "PM lock acquire");

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
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_ALLOW_PD,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&chan_cfg), TAG, "channel_config");
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "fade_func_install");

    ESP_LOGI(TAG, "PWM on GPIO %d (%d Hz), EN on GPIO %d",
             LIGHT_PWM_GPIO, LIGHT_PWM_FREQ_HZ, LIGHT_PWM_EN_GPIO);
    return pwm_power(false);
}

#endif // APP_DEBUG_DIAG_NO_LEDC

const light_driver_t light_driver_pwm = {
    .name = "pwm",
    .gpio_mask = (1ULL << LIGHT_PWM_GPIO) | (1ULL << LIGHT_PWM_EN_GPIO),
    .init = pwm_init,
    .power = pwm_power,
    .set_level = pwm_set_level,
    .fade = pwm_fade,
};
