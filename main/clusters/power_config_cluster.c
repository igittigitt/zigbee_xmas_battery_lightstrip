#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app_config.h"
#include "zigbee_config.h"
#include "power_config_cluster.h"
#include "battery_runtime_cluster.h"
#include "battery.h"
#include "battery_model.h"
#include "battery_guard.h"
#include "battery_config.h"
#include "light.h"
#include "light_config.h"

static const char *TAG = "POWER_CFG_CLUSTER";

/** ZCL "invalid / unknown" value for BatteryVoltage and BatteryPercentageRemaining. */
#define POWER_CONFIG_VALUE_UNKNOWN  0xFF

/* Measured state, written only in measure() under s_measure_mutex: the
 * esp_timer task (periodic) and the Zigbee task (startup, read requests)
 * both measure. */
static uint8_t s_percent;           // last state of charge in percent (smoothed), valid if s_percent_valid
static bool s_percent_valid;
static uint16_t s_voltage_mv = BATTERY_UNKNOWN_U16;    // last voltage in mV, unknown = none yet
static bool s_last_at_rest;         // last measurement: light off
static bool s_last_full_load;       // last measurement: light on at full brightness

static SemaphoreHandle_t s_measure_mutex;
static StaticSemaphore_t s_measure_mutex_buf;
static bool s_adc_ready;            // battery_init() succeeded (set once at startup)
static esp_timer_handle_t s_measure_timer;

/** Snapshot of one measurement, taken under s_measure_mutex. */
typedef struct {
    uint32_t mv;
    uint16_t voltage_mv;
    uint8_t percent;
    bool percent_valid;
    bool at_rest;       // light off: open circuit voltage
    bool full_load;     // light on at LIGHT_LEVEL_MAX: voltage under load
} measurement_t;

/* mV -> ZCL BatteryVoltage (unit 100 mV, rounded down so the value never
 * exceeds the measurement; 0xFF is reserved for "unknown"). */
static uint8_t zcl_voltage(uint32_t mv)
{
    uint32_t value = mv / 100;
    return value >= POWER_CONFIG_VALUE_UNKNOWN ? POWER_CONFIG_VALUE_UNKNOWN - 1 : (uint8_t)value;
}

/* Percent -> ZCL BatteryPercentageRemaining (unit 0.5 %, 200 = 100 %). */
static uint8_t zcl_percent(uint8_t percent)
{
    return (uint8_t)(percent * 2);
}

/*
 * Smoothing (battery_config.h, BATTERY_PERCENT_RISE_MIN): only falling values
 * are taken over, a rise only if it is large enough (charged / replaced).
 */
static void percent_update(uint8_t measured)
{
    if (!s_percent_valid || measured < s_percent || measured >= s_percent + BATTERY_PERCENT_RISE_MIN) {
        s_percent = measured;
        s_percent_valid = true;
    }
}

/*
 * Measure and update the smoothed state of charge. The voltage drops under
 * load, so each state has its own table: light off -> BATTERY_OCV_TABLE, light
 * on at full brightness -> BATTERY_LOAD_TABLE. Dimmed or during a ramp there
 * is no table, the state of charge is kept.
 * Blocks for the ADC samples (a few ms) and, if another task is measuring,
 * until that measurement is done. No Zigbee API inside: the mutex is never
 * held together with the Zigbee lock (no lock-order deadlock).
 */
static esp_err_t measure(measurement_t *m)
{
    xSemaphoreTake(s_measure_mutex, portMAX_DELAY);
    esp_err_t err = battery_measure_mv(&m->mv);
    if (err == ESP_OK) {
        s_voltage_mv = m->mv >= BATTERY_UNKNOWN_U16 ? BATTERY_UNKNOWN_U16 - 1 : (uint16_t)m->mv;
        const uint8_t level = light_get_level();
        m->at_rest = (level == 0);
        m->full_load = (level == LIGHT_LEVEL_MAX);
        s_last_at_rest = m->at_rest;
        s_last_full_load = m->full_load;
        if (m->at_rest || m->full_load) {
            battery_cells_t cells;
            battery_model_get_cells(&cells);
            percent_update(battery_percent_from_mv(cells.type, m->mv, m->full_load));
        }
        m->voltage_mv = s_voltage_mv;
        m->percent = s_percent;
        m->percent_valid = s_percent_valid;
    }
    xSemaphoreGive(s_measure_mutex);
    ESP_RETURN_ON_ERROR(err, TAG, "Battery measurement");

    ESP_LOGI(TAG, "Battery %" PRIu32 " mV (%s), state of charge %d %%", m->mv,
             m->at_rest ? "at rest" : (m->full_load ? "light on, load table" : "dimmed, SoC kept"),
             m->percent_valid ? m->percent : -1);
    return ESP_OK;
}

