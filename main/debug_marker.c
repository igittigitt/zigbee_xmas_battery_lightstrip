#include "app_config.h"
#include "debug_marker.h"

#if APP_DEBUG_MARKERS

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_pm.h"

static const char *TAG = "DEBUG_MARKER";

#define MARKER_PIN_MASK ((1ULL << DEBUG_MARKER_GPIO_AWAKE) | (1ULL << DEBUG_MARKER_GPIO_ZIGBEE) | \
                         (1ULL << DEBUG_MARKER_GPIO_LIGHT) | (1ULL << DEBUG_MARKER_GPIO_BATTERY))

/*
 * Light sleep callbacks (esp_pm.h, Kconfig PM_LIGHT_SLEEP_CALLBACKS). They are
 * executed from IRAM in the IDLE task directly before / after the automatic
 * light sleep and must be short; gpio_set_level() is in IRAM via
 * GPIO_CTRL_FUNC_IN_IRAM (selected by DEBUG level 3).
 */
static esp_err_t IRAM_ATTR sleep_enter_cb(int64_t sleep_time_us, void *arg)
{
    gpio_set_level(DEBUG_MARKER_GPIO_AWAKE, 0);
    return ESP_OK;
}

static esp_err_t IRAM_ATTR sleep_exit_cb(int64_t sleep_time_us, void *arg)
{
    gpio_set_level(DEBUG_MARKER_GPIO_AWAKE, 1);
    return ESP_OK;
}

/* Output levels for debug_marker_toggle() (output-only pins cannot be read back). */
static uint64_t s_toggle_levels;

void debug_marker_toggle(debug_marker_t marker)
{
    const uint64_t bit = 1ULL << (int)marker;
    s_toggle_levels ^= bit;
    gpio_set_level((gpio_num_t)marker, (s_toggle_levels & bit) ? 1 : 0);
}

esp_err_t debug_marker_init(void)
{
    const gpio_config_t io_cfg = {
        .pin_bit_mask = MARKER_PIN_MASK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_cfg), TAG, "GPIO config");

    for (int gpio = 0; gpio < GPIO_NUM_MAX; gpio++) {
        if (MARKER_PIN_MASK & (1ULL << gpio)) {
            gpio_set_level((gpio_num_t)gpio, 0);
            // Keep the active configuration and level in light sleep; with
            // CONFIG_PM_SLP_DISABLE_GPIO the pad would otherwise be released
            // (floating input at the PPK2) during sleep.
            ESP_RETURN_ON_ERROR(gpio_sleep_sel_dis((gpio_num_t)gpio), TAG, "sleep_sel_dis GPIO %d", gpio);
        }
    }
    gpio_set_level(DEBUG_MARKER_GPIO_AWAKE, 1);

    esp_pm_sleep_cbs_register_config_t cbs = {
        .enter_cb = sleep_enter_cb,
        .exit_cb = sleep_exit_cb,
    };
    ESP_RETURN_ON_ERROR(esp_pm_light_sleep_register_cbs(&cbs), TAG, "Sleep callbacks");

    ESP_LOGW(TAG, "DEBUG level 3: PPK2 markers D0-D3 = GPIO %d/%d/%d/%d (awake/zigbee/light/battery), "
             "log silenced from here on", DEBUG_MARKER_GPIO_AWAKE, DEBUG_MARKER_GPIO_ZIGBEE,
             DEBUG_MARKER_GPIO_LIGHT, DEBUG_MARKER_GPIO_BATTERY);
    // A disabled ESP_LOGx only compares the level; no UART output keeps the CPU awake.
    esp_log_level_set("*", ESP_LOG_NONE);
    return ESP_OK;
}

#endif // APP_DEBUG_MARKERS
