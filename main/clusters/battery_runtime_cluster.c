#include "esp_check.h"
#include "esp_log.h"
#include "ezbee/zcl/cluster/custom.h"
#include "zigbee_config.h"
#include "battery_runtime_cluster.h"
#include "power_config_cluster.h"

static const char *TAG = "BATT_RUNTIME_CLUSTER";

#define ATTR_ACCESS     (EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING)

esp_err_t battery_runtime_cluster_add(ezb_af_ep_desc_t ep_desc)
{
    const ezb_zcl_custom_cluster_config_t cfg = {
        .cluster_id = BATTERY_RUNTIME_CLUSTER_ID,
    };
    ezb_zcl_cluster_desc_t cluster = ezb_zcl_custom_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_FALSE(cluster, ESP_ERR_NO_MEM, TAG, "Cluster allocation");

    battery_runtime_t rt;
    battery_model_get(&rt);

    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_custom_cluster_desc_add_attr(
                            cluster, BATTERY_RUNTIME_ATTR_REMAINING_ON_TIME_ID, EZB_ZCL_ATTR_TYPE_UINT16,
                            ATTR_ACCESS, &rt.on_time_x10h)),
                        TAG, "RemainingOnTime");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_custom_cluster_desc_add_attr(
                            cluster, BATTERY_RUNTIME_ATTR_REMAINING_DAYS_ID, EZB_ZCL_ATTR_TYPE_UINT16,
                            ATTR_ACCESS, &rt.days_x10)),
                        TAG, "RemainingDays");
    uint16_t voltage_mv = power_config_cluster_get_voltage_mv();
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_custom_cluster_desc_add_attr(
                            cluster, BATTERY_RUNTIME_ATTR_VOLTAGE_MV_ID, EZB_ZCL_ATTR_TYPE_UINT16,
                            ATTR_ACCESS, &voltage_mv)),
                        TAG, "BatteryVoltageMv");

    return esp_zigbee_err_to_esp(ezb_af_endpoint_add_cluster_desc(ep_desc, cluster));
}

static esp_err_t set_u16(uint16_t attr_id, uint16_t value)
{
    ezb_zcl_status_t status = ezb_zcl_set_attr_value(ZB_ENDPOINT, BATTERY_RUNTIME_CLUSTER_ID, EZB_ZCL_CLUSTER_SERVER,
                                                     attr_id, EZB_ZCL_STD_MANUF_CODE, &value, false);
    if (status != EZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Setting attribute 0x%04x failed (ZCL status 0x%02x)", attr_id, status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t battery_runtime_cluster_update(const battery_runtime_t *runtime)
{
    ESP_RETURN_ON_ERROR(set_u16(BATTERY_RUNTIME_ATTR_REMAINING_ON_TIME_ID, runtime->on_time_x10h), TAG, "On time");
    return set_u16(BATTERY_RUNTIME_ATTR_REMAINING_DAYS_ID, runtime->days_x10);
}

esp_err_t battery_runtime_cluster_set_voltage(uint16_t mv)
{
    return set_u16(BATTERY_RUNTIME_ATTR_VOLTAGE_MV_ID, mv);
}
