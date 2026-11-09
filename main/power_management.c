#include <stdio.h>
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "soc/uart_pins.h"
#include "app_config.h"
#include "power_management.h"
#include "light.h"
#include "battery_config.h"

static const char *TAG = "POWER_MGMT";

#if defined(CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION)
#define SLEEP_USB_KEEPS_AWAKE   1
#else
#define SLEEP_USB_KEEPS_AWAKE   0
#endif

bool power_management_sleep_is_simulated(void)
{
    return APP_DEBUG && SLEEP_USB_KEEPS_AWAKE && usb_serial_jtag_is_connected();
}

/**
 * DEBUG: report how the project GPIOs behave in a real light sleep. The pads
 * are switched to their sleep configuration by hardware only on real sleep
 * entry (SLP_SEL, see gpio_sleep_sel_en() in driver/gpio.h); there is no
 * public API to apply it while awake, so the simulation cannot reproduce it.
 */
static void sleep_report_gpios(void)
{
    uint64_t mask = light_get_gpio_mask() | SLEEP_REPORT_EXTRA_GPIO_MASK |
                    (APP_BATTERY_ADC ? (1ULL << BATTERY_ADC_GPIO) : 0);

    ESP_LOGI(TAG, "GPIO report (behaviour in a REAL light sleep):");
    for (int gpio = 0; gpio < GPIO_NUM_MAX; gpio++) {
        if (!(mask & (1ULL << gpio))) {
            continue;
        }
        gpio_io_config_t cfg;
        if (gpio_get_io_config((gpio_num_t)gpio, &cfg) != ESP_OK) {
            continue;
        }
        ESP_LOGI(TAG, "  GPIO %2d: %s", gpio,
                 cfg.slp_sel ? "SleepSel=1 -> switches to its sleep configuration (released unless configured)"
                             : "SleepSel=0 -> keeps its active configuration and level");
    }
}

esp_err_t power_management_init(void)
{
#if CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE
    const esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        // Always enabled, as in the esp-zigbee-sdk example; DEBUG builds block
        // light sleep with a PM lock below instead of disabling it here.
        .light_sleep_enable = true,
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (APP_LIGHT_SLEEP) {
        ESP_LOGI(TAG, "Automatic light sleep enabled (CPU %d MHz)", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
        return ESP_OK;
    }

    // DEBUG level 1: keep the chip awake with a PM lock instead of disabling
    // light sleep in the PM configuration.
    static esp_pm_lock_handle_t s_awake_lock;
    ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "debug_awake", &s_awake_lock);
    if (ret == ESP_OK) {
        ret = esp_pm_lock_acquire(s_awake_lock);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DEBUG awake lock failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGW(TAG, "DEBUG level 1: light sleep blocked by PM lock, USB always available");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE / CONFIG_FREERTOS_USE_TICKLESS_IDLE not set: no light sleep");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/*
 * DEBUG level 2: print the PM locks (name, type, count, held time) and the
 * time per PM mode incl. light sleep / rejected light sleep counters
 * (esp_pm_dump_locks(), PM_PROFILING). vTaskDelay() does not block sleep.
 */
static void pm_dump_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POWER_MGMT_PM_DUMP_INTERVAL_MS));
        ESP_LOGI(TAG, "PM locks and sleep statistics:");
        esp_pm_dump_locks(stdout);
    }
}

void power_management_report(void)
{
    if (!APP_DEBUG) {
        return;
    }
    if (APP_DEBUG_PM_DUMP &&
        xTaskCreate(pm_dump_task, "pm_dump", POWER_MGMT_PM_DUMP_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "PM dump task not started");
    }
    if (!APP_LIGHT_SLEEP) {
        // No sleep at all: nothing simulated, GPIO report still useful for sleep tests
    } else if (!SLEEP_USB_KEEPS_AWAKE) {
        ESP_LOGW(TAG, "DEBUG level %d: real light sleep, USB console will drop; "
                      "monitor via UART0 (TX GPIO %d)", APP_DEBUG_LEVEL, U0TXD_GPIO_NUM);
    } else if (power_management_sleep_is_simulated()) {
        ESP_LOGW(TAG, "DEBUG: USB connected -> light sleep is SIMULATED (CPU stays awake). "
                      "Disconnect USB for real sleep.");
    }
    sleep_report_gpios();
}
