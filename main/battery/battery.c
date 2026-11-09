#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "battery.h"
#include "battery_config.h"
#include "debug_marker.h"

static const char *TAG = "BATTERY";

typedef struct {
    uint32_t mv;
    uint8_t percent;
} battery_ocv_point_t;

static const battery_ocv_point_t s_ocv_table[] = BATTERY_OCV_TABLE;
#define OCV_TABLE_LEN   (sizeof(s_ocv_table) / sizeof(s_ocv_table[0]))

static adc_unit_t s_unit;
static adc_channel_t s_channel;
static adc_cali_handle_t s_cali;    // NULL = not initialised

esp_err_t battery_init(void)
{
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(BATTERY_ADC_GPIO, &s_unit, &s_channel), TAG,
                        "GPIO %d is no ADC pin", BATTERY_ADC_GPIO);

    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_unit,
        .chan = s_channel,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err != ESP_OK) {
        s_cali = NULL;
        ESP_LOGE(TAG, "ADC calibration unavailable (%s), battery measurement disabled", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Battery measurement on GPIO %d (ADC%d channel %d)", BATTERY_ADC_GPIO, s_unit + 1, s_channel);
    return ESP_OK;
}

/* Average of the calibrated samples at the ADC pin in mV (unit must exist). */
static esp_err_t read_pin_mv(adc_oneshot_unit_handle_t unit, uint32_t *pin_mv)
{
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(unit, s_channel, &chan_cfg), TAG, "Channel config");

    uint32_t sum = 0;
    int valid = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        int mv = 0;
        // ESP_ERR_TIMEOUT if the ADC is busy (shared with other users): skip the sample
        if (adc_oneshot_get_calibrated_result(unit, s_cali, s_channel, &mv) == ESP_OK && mv >= 0) {
            sum += (uint32_t)mv;
            valid++;
        }
    }
    ESP_RETURN_ON_FALSE(valid >= BATTERY_ADC_SAMPLES / 2, ESP_ERR_TIMEOUT, TAG,
                        "Only %d of %d ADC samples valid", valid, BATTERY_ADC_SAMPLES);
    *pin_mv = sum / (uint32_t)valid;
    return ESP_OK;
}

esp_err_t battery_measure_mv(uint32_t *mv)
{
    ESP_RETURN_ON_FALSE(mv, ESP_ERR_INVALID_ARG, TAG, "mv is NULL");
    ESP_RETURN_ON_FALSE(s_cali, ESP_ERR_INVALID_STATE, TAG, "Not initialised");

    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = s_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_unit_handle_t unit = NULL;
    debug_marker_set(DEBUG_MARKER_BATTERY, true);   // PPK2 marker D3 (DEBUG level 3)
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &unit);
    if (err != ESP_OK) {
        debug_marker_set(DEBUG_MARKER_BATTERY, false);
        ESP_LOGE(TAG, "ADC unit: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t pin_mv = 0;
    err = read_pin_mv(unit, &pin_mv);
    // Always delete the unit: releases the power domains for light sleep
    esp_err_t del_err = adc_oneshot_del_unit(unit);
    debug_marker_set(DEBUG_MARKER_BATTERY, false);
    ESP_RETURN_ON_ERROR(err, TAG, "ADC read");
    ESP_RETURN_ON_ERROR(del_err, TAG, "ADC unit delete");

    uint32_t divided = pin_mv * (BATTERY_DIVIDER_R_TOP_KOHM + BATTERY_DIVIDER_R_BOTTOM_KOHM) /
                       BATTERY_DIVIDER_R_BOTTOM_KOHM;
    *mv = divided * BATTERY_CAL_PERMILLE / 1000;
    ESP_LOGD(TAG, "ADC pin %" PRIu32 " mV -> battery %" PRIu32 " mV (uncalibrated %" PRIu32 " mV)",
             pin_mv, *mv, divided);
    return ESP_OK;
}

uint8_t battery_percent_from_mv(uint32_t mv)
{
    if (mv <= s_ocv_table[0].mv) {
        return s_ocv_table[0].percent;
    }
    for (size_t i = 1; i < OCV_TABLE_LEN; i++) {
        const battery_ocv_point_t *lo = &s_ocv_table[i - 1];
        const battery_ocv_point_t *hi = &s_ocv_table[i];
        if (mv <= hi->mv) {
            return (uint8_t)(lo->percent + (mv - lo->mv) * (uint32_t)(hi->percent - lo->percent) / (hi->mv - lo->mv));
        }
    }
    return s_ocv_table[OCV_TABLE_LEN - 1].percent;
}
