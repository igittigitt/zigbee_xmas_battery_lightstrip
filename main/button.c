/*
 * Local button (goal Z5), see button.h.
 *
 * Idle: the task blocks on a notification, the GPIO interrupt (low level) is
 * enabled and the pin is a light sleep wakeup source. ESP32-H2: "Any IO can be
 * used as the external input to wake up the chip from Light-sleep"
 * (IDF v5.5.4 docs, api-reference/system/sleep_modes.rst, "GPIO Wakeup").
 * gpio_wakeup_enable() also keeps the pad configuration during sleep
 * (gpio_sleep_sel_dis with CONFIG_PM_SLP_DISABLE_GPIO, esp_driver_gpio/src/gpio.c).
 *
 * Pressed: the ISR disables the interrupt (level triggered, would fire
 * continuously) and wakes the task, which polls until release. While the pin
 * is low the wakeup source keeps the chip awake, so polling works.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "button.h"
#include "light.h"
#include "status_indicator.h"
#include "on_off_cluster.h"
#include "zigbee_device.h"

static const char *TAG = "BUTTON";

static TaskHandle_t s_task;

static void button_isr(void *arg)
{
    BaseType_t woken = pdFALSE;

    gpio_intr_disable(BUTTON_GPIO);
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

static bool is_pressed(void)
{
    return gpio_get_level(BUTTON_GPIO) == 0;
}

/* Wait until released; returns the press duration. Flashes once at the long press threshold. */
static uint32_t wait_release(void)
{
    const int64_t start_us = esp_timer_get_time();
    bool long_signalled = false;
    uint32_t held_ms = 0;

    while (is_pressed()) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
        held_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
        if (!long_signalled && held_ms >= BUTTON_LONG_PRESS_MS) {
            long_signalled = true;
            ESP_LOGW(TAG, "Long press: release the button for factory reset + pairing");
            const light_pattern_t flash = {
                .level = IND_LEVEL,
                .on_ms = BUTTON_RELEASE_FLASH_MS,
                .off_ms = 0,
                .count = 1,
            };
            light_pattern_start(&flash);
        }
    }
    return held_ms;
}

static void short_press(void)
{
    if (!zigbee_device_is_joined()) {
        // Not connected: the indication (search/pairing) keeps control of the light
        ESP_LOGI(TAG, "Short press ignored, not connected (hold %d s to re-pair)",
                 BUTTON_LONG_PRESS_MS / 1000);
        return;
    }
    bool on = !on_off_cluster_get_state();
    ESP_LOGI(TAG, "Short press: light %s", on ? "ON" : "OFF");
    on_off_cluster_set_state(on);
}

static void button_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t held_ms = wait_release();
        if (held_ms >= BUTTON_LONG_PRESS_MS) {
            zigbee_device_factory_reset();      // does not return (restart)
        } else if (held_ms >= BUTTON_DEBOUNCE_MS) {
            short_press();
        }
        // Released (pin high): re-arm, the level interrupt cannot fire immediately
        gpio_intr_enable(BUTTON_GPIO);
    }
}

esp_err_t button_init(void)
{
    // Internal pull-up in addition to the board's (if any): no current while
    // released, only while pressed (3.3 V across the pull-ups)
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_LOW_LEVEL,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config");

    BaseType_t ok = xTaskCreate(button_task, "button", BUTTON_TASK_STACK_SIZE, NULL,
                                BUTTON_TASK_PRIORITY, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create");

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {    // INVALID_STATE: already installed
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL), TAG, "isr add");
    ESP_RETURN_ON_ERROR(gpio_wakeup_enable(BUTTON_GPIO, GPIO_INTR_LOW_LEVEL), TAG, "wakeup enable");
    ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), TAG, "sleep gpio wakeup");

    ESP_LOGI(TAG, "Button on GPIO %d (short = toggle, %d s = factory reset + pairing)",
             BUTTON_GPIO, BUTTON_LONG_PRESS_MS / 1000);
    return ESP_OK;
}
