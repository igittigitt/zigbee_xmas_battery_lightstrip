/*
 * Consumption model for the remaining time (goal Z14).
 *
 * Charge is counted in uA*us: every change of the light output level books
 * the elapsed time with the previous current (BATTERY_I_IDLE_UA plus
 * BATTERY_I_LIGHT_FULL_UA scaled linearly with the level). A single esp_timer
 * (BATTERY_MODEL_INTERVAL_MS) books the elapsed time, updates the daily
 * average, saves to NVS and reports. No ADC access here: the voltage
 * measurement (goal Z7) lives in battery.c / power_config_cluster.c.
 *
 * The usable charge follows the cell configuration (type, capacity per cell,
 * parallel cells), which is set from Home Assistant (cluster 0xFC00) and
 * stored in NVS. Without a stored counter, and after every change of the cell
 * configuration, the counter starts at the state of charge derived from the
 * voltage instead of "full".
 */
#include <assert.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "app_config.h"
#include "battery.h"
#include "battery_config.h"
#include "battery_model.h"
#include "light.h"
#include "light_config.h"

static const char *TAG = "BATTERY_MODEL";

#define US_PER_HOUR             (3600ULL * 1000 * 1000)
#define US_PER_TENTH_HOUR       (US_PER_HOUR / 10)


/** Model updates per day. */
#define TICKS_PER_DAY           ((24UL * 3600 * 1000) / BATTERY_MODEL_INTERVAL_MS)
static_assert(TICKS_PER_DAY >= 1, "BATTERY_MODEL_INTERVAL_MS must not exceed one day");

#define NVS_KEY_CONSUMED        "consumed"
#define NVS_KEY_DAY_AVG         "day_avg"
#define NVS_KEY_CELL_TYPE       "cell_type"
#define NVS_KEY_CELL_CAP        "cell_cap"
#define NVS_KEY_CELLS_PAR       "cells_par"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_consumed;         // charge used since the last full battery, uA*us
static int64_t s_last_us;           // time of the last booking, 0 = not started
static uint32_t s_load_ua;          // current since s_last_us
static uint64_t s_day_avg;          // moving average of the daily consumption, uA*us, 0 = unknown
static uint64_t s_day_start;        // s_consumed at the start of the current day (not persistent)
static uint64_t s_usable;           // usable charge of the configured cells, uA*us
static bool s_counter_known;        // false: no stored counter, set from the first voltage at rest

static battery_cells_t s_cells = {
    .type = BATTERY_CELL_DEFAULT_TYPE,
    .capacity_mah = BATTERY_CAPACITY_35E_MAH,
    .parallel = BATTERY_CELLS_PARALLEL_DEFAULT,
};

static uint32_t s_ticks;            // updates since boot (esp_timer task only)
static esp_timer_handle_t s_timer;
static battery_model_cb_t s_cb;

static uint64_t usable_uaus(const battery_cells_t *cells)
{
    // Permille first, then hours: 20000 mAh x 8 x 1000 x 3.6e9 us x 880 would overflow uint64
    // (max 1.8e19); in this order the largest value is 1.4e8 uA h x 3.6e9 = 5.1e17.
    // Same result as before for capacities that are whole mAh (NVS counters stay valid).
    return (uint64_t)cells->capacity_mah * cells->parallel * BATTERY_USABLE_PERMILLE * US_PER_HOUR;
}

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
    const uint64_t usable = s_usable;
    portEXIT_CRITICAL(&s_lock);

    const uint64_t remaining = consumed < usable ? usable - consumed : 0;

    // Light-on time at the level used for "on" (dimming), idle current included
    runtime->on_time_x10h = clamp_u16(remaining / load_ua(light_get_on_level()) / US_PER_TENTH_HOUR);
    // Divide the divisor instead of multiplying the counter: remaining * 100 overflows uint64
    // from about 1.8e17 uA us (already one 3350 mAh cell is 1.06e19)
    runtime->days_x10 = day_avg >= 10 ? clamp_u16(remaining / (day_avg / 10)) : BATTERY_UNKNOWN_U16;
    runtime->percent = usable >= 100 ? (uint8_t)(remaining / (usable / 100)) : 0;
    if (runtime->percent > 100) {
        runtime->percent = 100;
    }
}