/* Zigbee task context or Zigbee lock held. */
static void set_attr(uint16_t attr_id, uint8_t value)
{
    ezb_zcl_status_t status = ezb_zcl_set_attr_value(ZB_ENDPOINT, EZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                                                     EZB_ZCL_CLUSTER_SERVER, attr_id, EZB_ZCL_STD_MANUF_CODE,
                                                     &value, false);
    if (status != EZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Setting attribute 0x%04x failed (ZCL status 0x%02x)", attr_id, status);
    }
}

/* Write a measurement to the attributes. Zigbee task context or Zigbee lock held. */
static void apply(const measurement_t *m)
{
    set_attr(EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, zcl_voltage(m->mv));
    battery_runtime_cluster_set_voltage(m->voltage_mv);     // exact mV in 0xFC00, errors are logged
    if (m->percent_valid) {
        set_attr(EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, zcl_percent(m->percent));
    }
}

esp_err_t power_config_cluster_update(void)
{
    measurement_t m;
    ESP_RETURN_ON_ERROR(measure(&m), TAG, "Measurement");

    esp_zigbee_lock_acquire(portMAX_DELAY);
    apply(&m);
    esp_zigbee_lock_release();

    battery_guard_check(m.mv);  // goal Z8: warning flashes or emergency shutdown
    return ESP_OK;
}

esp_err_t power_config_cluster_refresh(void)
{
    if (!s_adc_ready) {
        ESP_LOGI(TAG, "No battery measurement (DEBUG without ADC or init failed): values unchanged");
        return ESP_ERR_INVALID_STATE;
    }
    measurement_t m;
    ESP_RETURN_ON_ERROR(measure(&m), TAG, "Measurement");
    apply(&m);
    battery_guard_check(m.mv);  // does not block: starts a pattern or a task
    return ESP_OK;
}

/* esp_timer task context. */
static void measure_timer_cb(void *arg)
{
    power_config_cluster_update();  // errors are logged, values stay unchanged
}

/* Allow reporting of an attribute (Zigbee2MQTT configures reporting at the interview). */
static esp_err_t make_reportable(ezb_zcl_cluster_desc_t cluster, uint16_t attr_id)
{
    ezb_zcl_attr_desc_t attr = ezb_zcl_cluster_get_attr_desc(cluster, attr_id, EZB_ZCL_STD_MANUF_CODE);
    ESP_RETURN_ON_FALSE(attr != EZB_INVALID_ZCL_ATTR_DESC, ESP_ERR_NOT_FOUND, TAG, "Attribute 0x%04x missing", attr_id);
    ezb_zcl_attr_access_t access = ezb_zcl_attr_desc_get_access(attr) | EZB_ZCL_ATTR_ACCESS_REPORTING;
    return esp_zigbee_err_to_esp(ezb_zcl_attr_desc_set_access(attr, access));
}

/*
 * Initial attribute values (Zigbee task, before registration): a measurement
 * at startup (light still off), or the DEBUG emulation, or "unknown".
 */
