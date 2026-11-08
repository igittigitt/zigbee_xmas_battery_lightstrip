#include "esp_check.h"
#include "esp_log.h"
#include "zigbee_config.h"
#include "level_cluster.h"
#include "light.h"
#include "light_config.h"

static const char *TAG = "LEVEL_CLUSTER";

static uint8_t s_level = ZB_LEVEL_DEFAULT;  // ZCL CurrentLevel (Zigbee task only)
static bool s_sync_pending;                 // CurrentLevel differs after power-up (Zigbee task only)

/* ZCL level 0x00..0xFE -> light level 1..LIGHT_LEVEL_MAX ("on" never means dark). */
static uint8_t zcl_to_light_level(uint8_t zcl_level)
{
    if (zcl_level > LEVEL_ZCL_MAX) {
        zcl_level = LEVEL_ZCL_MAX;
    }
    uint32_t level = (uint32_t)zcl_level * LIGHT_LEVEL_MAX / LEVEL_ZCL_MAX;
    return level ? (uint8_t)level : 1;
}

static void apply_level(uint8_t zcl_level)
{
    s_level = zcl_level;
    esp_err_t err = light_set_on_level(zcl_to_light_level(zcl_level));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Setting brightness failed: %s", esp_err_to_name(err));
    }
}

static esp_err_t make_persistent(ezb_zcl_cluster_desc_t cluster, uint16_t attr_id, ezb_zcl_attr_access_t extra)
{
    ezb_zcl_attr_desc_t attr = ezb_zcl_cluster_get_attr_desc(cluster, attr_id, EZB_ZCL_STD_MANUF_CODE);
    ESP_RETURN_ON_FALSE(attr != EZB_INVALID_ZCL_ATTR_DESC, ESP_ERR_NOT_FOUND, TAG, "Attribute 0x%04x missing", attr_id);
    ezb_zcl_attr_access_t access = ezb_zcl_attr_desc_get_access(attr) | extra | EZB_ZCL_ATTR_ACCESS_PERSISTENT;
    return esp_zigbee_err_to_esp(ezb_zcl_attr_desc_set_access(attr, access));
}

esp_err_t level_cluster_add(ezb_af_ep_desc_t ep_desc)
{
    ezb_zcl_cluster_desc_t cluster = ezb_af_endpoint_get_cluster_desc(ep_desc, EZB_ZCL_CLUSTER_ID_LEVEL,
                                                                       EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_FALSE(cluster, ESP_ERR_NOT_FOUND, TAG, "Level Control cluster missing");

    const uint8_t start_up = ZB_START_UP_CURRENT_LEVEL_DEFAULT;
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_level_cluster_desc_add_attr(
                            cluster, EZB_ZCL_ATTR_LEVEL_START_UP_CURRENT_LEVEL_ID, &start_up)),
                        TAG, "StartUpCurrentLevel");

    ESP_RETURN_ON_ERROR(make_persistent(cluster, EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID, 0), TAG, "CurrentLevel");
    ESP_RETURN_ON_ERROR(make_persistent(cluster, EZB_ZCL_ATTR_LEVEL_START_UP_CURRENT_LEVEL_ID,
                                        EZB_ZCL_ATTR_ACCESS_READ_WRITE),
                        TAG, "StartUpCurrentLevel access");
    return ESP_OK;
}

static bool read_u8_attr(uint16_t attr_id, uint8_t *value)
{
    ezb_zcl_attr_desc_t attr = ezb_zcl_get_attr_desc(ZB_ENDPOINT, EZB_ZCL_CLUSTER_ID_LEVEL, EZB_ZCL_CLUSTER_SERVER,
                                                     attr_id, EZB_ZCL_STD_MANUF_CODE);
    return attr != EZB_INVALID_ZCL_ATTR_DESC && ezb_zcl_attr_desc_get_value(attr, value) == EZB_ERR_NONE;
}

void level_cluster_load_startup(void)
{
    uint8_t start_up = ZB_START_UP_CURRENT_LEVEL_DEFAULT;
    uint8_t previous = ZB_LEVEL_DEFAULT;

    if (!read_u8_attr(EZB_ZCL_ATTR_LEVEL_START_UP_CURRENT_LEVEL_ID, &start_up) ||
        !read_u8_attr(EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID, &previous)) {
        ESP_LOGE(TAG, "Reading StartUpCurrentLevel/CurrentLevel failed, using %u", ZB_LEVEL_DEFAULT);
        apply_level(ZB_LEVEL_DEFAULT);
        return;
    }
    if (previous > LEVEL_ZCL_MAX) {
        previous = ZB_LEVEL_DEFAULT;    // 0xFF: never set (stack default)
    }

    uint8_t level;
    if (start_up == 0xFF) {
        level = previous;               // previous level
    } else if (start_up == 0x00) {
        level = 1;                      // minimum (MinLevel of a lighting device)
    } else {
        level = start_up;               // fixed level 0x01..0xFE
    }
    ESP_LOGI(TAG, "StartUpCurrentLevel 0x%02x, previous %u -> %u", start_up, previous, level);

    apply_level(level);
    uint8_t stored = 0;
    read_u8_attr(EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID, &stored);
    s_sync_pending = (level != stored);
}

void level_cluster_sync_attribute(void)
{
    if (!s_sync_pending) {
        return;
    }
    s_sync_pending = false;

    uint8_t value = s_level;
    ezb_zcl_status_t status = ezb_zcl_set_attr_value(ZB_ENDPOINT, EZB_ZCL_CLUSTER_ID_LEVEL, EZB_ZCL_CLUSTER_SERVER,
                                                     EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID, EZB_ZCL_STD_MANUF_CODE,
                                                     &value, false);
    if (status != EZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Setting CurrentLevel failed (ZCL status 0x%02x)", status);
    } else {
        ESP_LOGI(TAG, "CurrentLevel attribute synchronised to %u", value);
    }
}

void level_cluster_attribute_changed(const ezb_zcl_attribute_t *attribute)
{
    if (attribute->data.value == NULL) {
        return;
    }

    switch (attribute->id) {
    case EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID: {
        uint8_t level = *(const uint8_t *)attribute->data.value;
        ESP_LOGI(TAG, "Level %u", level);
        apply_level(level);
        break;
    }

    case EZB_ZCL_ATTR_LEVEL_START_UP_CURRENT_LEVEL_ID:
        // Stored by the stack, evaluated at the next power-up
        ESP_LOGI(TAG, "StartUpCurrentLevel set to 0x%02x", *(const uint8_t *)attribute->data.value);
        break;

    default:
        ESP_LOGD(TAG, "Attribute 0x%04x changed", attribute->id);
        break;
    }
}
