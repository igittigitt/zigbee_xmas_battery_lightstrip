#include <stdbool.h>
#include "esp_log.h"
#include "ezbee/aps.h"
#include "ezbee/af.h"
#include "zigbee_config.h"
#include "read_request_handler.h"
#include "power_config_cluster.h"
#include "battery_runtime_cluster.h"

static const char *TAG = "READ_REQUEST";

/*
 * ZCL frame header (ZCL specification 07-5123, chapter 2.4.1 "General ZCL
 * Frame Format"): frame control (1 byte), manufacturer code (2 bytes, only if
 * the manufacturer-specific bit is set), transaction sequence number (1 byte),
 * command identifier (1 byte). Read Attributes payload (chapter 2.5.1): list
 * of attribute identifiers, uint16 little endian.
 */
#define ZCL_FC_FRAME_TYPE_MASK      0x03    // bits 0-1: 0b00 = global (profile-wide) command
#define ZCL_FC_FRAME_TYPE_GLOBAL    0x00
#define ZCL_FC_MANUF_SPECIFIC       0x04    // bit 2: manufacturer code present
#define ZCL_FC_SERVER_TO_CLIENT     0x08    // bit 3: direction, 0 = client to server
#define ZCL_HDR_LEN_MIN             3       // frame control, sequence number, command id
#define ZCL_MANUF_CODE_LEN          2

/** What has to happen before the stack answers. */
typedef enum {
    READ_STORED,        // value stored in the attribute is current (state, configuration)
    READ_MEASURE,       // measured value: measure again
    READ_RECOMPUTE,     // computed value: compute again
} read_action_t;

static read_action_t action_for(uint16_t cluster_id, uint16_t attr_id)
{
    if (cluster_id == EZB_ZCL_CLUSTER_ID_POWER_CONFIG &&
        (attr_id == EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID ||
         attr_id == EZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID)) {
        return READ_MEASURE;
    }
    if (cluster_id == BATTERY_RUNTIME_CLUSTER_ID) {
        if (attr_id == BATTERY_RUNTIME_ATTR_VOLTAGE_MV_ID) {
            return READ_MEASURE;
        }
        if (attr_id == BATTERY_RUNTIME_ATTR_REMAINING_ON_TIME_ID ||
            attr_id == BATTERY_RUNTIME_ATTR_REMAINING_DAYS_ID) {
            return READ_RECOMPUTE;
        }
    }
    return READ_STORED;
}

static const char *action_name(read_action_t action)
{
    switch (action) {
    case READ_MEASURE:
        return "measure";
    case READ_RECOMPUTE:
        return "recompute";
    default:
        return "stored value";
    }
}

/*
 * Stack callback, called before the stack processes the frame. Runs in the
 * Zigbee task (like the other stack callbacks; aps.h does not state the
 * context explicitly), so the Zigbee API is used without the lock. Blocks only
 * for a battery measurement (ADC samples, a few ms).
 * Returns false: the stack always processes the frame itself and answers.
 */
static bool aps_indication_cb(const ezb_apsde_data_ind_t *ind)
{
    if (ind->dst_endpoint != ZB_ENDPOINT || ind->profile_id != EZB_AF_HA_PROFILE_ID ||
        ind->asdu == NULL || ind->asdu_length < ZCL_HDR_LEN_MIN) {
        return false;
    }
    const uint8_t *zcl = ind->asdu;
    const uint8_t fc = zcl[0];
    if ((fc & ZCL_FC_FRAME_TYPE_MASK) != ZCL_FC_FRAME_TYPE_GLOBAL || (fc & ZCL_FC_SERVER_TO_CLIENT)) {
        return false;
    }
    const uint16_t hdr_len = ZCL_HDR_LEN_MIN + ((fc & ZCL_FC_MANUF_SPECIFIC) ? ZCL_MANUF_CODE_LEN : 0);
    if (ind->asdu_length < hdr_len || zcl[hdr_len - 1] != EZB_ZCL_CMD_READ_ATTRIBUTES) {
        return false;
    }

    const uint8_t *payload = zcl + hdr_len;
    const uint16_t count = (ind->asdu_length - hdr_len) / 2;
    ESP_LOGI(TAG, "Read request: cluster 0x%04x, %u attribute(s), seq %u", ind->cluster_id, count,
             zcl[hdr_len - 2]);

    bool measure = false;
    bool recompute = false;
    for (uint16_t i = 0; i < count; i++) {
        const uint16_t attr_id = (uint16_t)(payload[2 * i] | (payload[2 * i + 1] << 8));
        const read_action_t action = action_for(ind->cluster_id, attr_id);
        ESP_LOGI(TAG, "  attribute 0x%04x: %s", attr_id, action_name(action));
        measure |= (action == READ_MEASURE);
        recompute |= (action == READ_RECOMPUTE);
    }

    if (measure) {
        power_config_cluster_refresh();     // logs the measurement; on error the old values stay
    }
    if (recompute) {
        battery_runtime_cluster_refresh();  // logs the new remaining time
    }
    return false;
}

esp_err_t read_request_handler_init(void)
{
    ezb_apsde_data_indication_handler_register(aps_indication_cb);
    return ESP_OK;
}
