#include "esp_log.h"
#include "zigbee_config.h"
#include "attribute_handler.h"
#include "on_off_cluster.h"
#include "level_cluster.h"
#include "identify_cluster.h"

static const char *TAG = "ATTR_HANDLER";

static void set_attr_value_handler(const ezb_zcl_set_attr_value_message_t *message)
{
    if (message->info.status != EZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "SetAttributeValue with status 0x%02x ignored", message->info.status);
        return;
    }
    if (message->info.dst_ep != ZB_ENDPOINT || message->info.cluster_role != EZB_ZCL_CLUSTER_SERVER) {
        ESP_LOGW(TAG, "Unexpected endpoint %d / role %d", message->info.dst_ep, message->info.cluster_role);
        return;
    }

    switch (message->info.cluster_id) {
    case EZB_ZCL_CLUSTER_ID_ON_OFF:
        on_off_cluster_attribute_changed(&message->in.attribute);
        break;

    case EZB_ZCL_CLUSTER_ID_LEVEL:
        level_cluster_attribute_changed(&message->in.attribute);
        break;

    case EZB_ZCL_CLUSTER_ID_IDENTIFY:
        identify_cluster_attribute_changed(&message->in.attribute);
        break;

    default:
        // Groups, Scenes: handled by the stack, nothing to do here
        ESP_LOGD(TAG, "Cluster 0x%04x attribute 0x%04x changed", message->info.cluster_id,
                 message->in.attribute.id);
        break;
    }
}

void attribute_handler_callback(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    if (message == NULL) {
        return;
    }

    switch (callback_id) {
    case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID:
        set_attr_value_handler(message);
        break;

    case EZB_ZCL_CORE_IDENTIFY_EFFECT_CB_ID:
        identify_cluster_trigger_effect(message);
        break;

    case EZB_ZCL_CORE_DEFAULT_RSP_CB_ID:
        ESP_LOGD(TAG, "Default response, status 0x%02x",
                 ((const ezb_zcl_cmd_default_rsp_message_t *)message)->in.status_code);
        break;

    default:
        // INFO for now, to see which actions the coordinator triggers (hardware test phase)
        ESP_LOGI(TAG, "ZCL action 0x%04lx", (unsigned long)callback_id);
        break;
    }
}
