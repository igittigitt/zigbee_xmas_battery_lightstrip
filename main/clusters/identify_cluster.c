#include "esp_log.h"
#include "zigbee_config.h"
#include "identify_cluster.h"
#include "light.h"
#include "light_config.h"

static const char *TAG = "IDENTIFY_CLUSTER";

/* Last IdentifyTime value seen (Zigbee task only). */
static uint16_t s_last_identify_time;

static void identify_blink(void)
{
    const uint32_t period_ms = 1000 / ZB_IDENTIFY_BLINK_HZ;
    const light_pattern_t pattern = {
        .level = LIGHT_LEVEL_MAX,
        .on_ms = period_ms / 2,
        .off_ms = period_ms / 2,
        .count = ZB_IDENTIFY_DURATION_MS / period_ms,
    };
    esp_err_t err = light_pattern_start(&pattern);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Blinking failed: %s", esp_err_to_name(err));
    }
}

void identify_cluster_attribute_changed(const ezb_zcl_attribute_t *attribute)
{
    if (attribute->id != EZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID || attribute->data.value == NULL ||
        attribute->data.size < sizeof(uint16_t)) {
        ESP_LOGD(TAG, "Attribute 0x%04x changed", attribute->id);
        return;
    }

    uint16_t identify_time = *(const uint16_t *)attribute->data.value;
    ESP_LOGI(TAG, "IdentifyTime %u s", identify_time);

    // New Identify command: value rises (from 0 or above the running countdown)
    if (identify_time > s_last_identify_time) {
        ESP_LOGI(TAG, "Identify: blinking %d Hz for %d ms", ZB_IDENTIFY_BLINK_HZ, ZB_IDENTIFY_DURATION_MS);
        identify_blink();
    }
    s_last_identify_time = identify_time;
}

void identify_cluster_trigger_effect(ezb_zcl_identify_effect_message_t *message)
{
    ESP_LOGI(TAG, "TriggerEffect 0x%02x (variant 0x%02x): blinking", message->in.effect_id,
             message->in.effect_variant);
    identify_blink();
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
}
