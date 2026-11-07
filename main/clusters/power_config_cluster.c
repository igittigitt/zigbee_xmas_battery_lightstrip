#include "esp_check.h"
#include "esp_log.h"
#include "app_config.h"
#include "power_config_cluster.h"

static const char *TAG = "POWER_CFG_CLUSTER";

/** ZCL "invalid / unknown" value for BatteryVoltage and BatteryPercentageRemaining. */
#define POWER_CONFIG_VALUE_UNKNOWN  0xFF

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

    const uint8_t unknown = POWER_CONFIG_VALUE_UNKNOWN;
    // DEBUG: emulated level (unit 0.5 %, 200 = 100 %), production: unknown until goal Z7
    const uint8_t percentage = APP_DEBUG ? (uint8_t)(APP_DEBUG_BATTERY_PERCENT * 2) : unknown;

    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_power_config_cluster_desc_add_attr(
                            cluster, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &unknown)),
                        TAG, "BatteryVoltage");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_power_config_cluster_desc_add_attr(
                            cluster, EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID, &percentage)),
                        TAG, "BatteryPercentageRemaining");
    if (APP_DEBUG) {
        ESP_LOGW(TAG, "DEBUG: emulated battery level %d %%", APP_DEBUG_BATTERY_PERCENT);
    }

    return esp_zigbee_err_to_esp(ezb_af_endpoint_add_cluster_desc(ep_desc, cluster));
}
