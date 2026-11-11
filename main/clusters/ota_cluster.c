/*
 * OTA Upgrade client (goal Z16), see ota_cluster.h.
 *
 * Based on esp-zigbee-sdk 2.0.5 (commit e2fdf4ad) examples/ota_upgrade/ota_client
 * (ota_client.c, ota_file_parser.c), with these differences:
 *   - esp_ota_begin() with OTA_WITH_SEQUENTIAL_WRITES: erases sector by sector
 *     while writing instead of the whole slot at once (which would block the
 *     Zigbee task for the erase of 1.5 MB; esp_ota_ops.h, IDF 5.5.4)
 *   - own stream parser without heap; checks file identifier, manufacturer
 *     code and image type of the OTA file header against ZB_OTA_*
 *   - restart via esp_timer instead of inside the callback (no blocking in
 *     action callbacks, AGENTS.md)
 *   - rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE), battery check
 *
 * Threading: everything runs in the Zigbee task (action callbacks), except
 * the restart timer callback, which uses no Zigbee API.
 */
#include <inttypes.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_zigbee.h"
#include "zigbee_config.h"
#include "ota_cluster.h"
#include "power_config_cluster.h"
#include "battery_config.h"
#include "battery_model.h"

static const char *TAG = "OTA_CLUSTER";

/* Attribute values must stay valid while the endpoint exists (as in cluster_manager.c). */
static uint32_t s_current_file_version = ZB_OTA_FILE_VERSION;

static esp_timer_handle_t s_reboot_timer;

typedef enum {
    PARSE_FILE_HEADER,      // collecting the mandatory OTA file header
    PARSE_ELEMENT_HEADER,   // collecting a sub-element header (tag, length)
    PARSE_ELEMENT_DATA,     // writing the upgrade image sub-element
} parse_state_t;

/* Download context, one download at a time. */
static struct {
    bool active;                        // esp_ota_begin() done, handle open
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    uint32_t file_size;                 // total size of the OTA file (from START)
    uint32_t file_offset;               // bytes of the OTA file received so far
    parse_state_t state;
    uint8_t buf[sizeof(ezb_zcl_ota_file_header_t)];
    uint32_t buf_len;
    uint32_t skip;                      // bytes to skip (optional header fields, other sub-elements)
    uint32_t element_remaining;         // bytes left in the upgrade image sub-element
    bool image_seen;
    bool image_complete;
} s_ota;

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static void set_keepalive(uint32_t interval_ms)
{
    ezb_nwk_set_keepalive_interval(interval_ms);
    ESP_LOGI(TAG, "Keepalive %" PRIu32 " ms", interval_ms);
}

/** Close an open download (abort the OTA handle) and restore the keepalive. */
static void download_cleanup(void)
{
    if (s_ota.active) {
        esp_ota_abort(s_ota.handle);
        set_keepalive(ZB_KEEP_ALIVE_MS);
    }
    memset(&s_ota, 0, sizeof(s_ota));
}

static void reboot_timer_cb(void *arg)
{
    ESP_LOGW(TAG, "Restarting into the new firmware");
    esp_restart();
}

/* ---------------------------------------------------------------------------
 * Zigbee OTA file parser (ezbee/zcl/cluster/ota_file.h, little endian)
 * ------------------------------------------------------------------------- */

/** Copy up to @p want bytes into s_ota.buf; returns the number of bytes taken. */
static size_t collect(const uint8_t *data, size_t len, size_t want)
{
    size_t n = want - s_ota.buf_len;
    if (n > len) {
        n = len;
    }
    memcpy(&s_ota.buf[s_ota.buf_len], data, n);
    s_ota.buf_len += n;
    return n;
}

