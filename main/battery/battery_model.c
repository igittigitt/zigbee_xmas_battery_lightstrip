/*
 * Consumption model for the remaining time (goal Z14).
 *
 * Charge is counted in uA*us: every change of the light output level books
 * the elapsed time with the previous current (BATTERY_I_IDLE_UA plus
 * BATTERY_I_LIGHT_FULL_UA scaled linearly with the level). A single esp_timer
 * (BATTERY_MODEL_INTERVAL_MS) books the elapsed time, updates the daily
 * average, saves to NVS and reports. No ADC access here: the voltage
 * measurement (goal Z7) lives in battery.c / power_config_cluster.c.
 */
#include <assert.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "app_config.h"
#include "battery_config.h"
#include "battery_model.h"
#include "light.h"
#include "light_config.h"

static const char *TAG = "BATTERY_MODEL";

#define US_PER_HOUR             (3600ULL * 1000 * 1000)
#define US_PER_TENTH_HOUR       (US_PER_HOUR / 10)

/** Usable charge in uA*us. */
#define USABLE_UAUS             ((uint64_t)BATTERY_CAPACITY_MAH * 1000 * US_PER_HOUR * \
                                 BATTERY_USABLE_PERMILLE / 1000)

/** Model updates per day. */
#define TICKS_PER_DAY           ((24UL * 3600 * 1000) / BATTERY_MODEL_INTERVAL_MS)
static_assert(TICKS_PER_DAY >= 1, "BATTERY_MODEL_INTERVAL_MS must not exceed one day");

#define NVS_KEY_CONSUMED        "consumed"
#define NVS_KEY_DAY_AVG         "day_avg"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_consumed;         // charge used since the last full battery, uA*us
static int64_t s_last_us;           // time of the last booking, 0 = not started
static uint32_t s_load_ua;          // current since s_last_us
static uint64_t s_day_avg;          // moving average of the daily consumption, uA*us, 0 = unknown
static uint64_t s_day_start;        // s_consumed at the start of the current day (not persistent)

static uint32_t s_ticks;            // updates since boot (esp_timer task only)
static esp_timer_handle_t s_timer;
static battery_model_cb_t s_cb;

static uint32_t load_ua(uint8_t level)
{
    return BATTERY_I_IDLE_UA + (uint32_t)((uint64_t)BATTERY_I_LIGHT_FULL_UA * level / LIGHT_LEVEL_MAX);
}

/** Book the time since s_last_us with s_load_ua. Caller holds s_lock. */
static void book_locked(int64_t now_us)
{
    if (s_last_us != 0 && now_us > s_last_us) {
        s_consumed += (uint64_t)s_load_ua * (uint64_t)(now_us - s_last_us);
    }
    s_last_us = now_us;
}

void battery_model_set_load(uint8_t level)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    book_locked(now);
    s_load_ua = load_ua(level);
    portEXIT_CRITICAL(&s_lock);
}

static uint16_t clamp_u16(uint64_t value)
{
    return value >= BATTERY_UNKNOWN_U16 ? BATTERY_UNKNOWN_U16 - 1 : (uint16_t)value;
}

void battery_model_get(battery_runtime_t *runtime)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    book_locked(now);
    const uint64_t consumed = s_consumed;
    const uint64_t day_avg = s_day_avg;
    portEXIT_CRITICAL(&s_lock);

    const uint64_t remaining = consumed < USABLE_UAUS ? USABLE_UAUS - consumed : 0;

    // Light-on time at the level used for "on" (dimming), idle current included
    runtime->on_time_x10h = clamp_u16(remaining / load_ua(light_get_on_level()) / US_PER_TENTH_HOUR);
    runtime->days_x10 = day_avg ? clamp_u16(remaining * 10 / day_avg) : BATTERY_UNKNOWN_U16;
    runtime->percent = (uint8_t)(remaining * 100 / USABLE_UAUS);
}