static void nvs_save_cells(const battery_cells_t *cells)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(BATTERY_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, NVS_KEY_CELL_TYPE, cells->type);
        if (err == ESP_OK) {
            err = nvs_set_u16(nvs, NVS_KEY_CELL_CAP, cells->capacity_mah);
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(nvs, NVS_KEY_CELLS_PAR, cells->parallel);
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Saving cell configuration failed: %s", esp_err_to_name(err));
    }
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
    // Cell configuration: keep the defaults for missing or invalid entries
    battery_cells_t cells = s_cells;
    if (nvs_get_u8(nvs, NVS_KEY_CELL_TYPE, &cells.type) != ESP_OK) {
        cells.type = s_cells.type;
    }
    if (nvs_get_u16(nvs, NVS_KEY_CELL_CAP, &cells.capacity_mah) != ESP_OK) {
        cells.capacity_mah = s_cells.capacity_mah;
    }
    if (nvs_get_u8(nvs, NVS_KEY_CELLS_PAR, &cells.parallel) != ESP_OK) {
        cells.parallel = s_cells.parallel;
    }
    nvs_close(nvs);
    if (battery_model_cells_valid(&cells)) {
        s_cells = cells;
    } else {
        ESP_LOGW(TAG, "Stored cell configuration invalid, using the default");
    }

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

/** Set the counter so the model shows the given state of charge, and save it. */
static void set_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    portENTER_CRITICAL(&s_lock);
    book_locked(esp_timer_get_time());
    s_consumed = s_usable / 100 * (100 - percent);     // s_usable * (100 - percent) would overflow
    s_day_start = s_consumed;
    s_counter_known = true;
    portEXIT_CRITICAL(&s_lock);
    nvs_save();
}

void battery_model_boot_voltage(uint32_t mv)
{
    if (mv >= BATTERY_FULL_MV) {
        set_percent(100);
        ESP_LOGI(TAG, "Battery full (%lu mV), consumption counter reset", (unsigned long)mv);
    } else if (!s_counter_known) {
        const uint8_t percent = battery_percent_from_mv(s_cells.type, mv, false);
        set_percent(percent);
        ESP_LOGI(TAG, "No stored counter: starting at %u %% from %lu mV", percent, (unsigned long)mv);
    }
}

bool battery_model_cells_valid(const battery_cells_t *cells)
{
    return cells->type < BATTERY_CELL_TYPE_COUNT &&
           cells->capacity_mah >= BATTERY_CELL_CAPACITY_MIN_MAH &&
           cells->capacity_mah <= BATTERY_CELL_CAPACITY_MAX_MAH &&
           cells->parallel >= BATTERY_CELLS_PARALLEL_MIN &&
           cells->parallel <= BATTERY_CELLS_PARALLEL_MAX;
}

void battery_model_get_cells(battery_cells_t *cells)
{
    portENTER_CRITICAL(&s_lock);
    *cells = s_cells;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t battery_model_set_cells(const battery_cells_t *cells, int soc_percent)
{
    ESP_RETURN_ON_FALSE(battery_model_cells_valid(cells), ESP_ERR_INVALID_ARG, TAG, "Invalid cell configuration");

    portENTER_CRITICAL(&s_lock);
    book_locked(esp_timer_get_time());
    const uint64_t old_usable = s_usable;
    const uint64_t new_usable = usable_uaus(cells);
    s_cells = *cells;
    s_usable = new_usable;
    if (soc_percent < 0 && old_usable) {
        // State of charge unknown: keep the remaining share of the old configuration
        const uint64_t consumed = s_consumed < old_usable ? s_consumed : old_usable;
        s_consumed = (uint64_t)((double)consumed * (double)new_usable / (double)old_usable);
        s_day_start = s_consumed;
    }
    portEXIT_CRITICAL(&s_lock);

    nvs_save_cells(cells);
    if (soc_percent >= 0) {
        set_percent((uint8_t)soc_percent);
    } else {
        nvs_save();
    }
    ESP_LOGI(TAG, "Cells: type %u, %u mAh x %u%s", cells->type, cells->capacity_mah, cells->parallel,
             soc_percent >= 0 ? ", counter set from the voltage" : ", remaining share kept");
    return ESP_OK;
}

esp_err_t battery_model_init(battery_model_cb_t cb)
{
    s_cb = cb;
    s_counter_known = nvs_load();
    s_usable = usable_uaus(&s_cells);
    if (!s_counter_known) {
        if (!APP_BATTERY_ADC) {
            s_consumed = s_usable / 100 * (100 - APP_DEBUG_BATTERY_PERCENT);
            s_counter_known = true;
            ESP_LOGW(TAG, "DEBUG: model starts at the emulated level %d %%", APP_DEBUG_BATTERY_PERCENT);
        } else {
            ESP_LOGW(TAG, "No stored counter: set from the first voltage at rest");
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