static esp_err_t parse_file_header(void)
{
    ezb_zcl_ota_file_header_t hdr;
    memcpy(&hdr, s_ota.buf, sizeof(hdr));
    ESP_RETURN_ON_FALSE(hdr.file_id == EZB_ZCL_OTA_FILE_IDENTIFIER, ESP_ERR_INVALID_RESPONSE, TAG,
                        "No Zigbee OTA file (identifier 0x%08" PRIx32 ")", hdr.file_id);
    ESP_RETURN_ON_FALSE(hdr.hdr_length >= sizeof(hdr), ESP_ERR_INVALID_SIZE, TAG,
                        "Header length %u too small", hdr.hdr_length);
    ESP_RETURN_ON_FALSE(hdr.manufacturer_code == ZB_OTA_MANUFACTURER_CODE && hdr.image_type == ZB_OTA_IMAGE_TYPE,
                        ESP_ERR_INVALID_VERSION, TAG, "Image for 0x%04x/0x%04x, expected 0x%04x/0x%04x",
                        hdr.manufacturer_code, hdr.image_type, ZB_OTA_MANUFACTURER_CODE, ZB_OTA_IMAGE_TYPE);
    ESP_RETURN_ON_FALSE(hdr.total_image_size == s_ota.file_size, ESP_ERR_INVALID_SIZE, TAG,
                        "File size %" PRIu32 " in header, %" PRIu32 " announced", hdr.total_image_size,
                        s_ota.file_size);
    ESP_LOGI(TAG, "OTA file version 0x%08" PRIx32 ", header %u bytes", hdr.file_version, hdr.hdr_length);
    s_ota.skip = hdr.hdr_length - sizeof(hdr);      // optional header fields
    s_ota.state = PARSE_ELEMENT_HEADER;
    return ESP_OK;
}

