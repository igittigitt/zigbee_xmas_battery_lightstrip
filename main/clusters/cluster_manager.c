#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "zigbee_config.h"
#include "cluster_manager.h"
#include "power_config_cluster.h"
#include "battery_runtime_cluster.h"
#include "on_off_cluster.h"
#include "level_cluster.h"
#include "ota_cluster.h"

static const char *TAG = "CLUSTER_MANAGER";

/* ZCL character string: length byte followed by the characters (no terminator). */
#define ZCL_STRING_MAX_LEN  32

static uint8_t s_manufacturer_name[ZCL_STRING_MAX_LEN + 1];
static uint8_t s_model_identifier[ZCL_STRING_MAX_LEN + 1];
static uint8_t s_sw_build_id[ZB_SW_BUILD_ID_MAX_LEN + 1];
static uint8_t s_date_code[ZB_DATE_CODE_MAX_LEN + 1];

static void zcl_string_from_cstr(uint8_t *dst, const char *src)
{
    size_t len = strnlen(src, ZCL_STRING_MAX_LEN);
    dst[0] = (uint8_t)len;
    memcpy(&dst[1], src, len);
}

/* Compile date of the app description ("Mmm dd yyyy", format of __DATE__, C17 6.10.8.1)
 * as "YYYYMMDD". Returns false if the image has no date (CONFIG_APP_COMPILE_TIME_DATE off). */
static bool build_date_code(char *out, size_t out_size)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *date = esp_app_get_description()->date;
    if (strlen(date) != 11) {
        return false;
    }
    const char *pos = strstr(months, (char[]){date[0], date[1], date[2], '\0'});
    if (pos == NULL || (pos - months) % 3 != 0) {
        return false;
    }
    unsigned month = (unsigned)(pos - months) / 3 + 1;
    unsigned day = (unsigned)atoi(&date[4]);  // day of month is space-padded ("Nov  1")
    snprintf(out, out_size, "%.4s%02u%02u", &date[7], month, day);
    return true;
}

static esp_err_t basic_add_identity(ezb_af_ep_desc_t ep_desc)
{
    ezb_zcl_cluster_desc_t basic = ezb_af_endpoint_get_cluster_desc(ep_desc, EZB_ZCL_CLUSTER_ID_BASIC,
                                                                     EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_FALSE(basic, ESP_ERR_NOT_FOUND, TAG, "Basic cluster missing");

    zcl_string_from_cstr(s_manufacturer_name, ZB_MANUFACTURER_NAME);
    zcl_string_from_cstr(s_model_identifier, ZB_MODEL_IDENTIFIER);
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_basic_cluster_desc_add_attr(
                            basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, s_manufacturer_name)),
                        TAG, "ManufacturerName");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_basic_cluster_desc_add_attr(
                            basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, s_model_identifier)),
                        TAG, "ModelIdentifier");

    char sw_build_id[ZB_SW_BUILD_ID_MAX_LEN + 1];
    snprintf(sw_build_id, sizeof(sw_build_id), ZB_SW_BUILD_ID_FORMAT,
             (unsigned)(ZB_OTA_APP_RELEASE >> 4), (unsigned)(ZB_OTA_APP_RELEASE & 0x0F),
             (unsigned)ZB_OTA_APP_BUILD);
    s_sw_build_id[0] = (uint8_t)strnlen(sw_build_id, ZB_SW_BUILD_ID_MAX_LEN);
    memcpy(&s_sw_build_id[1], sw_build_id, s_sw_build_id[0]);
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_basic_cluster_desc_add_attr(
                            basic, EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID, s_sw_build_id)),
                        TAG, "SWBuildID");

    char date_code[ZB_DATE_CODE_MAX_LEN + 1];
    if (build_date_code(date_code, sizeof(date_code))) {
        s_date_code[0] = (uint8_t)strnlen(date_code, ZB_DATE_CODE_MAX_LEN);
        memcpy(&s_date_code[1], date_code, s_date_code[0]);
        ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_basic_cluster_desc_add_attr(
                                basic, EZB_ZCL_ATTR_BASIC_DATE_CODE_ID, s_date_code)),
                            TAG, "DateCode");
    } else {
        date_code[0] = '\0';  // no compile date in the image: attribute keeps its default
    }
    ESP_LOGI(TAG, "SWBuildID %s, DateCode %s, OTA file version 0x%08" PRIx32, sw_build_id, date_code,
             (uint32_t)ZB_OTA_FILE_VERSION);
    return ESP_OK;
}

esp_err_t cluster_manager_register_device(void)
{
    ezb_zha_dimmable_light_config_t light_cfg = EZB_ZHA_DIMMABLE_LIGHT_CONFIG();
    light_cfg.basic_cfg.power_source = EZB_ZCL_BASIC_POWER_SOURCE_BATTERY;
    light_cfg.level_cfg.current_level = ZB_LEVEL_DEFAULT;  // template default 0xFF is no valid level

    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ezb_af_ep_desc_t ep_desc = ezb_zha_create_dimmable_light(ZB_ENDPOINT, &light_cfg);
    ESP_RETURN_ON_FALSE(dev_desc && ep_desc, ESP_ERR_NO_MEM, TAG, "Descriptor allocation");

    ESP_RETURN_ON_ERROR(basic_add_identity(ep_desc), TAG, "Basic identity");
    ESP_RETURN_ON_ERROR(on_off_cluster_add(ep_desc), TAG, "On/Off extension");
    ESP_RETURN_ON_ERROR(level_cluster_add(ep_desc), TAG, "Level Control extension");
    ESP_RETURN_ON_ERROR(power_config_cluster_add(ep_desc), TAG, "Power Configuration");
    ESP_RETURN_ON_ERROR(battery_runtime_cluster_add(ep_desc), TAG, "Battery runtime");
    ESP_RETURN_ON_ERROR(ota_cluster_add(ep_desc), TAG, "OTA Upgrade client");

    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_af_device_add_endpoint_desc(dev_desc, ep_desc)),
                        TAG, "Add endpoint");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_af_device_desc_register(dev_desc)), TAG, "Register device");
    ESP_RETURN_ON_ERROR(ota_cluster_init(), TAG, "OTA client configuration");

    ESP_LOGI(TAG, "Endpoint %d registered: Dimmable Light, %s / %s", ZB_ENDPOINT,
             ZB_MANUFACTURER_NAME, ZB_MODEL_IDENTIFIER);
    return ESP_OK;
}

void cluster_manager_update_runtime(const battery_runtime_t *runtime)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    battery_runtime_cluster_update(runtime);    // errors are logged
    esp_zigbee_lock_release();
}
