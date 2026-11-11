#include "esp_check.h"
#include "esp_log.h"
#include "ezbee/zcl/cluster/custom.h"
#include "zigbee_config.h"
#include "zigbee_device.h"
#include "battery_config.h"
#include "battery_runtime_cluster.h"
#include "power_config_cluster.h"

static const char *TAG = "BATT_RUNTIME_CLUSTER";

#define ATTR_ACCESS     (EZB_ZCL_ATTR_ACCESS_READ | EZB_ZCL_ATTR_ACCESS_REPORTING)
#define ATTR_ACCESS_RW  (ATTR_ACCESS | EZB_ZCL_ATTR_ACCESS_WRITE)

static esp_err_t set_attr(uint16_t attr_id, void *value)
{
    ezb_zcl_status_t status = ezb_zcl_set_attr_value(ZB_ENDPOINT, BATTERY_RUNTIME_CLUSTER_ID, EZB_ZCL_CLUSTER_SERVER,
                                                     attr_id, EZB_ZCL_STD_MANUF_CODE, value, false);
    if (status != EZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Setting attribute 0x%04x failed (ZCL status 0x%02x)", attr_id, status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/** Datasheet capacity of a specific cell type, 0 for generic. */
static uint16_t type_capacity_mah(uint8_t type)
{
    switch (type) {
    case BATTERY_CELL_SAMSUNG_35E:
        return BATTERY_CAPACITY_35E_MAH;
    default:
        return 0;
    }
}

/* Stack: is the written value in range? Wrong values are rejected (INVALID_VALUE). */
static ezb_zcl_status_t check_value_cb(uint16_t attr_id, uint8_t ep_id, void *value)
{
    if (value == NULL) {
        return EZB_ZCL_STATUS_INVALID_VALUE;
    }
    switch (attr_id) {
    case BATTERY_RUNTIME_ATTR_CELL_TYPE_ID:
        return *(const uint8_t *)value < BATTERY_CELL_TYPE_COUNT ? EZB_ZCL_STATUS_SUCCESS
                                                                 : EZB_ZCL_STATUS_INVALID_VALUE;
    case BATTERY_RUNTIME_ATTR_CELL_CAPACITY_ID: {
        const uint16_t mah = *(const uint16_t *)value;
        return mah >= BATTERY_CELL_CAPACITY_MIN_MAH && mah <= BATTERY_CELL_CAPACITY_MAX_MAH
               ? EZB_ZCL_STATUS_SUCCESS : EZB_ZCL_STATUS_INVALID_VALUE;
    }
    case BATTERY_RUNTIME_ATTR_CELLS_PARALLEL_ID: {
        const uint8_t n = *(const uint8_t *)value;
        return n >= BATTERY_CELLS_PARALLEL_MIN && n <= BATTERY_CELLS_PARALLEL_MAX
               ? EZB_ZCL_STATUS_SUCCESS : EZB_ZCL_STATUS_INVALID_VALUE;
    }
    default:
        return EZB_ZCL_STATUS_SUCCESS;
    }
}

/*
 * Stack: an attribute was written by the coordinator (Zigbee task). Applies
 * the coupling between type and capacity, saves, starts the counter at the
 * state of charge from the voltage and reports the result.
 */
static void write_attr_cb(uint8_t ep_id, uint16_t attr_id, void *new_value, uint16_t manuf_code)
{
    if (ep_id != ZB_ENDPOINT || new_value == NULL) {
        return;
    }
    battery_cells_t cells;
    battery_model_get_cells(&cells);

    switch (attr_id) {
    case BATTERY_RUNTIME_ATTR_CELL_TYPE_ID:
        cells.type = *(const uint8_t *)new_value;
        if (type_capacity_mah(cells.type)) {
            cells.capacity_mah = type_capacity_mah(cells.type);
        }
        break;
    case BATTERY_RUNTIME_ATTR_CELL_CAPACITY_ID:
        cells.capacity_mah = *(const uint16_t *)new_value;
        if (cells.type != BATTERY_CELL_GENERIC_LIION && cells.capacity_mah != type_capacity_mah(cells.type)) {
            cells.type = BATTERY_CELL_GENERIC_LIION;     // other capacity = other cell
        }
        break;
    case BATTERY_RUNTIME_ATTR_CELLS_PARALLEL_ID:
        cells.parallel = *(const uint8_t *)new_value;
        break;
    default:
        return;
    }
    ESP_LOGI(TAG, "Cell configuration written (attribute 0x%04x): type %u, %u mAh x %u", attr_id, cells.type,
             cells.capacity_mah, cells.parallel);

    if (battery_model_set_cells(&cells, -1) != ESP_OK) {
        return;
    }
    const int soc = power_config_cluster_cells_changed();   // table of the new type, from the last voltage
    if (soc >= 0) {
        battery_model_set_cells(&cells, soc);               // counter starts at that state of charge
    }

    // Make the attributes match the applied configuration and tell HA
    set_attr(BATTERY_RUNTIME_ATTR_CELL_TYPE_ID, &cells.type);
    set_attr(BATTERY_RUNTIME_ATTR_CELL_CAPACITY_ID, &cells.capacity_mah);
    set_attr(BATTERY_RUNTIME_ATTR_CELLS_PARALLEL_ID, &cells.parallel);
    battery_runtime_cluster_refresh();
    zigbee_device_report_attr(BATTERY_RUNTIME_CLUSTER_ID, BATTERY_RUNTIME_ATTR_CELL_TYPE_ID);
    zigbee_device_report_attr(BATTERY_RUNTIME_CLUSTER_ID, BATTERY_RUNTIME_ATTR_CELL_CAPACITY_ID);
    zigbee_device_report_attr(BATTERY_RUNTIME_CLUSTER_ID, BATTERY_RUNTIME_ATTR_CELLS_PARALLEL_ID);
    zigbee_device_report_attr(BATTERY_RUNTIME_CLUSTER_ID, BATTERY_RUNTIME_ATTR_REMAINING_ON_TIME_ID);
    zigbee_device_report_attr(EZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                              EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID);
}

/* Cluster init (called by the stack): register the write handlers. Same place
 * as in the esp-zigbee-sdk 2.0.5 data_stream examples (init_func). */
static void cluster_init(uint8_t ep_id)
{
    const ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id = BATTERY_RUNTIME_CLUSTER_ID,
        .cluster_role = EZB_ZCL_CLUSTER_SERVER,
        .check_value_cb = check_value_cb,
        .write_attr_cb = write_attr_cb,
    };
    ezb_err_t err = ezb_zcl_custom_cluster_handlers_register(&handlers);
    if (err != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Registering the write handlers failed: 0x%04x", (unsigned)err);
    }
}

esp_err_t battery_runtime_cluster_add(ezb_af_ep_desc_t ep_desc)
{
    const ezb_zcl_custom_cluster_config_t cfg = {
        .cluster_id = BATTERY_RUNTIME_CLUSTER_ID,
        .init_func = cluster_init,
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

    battery_cells_t cells;
    battery_model_get_cells(&cells);
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_custom_cluster_desc_add_attr(
                            cluster, BATTERY_RUNTIME_ATTR_CELL_TYPE_ID, EZB_ZCL_ATTR_TYPE_ENUM8,
                            ATTR_ACCESS_RW, &cells.type)),
                        TAG, "CellType");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_custom_cluster_desc_add_attr(
                            cluster, BATTERY_RUNTIME_ATTR_CELL_CAPACITY_ID, EZB_ZCL_ATTR_TYPE_UINT16,
                            ATTR_ACCESS_RW, &cells.capacity_mah)),
                        TAG, "CellCapacity");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_custom_cluster_desc_add_attr(
                            cluster, BATTERY_RUNTIME_ATTR_CELLS_PARALLEL_ID, EZB_ZCL_ATTR_TYPE_UINT8,
                            ATTR_ACCESS_RW, &cells.parallel)),
                        TAG, "CellsParallel");

    return esp_zigbee_err_to_esp(ezb_af_endpoint_add_cluster_desc(ep_desc, cluster));
}

static esp_err_t set_u16(uint16_t attr_id, uint16_t value)
{
    return set_attr(attr_id, &value);
}

esp_err_t battery_runtime_cluster_update(const battery_runtime_t *runtime)
{
    ESP_RETURN_ON_ERROR(set_u16(BATTERY_RUNTIME_ATTR_REMAINING_ON_TIME_ID, runtime->on_time_x10h), TAG, "On time");
    return set_u16(BATTERY_RUNTIME_ATTR_REMAINING_DAYS_ID, runtime->days_x10);
}

esp_err_t battery_runtime_cluster_refresh(void)
{
    battery_runtime_t rt;
    battery_model_get(&rt);
    ESP_LOGI(TAG, "Recomputed: light-on %u.%u h, days %u.%u (6553.5 = unknown)",
             rt.on_time_x10h / 10, rt.on_time_x10h % 10, rt.days_x10 / 10, rt.days_x10 % 10);
    return battery_runtime_cluster_update(&rt);
}

esp_err_t battery_runtime_cluster_set_voltage(uint16_t mv)
{
    return set_u16(BATTERY_RUNTIME_ATTR_VOLTAGE_MV_ID, mv);
}