static void initial_values(uint8_t *voltage, uint8_t *percentage)
{
    *voltage = POWER_CONFIG_VALUE_UNKNOWN;
    *percentage = POWER_CONFIG_VALUE_UNKNOWN;

    if (!APP_BATTERY_ADC) {
        // DEBUG without ADC: emulated level, voltage unknown
        *percentage = zcl_percent(APP_DEBUG_BATTERY_PERCENT);
        ESP_LOGW(TAG, "DEBUG: emulated battery level %d %%", APP_DEBUG_BATTERY_PERCENT);
        return;
    }
    if (battery_init() != ESP_OK) {
        return;     // logged; attributes stay unknown
    }
    s_adc_ready = true;
    if (APP_DEBUG_MARKERS) {
        // DEBUG level 3 silences the log (debug_marker_init()); let the boot
        // measurement through once (UART0 TX GPIO 24), then silent again.
        // A tag level overrides the default level "*" (esp_log_level_set()).
        esp_log_level_set(TAG, ESP_LOG_INFO);
    }
    measurement_t m;
    const esp_err_t measure_err = measure(&m);
    if (APP_DEBUG_MARKERS) {
        esp_log_level_set(TAG, ESP_LOG_NONE);
    }
    if (measure_err == ESP_OK) {
        *voltage = zcl_voltage(m.mv);
        if (m.at_rest) {
            battery_model_boot_voltage(m.mv);   // full cell: reset the remaining-time model (Z14)
        }
        if (m.percent_valid) {
            *percentage = zcl_percent(m.percent);
        }
    }

    const esp_timer_create_args_t timer_args = {
        .callback = measure_timer_cb,
        .name = "battery",
        // After a long sleep only one measurement is due, not all missed ones
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_measure_timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(s_measure_timer, (uint64_t)BATTERY_MEASURE_INTERVAL_MS * 1000);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Battery measurement timer failed: %s", esp_err_to_name(err));
    }
}

uint16_t power_config_cluster_get_voltage_mv(void)
{
    return s_voltage_mv;
}

int power_config_cluster_cells_changed(void)
{
    if (!s_adc_ready) {
        return -1;
    }
    int soc = -1;
    xSemaphoreTake(s_measure_mutex, portMAX_DELAY);
    if (s_voltage_mv != BATTERY_UNKNOWN_U16 && (s_last_at_rest || s_last_full_load)) {
        battery_cells_t cells;
        battery_model_get_cells(&cells);
        // New table: take the value directly, the smoothing would block a rise
        s_percent = battery_percent_from_mv(cells.type, s_voltage_mv, s_last_full_load);
        s_percent_valid = true;
        soc = s_percent;
    }
    xSemaphoreGive(s_measure_mutex);

    if (soc >= 0) {
        set_attr(EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, zcl_percent((uint8_t)soc));
        ESP_LOGI(TAG, "Cell configuration changed: state of charge %d %% from %u mV", soc, s_voltage_mv);
    }
    return soc;
}

esp_err_t power_config_cluster_add(ezb_af_ep_desc_t ep_desc)
{
    // Mains attributes are mandatory in the stack's server config, values per
    // power_config_desc.h defaults (device is battery powered, no mains).
    const ezb_zcl_power_config_cluster_server_config_t cfg = {
        .mains_voltage = EZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_DEFAULT_VALUE,
        .mains_voltage_min_threshold = EZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MIN_THRESHOLD_DEFAULT_VALUE,
        .mains_voltage_max_threshold = EZB_ZCL_POWER_CONFIG_MAINS_VOLTAGE_MAX_THRESHOLD_DEFAULT_VALUE,
    };
    ezb_zcl_cluster_desc_t cluster = ezb_zcl_power_config_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_FALSE(cluster, ESP_ERR_NO_MEM, TAG, "Cluster allocation");

    s_measure_mutex = xSemaphoreCreateMutexStatic(&s_measure_mutex_buf);

    uint8_t voltage;
    uint8_t percentage;
    initial_values(&voltage, &percentage);

    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_power_config_cluster_desc_add_attr(
                            cluster, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &voltage)),
                        TAG, "BatteryVoltage");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_power_config_cluster_desc_add_attr(
                            cluster, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &percentage)),
                        TAG, "BatteryPercentageRemaining");
    ESP_RETURN_ON_ERROR(make_reportable(cluster, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID), TAG,
                        "BatteryVoltage reporting");
    ESP_RETURN_ON_ERROR(make_reportable(cluster, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID), TAG,
                        "BatteryPercentageRemaining reporting");

    return esp_zigbee_err_to_esp(ezb_af_endpoint_add_cluster_desc(ep_desc, cluster));
}