static void nvs_save(void)
{
    portENTER_CRITICAL(&s_lock);
    const uint64_t consumed = s_consumed;
    const uint64_t day_avg = s_day_avg;
    portEXIT_CRITICAL(&s_lock);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(BATTERY_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u64(nvs, NVS_KEY_CONSUMED, consumed);
        if (err == ESP_OK) {
            err = nvs_set_u64(nvs, NVS_KEY_DAY_AVG, day_avg);
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Saving counters failed: %s", esp_err_to_name(err));
    }
}

/** Load the counters; returns false if none are stored. */
static bool nvs_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(BATTERY_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;   // namespace does not exist yet
    }
    uint64_t consumed = 0;
    uint64_t day_avg = 0;
    const bool found = (nvs_get_u64(nvs, NVS_KEY_CONSUMED, &consumed) == ESP_OK);
    if (nvs_get_u64(nvs, NVS_KEY_DAY_AVG, &day_avg) != ESP_OK) {
        day_avg = 0;    // not stored yet: unknown
    }
    nvs_close(nvs);

    s_consumed = consumed;
    s_day_avg = day_avg;
    return found;
}

static void log_runtime(const char *what, const battery_runtime_t *rt)
{
    ESP_LOGI(TAG, "%s: model %u %%, light-on %u.%u h, days %u.%u (6553.5 = unknown)", what, rt->percent,
             rt->on_time_x10h / 10, rt->on_time_x10h % 10, rt->days_x10 / 10, rt->days_x10 % 10);
}

/** esp_timer task context. */
static void model_timer_cb(void *arg)
{
    s_ticks++;
    const bool new_day = (s_ticks % TICKS_PER_DAY == 0);

    if (new_day) {
        portENTER_CRITICAL(&s_lock);
        book_locked(esp_timer_get_time());
        const uint64_t day = s_consumed > s_day_start ? s_consumed - s_day_start : 0;
        s_day_avg = s_day_avg ? (s_day_avg * (BATTERY_DAYS_AVG_N - 1) + day) / BATTERY_DAYS_AVG_N : day;
        s_day_start = s_consumed;
        portEXIT_CRITICAL(&s_lock);
    }
    if (new_day || s_ticks % BATTERY_NVS_SAVE_EVERY == 0) {
        nvs_save();
    }

    battery_runtime_t rt;
    battery_model_get(&rt);
    log_runtime("Update", &rt);
    if (s_cb) {
        s_cb(&rt);
    }
}

void battery_model_boot_voltage(uint32_t mv)
{
    if (mv < BATTERY_FULL_MV) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_consumed = 0;
    s_day_start = 0;
    portEXIT_CRITICAL(&s_lock);
    nvs_save();
    ESP_LOGI(TAG, "Battery full (%lu mV), consumption counter reset", (unsigned long)mv);
}

esp_err_t battery_model_init(battery_model_cb_t cb)
{
    s_cb = cb;
    if (!nvs_load()) {
        if (!APP_BATTERY_ADC) {
            s_consumed = USABLE_UAUS * (100 - APP_DEBUG_BATTERY_PERCENT) / 100;
            ESP_LOGW(TAG, "DEBUG: model starts at the emulated level %d %%", APP_DEBUG_BATTERY_PERCENT);
        } else {
            ESP_LOGW(TAG, "No stored counter: assuming a full battery");
        }
    }

    portENTER_CRITICAL(&s_lock);
    s_day_start = s_consumed;
    s_load_ua = load_ua(0);
    s_last_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_lock);

    const esp_timer_create_args_t timer_args = {
        .callback = model_timer_cb,
        .name = "battery_model",
        // After a long sleep only one update is due, not all missed ones
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_timer), TAG, "Timer create");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_timer, (uint64_t)BATTERY_MODEL_INTERVAL_MS * 1000),
                        TAG, "Timer start");

    battery_runtime_t rt;
    battery_model_get(&rt);
    log_runtime("Start (placeholder currents, see battery_config.h)", &rt);
    return ESP_OK;
}
