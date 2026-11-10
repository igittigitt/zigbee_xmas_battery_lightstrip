#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "zigbee_config.h"
#include "on_off_cluster.h"
#include "light.h"

static const char *TAG = "ON_OFF_CLUSTER";

static volatile bool s_on;
static bool s_sync_pending;     // OnOff attribute differs from the light after power-up (Zigbee task only)

static void apply_state(bool on)
{
    s_on = on;
    light_set_on(on);   // non-blocking, queued to the light task
    ESP_LOGI(TAG, "Light %s", on ? "ON" : "OFF");
}

static esp_err_t make_persistent(ezb_zcl_cluster_desc_t cluster, uint16_t attr_id, ezb_zcl_attr_access_t extra)
{
    ezb_zcl_attr_desc_t attr = ezb_zcl_cluster_get_attr_desc(cluster, attr_id, EZB_ZCL_STD_MANUF_CODE);
    ESP_RETURN_ON_FALSE(attr != EZB_INVALID_ZCL_ATTR_DESC, ESP_ERR_NOT_FOUND, TAG, "Attribute 0x%04x missing", attr_id);
    ezb_zcl_attr_access_t access = ezb_zcl_attr_desc_get_access(attr) | extra | EZB_ZCL_ATTR_ACCESS_PERSISTENT;
    return esp_zigbee_err_to_esp(ezb_zcl_attr_desc_set_access(attr, access));
}

esp_err_t on_off_cluster_add(ezb_af_ep_desc_t ep_desc)
{
    ezb_zcl_cluster_desc_t cluster = ezb_af_endpoint_get_cluster_desc(ep_desc, EZB_ZCL_CLUSTER_ID_ON_OFF,
                                                                       EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_FALSE(cluster, ESP_ERR_NOT_FOUND, TAG, "On/Off cluster missing");

    const uint8_t start_up = ZB_START_UP_ON_OFF_DEFAULT;
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_on_off_cluster_desc_add_attr(
                            cluster, EZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF_ID, &start_up)),
                        TAG, "StartUpOnOff");

    ESP_RETURN_ON_ERROR(make_persistent(cluster, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, 0), TAG, "OnOff");
    ESP_RETURN_ON_ERROR(make_persistent(cluster, EZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF_ID,
                                        EZB_ZCL_ATTR_ACCESS_READ_WRITE),
                        TAG, "StartUpOnOff access");
    return ESP_OK;
}

static bool read_u8_attr(uint16_t attr_id, uint8_t *value)
{
    ezb_zcl_attr_desc_t attr = ezb_zcl_get_attr_desc(ZB_ENDPOINT, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER,
                                                     attr_id, EZB_ZCL_STD_MANUF_CODE);
    return attr != EZB_INVALID_ZCL_ATTR_DESC && ezb_zcl_attr_desc_get_value(attr, value) == EZB_ERR_NONE;
}

bool on_off_cluster_load_startup(void)
{
    uint8_t start_up = ZB_START_UP_ON_OFF_DEFAULT;
    uint8_t previous = 0;

    if (!read_u8_attr(EZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF_ID, &start_up) ||
        !read_u8_attr(EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &previous)) {
        ESP_LOGE(TAG, "Reading StartUpOnOff/OnOff failed, light stays off");
        s_on = false;
        return false;
    }

    bool on;
    switch (start_up) {
    case EZB_ZCL_ON_OFF_START_UP_ON_OFF_SET_ON_OFF_TO_OFF:
        on = false;
        break;
    case EZB_ZCL_ON_OFF_START_UP_ON_OFF_SET_ON_OFF_TO_ON:
        on = true;
        break;
    case EZB_ZCL_ON_OFF_START_UP_ON_OFF_TOGGLE_PREVIOUS_ON_OFF:
        on = !previous;
        break;
    case EZB_ZCL_ON_OFF_START_UP_ON_OFF_SET_PREVIOUS_ON_OFF:
        on = previous;
        break;
    default:
        // Undefined value: stay off (saves battery)
        on = false;
        break;
    }
    ESP_LOGI(TAG, "StartUpOnOff 0x%02x, previous %s -> %s", start_up, previous ? "ON" : "OFF", on ? "ON" : "OFF");

    // The light is switched by the status indication (goals Z10/Z12) after
    // connecting; the OnOff attribute is written after joining, only if it changes.
    s_on = on;
    s_sync_pending = (on != (previous != 0));
    return on;
}

