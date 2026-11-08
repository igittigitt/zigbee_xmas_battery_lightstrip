#ifndef LEVEL_CLUSTER_H
#define LEVEL_CLUSTER_H

#include "esp_err.h"
#include "esp_zigbee.h"

/*
 * The Level Control cluster itself is part of the ZHA Dimmable Light template
 * (cluster_manager.c). The stack handles the Level Control commands
 * (MoveToLevel, Move, Step, Stop and their WithOnOff variants) and reports
 * every CurrentLevel change via the SET_ATTR_VALUE action, as in the
 * esp-zigbee-sdk 2.0.5 example color_dimmable_light. This module maps
 * CurrentLevel to the "on" brightness of the light module.
 */

/** ZCL CurrentLevel range for lighting (0xFF is not a valid level). */
#define LEVEL_ZCL_MAX       0xFE

/**
 * @brief Extend the Level Control cluster of the endpoint (before registration).
 *
 * Adds StartUpCurrentLevel (0x4000, default ZB_START_UP_CURRENT_LEVEL_DEFAULT)
 * and marks CurrentLevel and StartUpCurrentLevel as persistent.
 *
 * @param ep_desc Endpoint descriptor containing the Level Control server cluster
 * @return ESP_OK on success
 */
esp_err_t level_cluster_add(ezb_af_ep_desc_t ep_desc);

/**
 * @brief Determine the brightness after power-up from StartUpCurrentLevel.
 *
 * Zigbee task only. Call once after the stack has loaded its persistent data
 * (EZB_ZDO_SIGNAL_SKIP_STARTUP), before the status indication ends. Sets the
 * "on" brightness of the light module without switching the light. If the
 * result differs from the stored CurrentLevel, level_cluster_sync_attribute()
 * writes it after joining.
 */
void level_cluster_load_startup(void);

/**
 * @brief Write CurrentLevel after joining, if StartUpCurrentLevel changed it
 *        at power-up. Zigbee task only; no-op otherwise.
 */
void level_cluster_sync_attribute(void);

/**
 * @brief Handle a changed Level Control attribute (from Zigbee).
 *
 * Called by the attribute handler in the Zigbee task. CurrentLevel becomes
 * the "on" brightness (soft ramp, goal Z3); if the light is off, it is only
 * stored. Non-blocking.
 *
 * @param attribute Changed attribute
 */
void level_cluster_attribute_changed(const ezb_zcl_attribute_t *attribute);

#endif // LEVEL_CLUSTER_H
