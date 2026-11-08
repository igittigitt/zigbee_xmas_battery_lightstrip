#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "app_config.h"
#include "zigbee_config.h"
#include "power_config_cluster.h"
#include "battery.h"
#include "battery_model.h"
#include "battery_config.h"
#include "light.h"

static const char *TAG = "POWER_CFG_CLUSTER";

/** ZCL "invalid / unknown" value for BatteryVoltage and BatteryPercentageRemaining. */
#define POWER_CONFIG_VALUE_UNKNOWN  0xFF

/* Last reported state of charge in percent (smoothed), valid if s_percent_valid.
 * Written by the measuring task only (Zigbee task at startup, then esp_timer task). */
static uint8_t s_percent;
static bool s_percent_valid;

static esp_timer_handle_t s_measure_timer;

/* mV -> ZCL BatteryVoltage (unit 100 mV, rounded; 0xFF is reserved for "unknown"). */
static uint8_t zcl_voltage(uint32_t mv)
{
    uint32_t value = (mv + 50) / 100;
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
 * Measure and update the smoothed state of charge. With the light on the
 * voltage drops under load, so the state of charge is only derived from
 * measurements at rest (docs/hardware.md, "Software-Regeln").
 */
static esp_err_t measure(uint32_t *mv, bool *at_rest)
{
    ESP_RETURN_ON_ERROR(battery_measure_mv(mv), TAG, "Battery measurement");
    *at_rest = (light_get_level() == 0);
    if (*at_rest) {
        percent_update(battery_percent_from_mv(*mv));
    }
    ESP_LOGI(TAG, "Battery %" PRIu32 " mV (%s), state of charge %d %%", *mv,
             *at_rest ? "at rest" : "light on, SoC kept", s_percent_valid ? s_percent : -1);
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

esp_err_t power_config_cluster_update(void)
{
    uint32_t mv = 0;
    bool at_rest = false;
    ESP_RETURN_ON_ERROR(measure(&mv, &at_rest), TAG, "Measurement");

    esp_zigbee_lock_acquire(portMAX_DELAY);
    set_attr(EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, zcl_voltage(mv));
    if (s_percent_valid) {
        set_attr(EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, zcl_percent(s_percent));
    }
    esp_zigbee_lock_release();
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
    uint32_t mv = 0;
    bool at_rest = false;
    if (measure(&mv, &at_rest) == ESP_OK) {
        *voltage = zcl_voltage(mv);
        if (at_rest) {
            battery_model_boot_voltage(mv);     // full cell: reset the remaining-time model (Z14)
        }
        if (s_percent_valid) {
            *percentage = zcl_percent(s_percent);
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
