#ifndef OTA_CLUSTER_H
#define OTA_CLUSTER_H

#include "esp_err.h"
#include "esp_zigbee.h"

/*
 * OTA Upgrade client (cluster 0x0019, goal Z16): firmware update over Zigbee
 * from Zigbee2MQTT, started with "Install" in Home Assistant.
 *
 * Flow: Z2M sends Image Notify -> the stack answers with Query Next Image
 * Request and downloads the image block by block; this module parses the
 * Zigbee OTA file (header + sub-elements, ezbee/zcl/cluster/ota_file.h) and
 * writes the upgrade image sub-element (tag 0x0000) into the next OTA slot
 * with esp_ota_*. After the transfer it switches the boot partition and
 * restarts. The new image confirms itself after the network join
 * (ota_cluster_confirm_image), otherwise the bootloader rolls back on the next
 * reset (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
 *
 * The device never polls the server by itself (no periodic wake-up, decision
 * 2026-11-11). Constants in zigbee_config.h (ZB_OTA_*).
 */

/**
 * @brief Add the OTA Upgrade client cluster to an endpoint.
 *
 * ManufacturerID / ImageTypeID / CurrentFileVersion from ZB_OTA_*. Being a
 * client (output) cluster is what makes Zigbee2MQTT accept OTA for the device.
 *
 * @param ep_desc Endpoint descriptor (before registration)
 * @return ESP_OK or an error from the stack
 */
esp_err_t ota_cluster_add(ezb_af_ep_desc_t ep_desc);

/**
 * @brief Configure the OTA client after the device was registered.
 *
 * Sets the maximum block size (ZB_OTA_BLOCK_SIZE).
 *
 * @return ESP_OK or an error from the stack
 */
esp_err_t ota_cluster_init(void);

/**
 * @brief Handle an OTA client progress message (Zigbee task).
 *
 * START/RECEIVING/CHECK/APPLY/FINISH/ABORT, sets message->out.result.
 * The download is refused (ABORT) if the last measured battery voltage is
 * below BATTERY_WARN_MV.
 *
 * @param message Progress message from the stack (EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID)
 */
void ota_cluster_progress(ezb_zcl_ota_upgrade_client_progress_message_t *message);

/**
 * @brief Handle a Query Next Image Response (Zigbee task, log only).
 *
 * @param message Response message from the stack (EZB_ZCL_CORE_OTA_UPGRADE_QUERY_NEXT_IMAGE_RSP_CB_ID)
 */
void ota_cluster_query_next_image_rsp(ezb_zcl_ota_upgrade_query_next_image_rsp_message_t *message);

/**
 * @brief Confirm the running image after a successful network join.
 *
 * If the running image was just installed by OTA (state PENDING_VERIFY), mark
 * it valid and cancel the rollback. Does nothing otherwise (e.g. USB flash).
 */
void ota_cluster_confirm_image(void);

#endif // OTA_CLUSTER_H