static esp_err_t parse_element_header(void)
{
    ezb_zcl_ota_file_sub_element_header_t el;
    memcpy(&el, s_ota.buf, sizeof(el));
    if (el.tag_id != EZB_ZCL_OTA_FILE_TAG_ID_UPGRADE_IMAGE) {
        ESP_LOGW(TAG, "Skipping sub-element tag 0x%04x (%" PRIu32 " bytes)", el.tag_id, el.length);
        s_ota.skip = el.length;
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(!s_ota.image_seen, ESP_ERR_INVALID_STATE, TAG, "Second upgrade image sub-element");
    ESP_RETURN_ON_FALSE(el.length <= s_ota.partition->size, ESP_ERR_INVALID_SIZE, TAG,
                        "Image %" PRIu32 " bytes > partition %" PRIu32 " bytes", el.length, s_ota.partition->size);
    ESP_LOGI(TAG, "Upgrade image: %" PRIu32 " bytes -> %s", el.length, s_ota.partition->label);
    s_ota.image_seen = true;
    s_ota.element_remaining = el.length;
    s_ota.image_complete = (el.length == 0);
    s_ota.state = el.length ? PARSE_ELEMENT_DATA : PARSE_ELEMENT_HEADER;
    return ESP_OK;
}

static esp_err_t parse_block(const uint8_t *data, size_t len)
{
    while (len > 0) {
        size_t n;
        if (s_ota.skip > 0) {
            n = (len < s_ota.skip) ? len : s_ota.skip;
            s_ota.skip -= n;
        } else if (s_ota.state == PARSE_FILE_HEADER) {
            n = collect(data, len, sizeof(ezb_zcl_ota_file_header_t));
            if (s_ota.buf_len == sizeof(ezb_zcl_ota_file_header_t)) {
                s_ota.buf_len = 0;
                ESP_RETURN_ON_ERROR(parse_file_header(), TAG, "File header");
            }
        } else if (s_ota.state == PARSE_ELEMENT_HEADER) {
            n = collect(data, len, sizeof(ezb_zcl_ota_file_sub_element_header_t));
            if (s_ota.buf_len == sizeof(ezb_zcl_ota_file_sub_element_header_t)) {
                s_ota.buf_len = 0;
                ESP_RETURN_ON_ERROR(parse_element_header(), TAG, "Sub-element header");
            }
        } else {
            n = (len < s_ota.element_remaining) ? len : s_ota.element_remaining;
            ESP_RETURN_ON_ERROR(esp_ota_write(s_ota.handle, data, n), TAG, "esp_ota_write");
            s_ota.element_remaining -= n;
            if (s_ota.element_remaining == 0) {
                s_ota.image_complete = true;
                s_ota.state = PARSE_ELEMENT_HEADER;
            }
        }
        data += n;
        len -= n;
    }
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * Progress steps
 * ------------------------------------------------------------------------- */

static esp_err_t on_start(const ezb_zcl_ota_upgrade_client_progress_start_t *start)
{
    ESP_LOGI(TAG, "OTA start: 0x%04x/0x%04x version 0x%08" PRIx32 " -> 0x%08" PRIx32 ", %" PRIu32 " bytes",
             start->manuf_code, start->image_type, s_current_file_version, start->file_version,
             start->image_size);
    download_cleanup();     // a previous, unfinished download

    // Do not start a download (~30 min of fast polling, docs/zigbee.md) on a weak battery.
    uint16_t mv = power_config_cluster_get_voltage_mv();
    ESP_RETURN_ON_FALSE(mv == BATTERY_UNKNOWN_U16 || mv >= BATTERY_WARN_MV, ESP_ERR_INVALID_STATE, TAG,
                        "Battery %u mV below %u mV, OTA refused", mv, BATTERY_WARN_MV);
    ESP_RETURN_ON_FALSE(start->manuf_code == ZB_OTA_MANUFACTURER_CODE && start->image_type == ZB_OTA_IMAGE_TYPE,
                        ESP_ERR_INVALID_VERSION, TAG, "Image not for this device");

    s_ota.partition = esp_ota_get_next_update_partition(NULL);
    ESP_RETURN_ON_FALSE(s_ota.partition, ESP_ERR_NOT_FOUND, TAG, "No OTA partition");
    ESP_RETURN_ON_ERROR(esp_ota_begin(s_ota.partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ota.handle), TAG,
                        "esp_ota_begin");
    s_ota.active = true;
    s_ota.file_size = start->image_size;
    s_ota.state = PARSE_FILE_HEADER;
    set_keepalive(ZB_OTA_KEEP_ALIVE_MS);
    return ESP_OK;
}

static esp_err_t on_receiving(const ezb_zcl_ota_upgrade_client_progress_receiving_t *rx)
{
    ESP_RETURN_ON_FALSE(s_ota.active, ESP_ERR_INVALID_STATE, TAG, "Block without download");
    ESP_RETURN_ON_FALSE(rx->file_offset == s_ota.file_offset, ESP_ERR_INVALID_STATE, TAG,
                        "Block at offset %" PRIu32 ", expected %" PRIu32, rx->file_offset, s_ota.file_offset);
    ESP_RETURN_ON_FALSE(s_ota.file_offset + rx->block_size <= s_ota.file_size, ESP_ERR_INVALID_SIZE, TAG,
                        "Block beyond the file end");
    ESP_RETURN_ON_ERROR(parse_block(rx->block, rx->block_size), TAG, "Block at offset %" PRIu32, rx->file_offset);
    s_ota.file_offset += rx->block_size;
    ESP_LOGD(TAG, "%" PRIu32 "/%" PRIu32 " bytes", s_ota.file_offset, s_ota.file_size);
    return ESP_OK;
}

static esp_err_t on_check(void)
{
    ESP_RETURN_ON_FALSE(s_ota.active, ESP_ERR_INVALID_STATE, TAG, "Check without download");
    ESP_RETURN_ON_FALSE(s_ota.file_offset == s_ota.file_size && s_ota.image_complete && s_ota.skip == 0 &&
                        s_ota.buf_len == 0 && s_ota.state == PARSE_ELEMENT_HEADER,
                        ESP_ERR_INVALID_SIZE, TAG, "Incomplete OTA file (%" PRIu32 "/%" PRIu32 " bytes)",
                        s_ota.file_offset, s_ota.file_size);
    return ESP_OK;
}

static esp_err_t on_apply(void)
{
    ESP_RETURN_ON_FALSE(s_ota.active, ESP_ERR_INVALID_STATE, TAG, "Apply without download");
    s_ota.active = false;   // esp_ota_end() releases the handle also on failure
    set_keepalive(ZB_KEEP_ALIVE_MS);
    // esp_ota_end validates the written app image (ESP_ERR_OTA_VALIDATE_FAILED, esp_ota_ops.h)
    ESP_RETURN_ON_ERROR(esp_ota_end(s_ota.handle), TAG, "esp_ota_end");
    ESP_RETURN_ON_ERROR(esp_ota_set_boot_partition(s_ota.partition), TAG, "esp_ota_set_boot_partition");
    ESP_LOGI(TAG, "New firmware in %s, boot partition switched", s_ota.partition->label);
    return ESP_OK;
}

static esp_err_t on_finish(const ezb_zcl_ota_upgrade_client_progress_finish_t *finish)
{
    // How the stack uses the server's upgrade time is not documented [UNBELEGT]; logged only.
    ESP_LOGI(TAG, "OTA finished (count down %" PRIu32 " s), restart in %d ms", finish->count_down_delay,
             ZB_OTA_REBOOT_DELAY_MS);
    memset(&s_ota, 0, sizeof(s_ota));
    if (s_reboot_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = reboot_timer_cb,
            .name = "ota_reboot",
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_reboot_timer), TAG, "Reboot timer");
    }
    return esp_timer_start_once(s_reboot_timer, (uint64_t)ZB_OTA_REBOOT_DELAY_MS * 1000);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

esp_err_t ota_cluster_add(ezb_af_ep_desc_t ep_desc)
{
    // Values as in the esp-zigbee-sdk 2.0.5 example ota_client
    const ezb_zcl_ota_upgrade_cluster_client_config_t cfg = {
        .upgrade_server_id = EZB_ZCL_OTA_UPGRADE_UPGRADE_SERVER_ID_DEFAULT_VALUE,
        .file_offset = 0,
        .image_upgrade_status = EZB_ZCL_OTA_UPGRADE_IMAGE_UPGRADE_STATUS_DEFAULT_VALUE,
        .manufacturer_id = ZB_OTA_MANUFACTURER_CODE,
        .image_type_id = ZB_OTA_IMAGE_TYPE,
    };
    ezb_zcl_cluster_desc_t cluster = ezb_zcl_ota_upgrade_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_CLIENT);
    ESP_RETURN_ON_FALSE(cluster, ESP_ERR_NO_MEM, TAG, "OTA cluster allocation");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_zcl_ota_upgrade_cluster_desc_add_attr(
                            cluster, EZB_ZCL_ATTR_OTA_UPGRADE_CURRENT_FILE_VERSION_ID, &s_current_file_version)),
                        TAG, "CurrentFileVersion");
    ESP_LOGI(TAG, "OTA client 0x%04x/0x%04x, file version 0x%08" PRIx32, ZB_OTA_MANUFACTURER_CODE,
             ZB_OTA_IMAGE_TYPE, s_current_file_version);
    return esp_zigbee_err_to_esp(ezb_af_endpoint_add_cluster_desc(ep_desc, cluster));
}

