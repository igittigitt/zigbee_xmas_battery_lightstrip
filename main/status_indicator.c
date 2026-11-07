#include "esp_log.h"
#include "esp_timer.h"
#include "light.h"
#include "status_indicator.h"

static const char *TAG = "STATUS_IND";

typedef enum {
    IND_NONE,
    IND_SEARCHING,
    IND_PAIRING,
} ind_state_t;

static ind_state_t s_state = IND_NONE;
static int64_t s_search_start_us;

static void run(uint32_t on_ms, uint32_t off_ms, uint32_t count, bool on_after)
{
    const light_pattern_t pattern = {
        .level = IND_LEVEL,
        .on_ms = on_ms,
        .off_ms = off_ms,
        .count = count,
    };
    esp_err_t err = light_pattern_then(&pattern, on_after);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Light pattern failed: %s", esp_err_to_name(err));
    }
}

void status_indicator_searching(void)
{
    s_state = IND_SEARCHING;
    s_search_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Searching stored network");
    run(IND_SEARCH_ON_MS, IND_SEARCH_OFF_MS, 0, false);
}

void status_indicator_pairing(void)
{
    s_state = IND_PAIRING;
    ESP_LOGI(TAG, "Pairing mode");
    run(IND_PAIRING_ON_MS, IND_PAIRING_OFF_MS, 0, false);
}

void status_indicator_connected(bool on)
{
    switch (s_state) {
    case IND_SEARCHING:
        ESP_LOGI(TAG, "Rejoined: %d blinks, then %s", IND_REJOINED_COUNT, on ? "ON" : "OFF");
        run(IND_REJOINED_ON_MS, IND_REJOINED_OFF_MS, IND_REJOINED_COUNT, on);
        break;
    case IND_PAIRING:
        ESP_LOGI(TAG, "Paired: %d pulses, then %s", IND_PAIRED_COUNT, on ? "ON" : "OFF");
        run(IND_PAIRED_ON_MS, IND_PAIRED_OFF_MS, IND_PAIRED_COUNT, on);
        break;
    default:
        return;
    }
    s_state = IND_NONE;
}

void status_indicator_stop(bool on)
{
    if (s_state == IND_NONE) {
        return;
    }
    ESP_LOGI(TAG, "Indication stopped, light %s", on ? "ON" : "OFF");
    s_state = IND_NONE;
    light_set_on(on);
}

void status_indicator_check_timeout(bool on)
{
    if (s_state == IND_SEARCHING &&
        esp_timer_get_time() - s_search_start_us >= (int64_t)IND_SEARCH_TIMEOUT_MS * 1000) {
        ESP_LOGW(TAG, "No connection after %d s", IND_SEARCH_TIMEOUT_MS / 1000);
        status_indicator_stop(on);
    }
}
