#include <inttypes.h>
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "battery.h"
#include "battery_config.h"
#include "battery_guard.h"
#include "button.h"
#include "light.h"
#include "light_config.h"

static const char *TAG = "BATTERY_GUARD";

#define SHUTDOWN_TASK_STACK_SIZE    3072
#define SHUTDOWN_TASK_PRIORITY      5

/*
 * Set directly before the emergency deep sleep. RTC (LP) memory keeps its
 * content in deep sleep and is initialised at power-on (esp_attr.h,
 * RTC_DATA_ATTR), so after a BMS cut-off or battery change it is false.
 */
static RTC_DATA_ATTR bool s_rtc_emergency;

static volatile bool s_shutdown_started;

/*
 * Drive all light pins low and latch them, independent of the light task
 * state (a Zigbee command could have switched the light on again during the
 * ramp). gpio_config() detaches the LEDC signal from the pin
 * (gpio_output_enable(): "No peripheral output signal routed to the pin").
 * The hold keeps the level in deep sleep (driver/gpio.h).
 */
static void light_pins_off(void)
{
    const uint64_t mask = light_get_gpio_mask();
    const gpio_config_t io_cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (mask == 0 || gpio_config(&io_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Light pins could not be forced low");
        return;
    }
    for (int gpio = 0; gpio < GPIO_NUM_MAX; gpio++) {
        if (mask & (1ULL << gpio)) {
            gpio_set_level((gpio_num_t)gpio, 0);
            gpio_hold_dis((gpio_num_t)gpio);    // re-latch: a held pad keeps its old level
            gpio_hold_en((gpio_num_t)gpio);
        }
    }
}

/* Emergency deep sleep; wakeup by the button (EXT1) or the timer. Does not return. */
static void enter_emergency_sleep(void)
{
    light_pins_off();

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    // EXT1 works in deep sleep as well (LP pad GPIO 9, see button.c)
    if (esp_sleep_enable_ext1_wakeup_io(1ULL << BUTTON_GPIO, ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) {
        ESP_LOGE(TAG, "Button wakeup not available, timer only");
    }
    esp_sleep_enable_timer_wakeup(BATTERY_EMERGENCY_WAKE_MS * 1000);

    s_rtc_emergency = true;
    ESP_LOGW(TAG, "Emergency deep sleep (wakeup: button or after %" PRIu32 " min)",
             (uint32_t)(BATTERY_EMERGENCY_WAKE_MS / 60000));
    esp_deep_sleep_start();
}

/*
 * "Battery empty" signal, blocking (a few seconds): BATTERY_EMPTY_FLASH_COUNT
 * short flashes, then a last flash that fades out with the soft ramp, so the
 * chain does not simply look dead in the emergency sleep. Ends with the light
 * off. Called from app_main (boot check) or the shutdown task.
 */
static void empty_signal(void)
{
    const uint8_t level = (uint8_t)(LIGHT_LEVEL_MAX * BATTERY_EMPTY_LEVEL_PERCENT / 100);

    if (light_get_level() != 0) {
        // Light on: soft ramp down first (goal Z3, no current spike)
        light_fade_to(0, LIGHT_RAMP_MS);
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SHUTDOWN_LIGHT_WAIT_MS));
    }
    for (int i = 0; i < BATTERY_EMPTY_FLASH_COUNT; i++) {
        light_fade_to(level, LIGHT_BLINK_RAMP_MS);
        vTaskDelay(pdMS_TO_TICKS(BATTERY_WARN_FLASH_MS));
        light_fade_to(0, LIGHT_BLINK_RAMP_MS);
        vTaskDelay(pdMS_TO_TICKS(BATTERY_WARN_PAUSE_MS));
    }
    light_fade_to(level, LIGHT_BLINK_RAMP_MS);
    vTaskDelay(pdMS_TO_TICKS(BATTERY_WARN_FLASH_MS));
    light_fade_to(0, LIGHT_RAMP_MS);
    vTaskDelay(pdMS_TO_TICKS(BATTERY_SHUTDOWN_LIGHT_WAIT_MS));
}

static void shutdown_task(void *arg)
{
    empty_signal();
    enter_emergency_sleep();    // forces the pins low in any case
}

/* Woken by the timer only (not by the button): no signal, nobody is looking. */
static bool woken_by_timer_only(void)
{
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    return (causes & (1UL << ESP_SLEEP_WAKEUP_TIMER)) && !(causes & (1UL << ESP_SLEEP_WAKEUP_EXT1));
}

/* Warning: 3 short flashes if off, 3 short dark gaps if on; the light returns to its state. */
static void warn_flash(void)
{
    const bool on = (light_get_level() != 0);
    const light_pattern_t pattern = {
        .level = light_get_on_level(),
        // The pattern starts with the "on" phase at .level: if the light is on,
        // that phase is the bright gap and the "off" phase the dark flash.
        .on_ms = on ? BATTERY_WARN_PAUSE_MS : BATTERY_WARN_FLASH_MS,
        .off_ms = on ? BATTERY_WARN_FLASH_MS : BATTERY_WARN_PAUSE_MS,
        .count = BATTERY_WARN_FLASH_COUNT,
    };
    if (light_pattern_start(&pattern) != ESP_OK) {
        ESP_LOGE(TAG, "Warning pattern failed");
    }
}

esp_err_t battery_guard_boot_check(void)
{
    if (!APP_BATTERY_GUARD) {
        s_rtc_emergency = false;
        return ESP_OK;
    }
    const bool after_emergency = s_rtc_emergency && esp_reset_reason() == ESP_RST_DEEPSLEEP;

    ESP_RETURN_ON_ERROR(battery_init(), TAG, "Battery init");
    uint32_t mv = 0;
    ESP_RETURN_ON_ERROR(battery_measure_mv(&mv), TAG, "Battery measurement");

    const uint32_t limit = after_emergency ? BATTERY_RESUME_MV : BATTERY_SHUTDOWN_MV;
    if (mv < limit) {
        // Wakeup causes as bitmap of esp_sleep_source_t (diagnosis of the wake/sleep cycle)
        ESP_LOGW(TAG, "Battery %" PRIu32 " mV < %" PRIu32 " mV (reset reason %d, wakeup causes 0x%08" PRIx32 ")",
                 mv, limit, (int)esp_reset_reason(), esp_sleep_get_wakeup_causes());
        if (!woken_by_timer_only()) {
            empty_signal();     // button wakeup or reset: show "battery empty"
        }
        enter_emergency_sleep();
    }
    if (after_emergency) {
        ESP_LOGI(TAG, "Battery %" PRIu32 " mV, resuming after emergency sleep", mv);
    }
    s_rtc_emergency = false;
    return ESP_OK;
}

void battery_guard_check(uint32_t mv)
{
    if (!APP_BATTERY_GUARD || s_shutdown_started) {
        return;
    }
    if (mv < BATTERY_SHUTDOWN_MV) {
        s_shutdown_started = true;
        ESP_LOGW(TAG, "Battery %" PRIu32 " mV < %d mV: light off, emergency sleep", mv, BATTERY_SHUTDOWN_MV);
        if (xTaskCreate(shutdown_task, "bat_shutdown", SHUTDOWN_TASK_STACK_SIZE, NULL,
                        SHUTDOWN_TASK_PRIORITY, NULL) != pdPASS) {
            enter_emergency_sleep();    // no ramp, but the pins are forced low
        }
        return;
    }
    if (mv < BATTERY_WARN_MV) {
        ESP_LOGW(TAG, "Battery %" PRIu32 " mV < %d mV: warning", mv, BATTERY_WARN_MV);
        warn_flash();
    }
}
