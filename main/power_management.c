#include "esp_log.h"
#include "esp_pm.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "app_config.h"
#include "zigbee_config.h"
#include "power_management.h"
#include "light.h"

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
    uint64_t mask = light_get_gpio_mask() | SLEEP_REPORT_EXTRA_GPIO_MASK;

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
        // Always enabled, as in the esp-zigbee-sdk example: with light_sleep_enable
        // = false, joining failed with NWK 0xC3 (docs/known-issues.md, 1c).
        .light_sleep_enable = true,
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (APP_LIGHT_SLEEP || ZB_TEST_NO_AWAKE_LOCK) {
        ESP_LOGI(TAG, "Automatic light sleep enabled (CPU %d MHz)", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
        return ESP_OK;
    }

    // DEBUG without ZL_DEBUG_LIGHT_SLEEP: keep the chip awake with a PM lock
    // instead of disabling light sleep in the PM configuration.
    static esp_pm_lock_handle_t s_awake_lock;
    ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "debug_awake", &s_awake_lock);
    if (ret == ESP_OK) {
        ret = esp_pm_lock_acquire(s_awake_lock);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DEBUG awake lock failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGW(TAG, "DEBUG: light sleep blocked by PM lock (Kconfig ZL_DEBUG_LIGHT_SLEEP=n), "
                  "USB always available");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE / CONFIG_FREERTOS_USE_TICKLESS_IDLE not set: no light sleep");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void power_management_report(void)
{
    if (!APP_DEBUG) {
        return;
    }
    if (!APP_LIGHT_SLEEP) {
        // No sleep at all: nothing simulated, GPIO report still useful for sleep tests
    } else if (!SLEEP_USB_KEEPS_AWAKE) {
        ESP_LOGW(TAG, "DEBUG: CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION not set, "
                      "USB console will drop during light sleep");
    } else if (power_management_sleep_is_simulated()) {
        ESP_LOGW(TAG, "DEBUG: USB connected -> light sleep is SIMULATED (CPU stays awake). "
                      "Disconnect USB for real sleep.");
    }
    sleep_report_gpios();
}