void on_off_cluster_sync_attribute(void)
{
    if (!s_sync_pending) {
        return;
    }
    s_sync_pending = false;

    uint8_t value = s_on ? 1 : 0;
    ezb_zcl_status_t status = ezb_zcl_set_attr_value(ZB_ENDPOINT, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER,
                                                     EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, EZB_ZCL_STD_MANUF_CODE, &value,
                                                     false);
    if (status != EZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Setting OnOff failed (ZCL status 0x%02x)", status);
    } else {
        ESP_LOGI(TAG, "OnOff attribute synchronised to %s", s_on ? "ON" : "OFF");
    }
}

void on_off_cluster_attribute_changed(const ezb_zcl_attribute_t *attribute)
{
    if (attribute->data.value == NULL) {
        return;
    }

    switch (attribute->id) {
    case EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID:
        apply_state(*(const uint8_t *)attribute->data.value != 0);
        break;

    case EZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF_ID:
        // Stored by the stack, evaluated at the next power-up
        ESP_LOGI(TAG, "StartUpOnOff set to 0x%02x", *(const uint8_t *)attribute->data.value);
        break;

    default:
        ESP_LOGD(TAG, "Attribute 0x%04x changed", attribute->id);
        break;
    }
}

/*
 * Send the OnOff attribute to the coordinator right away (Zigbee lock held).
 * Whether ezb_zcl_set_attr_value() alone triggers a report is not documented
 * (esp-zigbee-sdk 2.0.5 zcl_common.h / zcl_general_report.rst), and a local
 * switch was not shown in Home Assistant. The explicit report goes directly to
 * the coordinator, so it does not depend on a binding. Note from
 * zcl_general_cmd.h: "The actual attributes reported depend on the reporting
 * configuration"; a missing reporting entry is therefore logged.
 */
static void report_on_off(void)
{
    if (ezb_zcl_reporting_info_find(ZB_ENDPOINT, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER,
                                    EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, EZB_ZCL_STD_MANUF_CODE)
        == EZB_ZCL_INVALID_REPORTING_INFO) {
        ESP_LOGW(TAG, "No reporting entry for OnOff, report may not be sent");
    }

    ezb_zcl_report_attr_cmd_t cmd = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(ZB_COORDINATOR_SHORT_ADDR),
            .dst_ep = ZB_COORDINATOR_ENDPOINT,
            .src_ep = ZB_ENDPOINT,
            .cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
            .fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI,
        },
        .payload.attr_id = EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
    };
    ezb_err_t err = ezb_zcl_report_attr_cmd_req(&cmd);
    if (err != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "OnOff report failed: 0x%04x", (unsigned)err);
    }
}

esp_err_t on_off_cluster_set_state(bool on)
{
    uint8_t value = on ? 1 : 0;

    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_zcl_status_t status = ezb_zcl_set_attr_value(ZB_ENDPOINT, EZB_ZCL_CLUSTER_ID_ON_OFF,
                                                     EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                                                     EZB_ZCL_STD_MANUF_CODE, &value, false);
    if (status == EZB_ZCL_STATUS_SUCCESS) {
        report_on_off();
    }
    esp_zigbee_lock_release();

    if (status != EZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Setting OnOff failed (ZCL status 0x%02x)", status);
        return ESP_FAIL;
    }
    apply_state(on);
    return ESP_OK;
}

bool on_off_cluster_get_state(void)
{
    return s_on;
}