esp_err_t ota_cluster_init(void)
{
    return esp_zigbee_err_to_esp(ezb_zcl_ota_upgrade_set_download_block_size(ZB_ENDPOINT, ZB_OTA_BLOCK_SIZE));
}

void ota_cluster_progress(ezb_zcl_ota_upgrade_client_progress_message_t *message)
{
    esp_err_t err = ESP_OK;
    switch (message->in.progress) {
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_START:
        err = on_start(&message->in.start);
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING:
        err = on_receiving(&message->in.receiving);
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_CHECK:
        err = on_check();
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_APPLY:
        err = on_apply();
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH:
        err = on_finish(&message->in.finish);
        break;
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT:
        ESP_LOGW(TAG, "OTA aborted");
        download_cleanup();
        break;
    default:
        ESP_LOGW(TAG, "Unknown OTA progress %d", (int)message->in.progress);
        break;
    }
    if (err != ESP_OK) {
        download_cleanup();
    }
    message->out.result = (err == ESP_OK) ? EZB_ZCL_STATUS_SUCCESS : EZB_ZCL_STATUS_ABORT;
}

void ota_cluster_query_next_image_rsp(ezb_zcl_ota_upgrade_query_next_image_rsp_message_t *message)
{
    const ezb_zcl_ota_upgrade_new_image_t *img = &message->in.image;
    ESP_LOGI(TAG, "Query Next Image Response: status 0x%02x, 0x%04x/0x%04x version 0x%08" PRIx32 ", %" PRIu32
             " bytes", img->status, img->manuf_code, img->image_type, img->file_version, img->size);
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
}

void ota_cluster_confirm_image(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;     // not a fresh OTA image (e.g. flashed via USB)
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "New firmware in %s confirmed (version 0x%08" PRIx32 ")", running->label,
                 s_current_file_version);
    } else {
        ESP_LOGE(TAG, "Confirming the firmware failed: %s", esp_err_to_name(err));
    }
}
