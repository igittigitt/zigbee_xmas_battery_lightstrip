#ifndef ON_OFF_CLUSTER_H
#define ON_OFF_CLUSTER_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_zigbee.h"

/*
 * The On/Off cluster itself is part of the ZHA On/Off Light template
 * (cluster_manager.c); this module extends it and connects it to the light
 * module.
 */

/**
 * @brief Extend the On/Off cluster of the endpoint (before registration).
 *
 * Adds the attribute StartUpOnOff (0x4003, default ZB_START_UP_ON_OFF_DEFAULT,
 * goal Z6) and marks OnOff and StartUpOnOff as persistent
 * (EZB_ZCL_ATTR_ACCESS_PERSISTENT), so both survive a restart.
 *
 * @param ep_desc Endpoint descriptor containing the On/Off server cluster
 * @return ESP_OK on success
 */
esp_err_t on_off_cluster_add(ezb_af_ep_desc_t ep_desc);

/**
 * @brief Determine the light state after power-up from StartUpOnOff (goal Z6).
 *
 * Zigbee task only. Call once after the stack has loaded its persistent data
 * (EZB_ZDO_SIGNAL_SKIP_STARTUP). Evaluates StartUpOnOff (off / on / toggle /
 * previous) and stores the result as current state, without switching the
 * light: that is done by the status indication after connecting (Z10/Z12).
 * Only reads attributes; if the state differs from the stored OnOff,
 * on_off_cluster_sync_attribute() writes it after joining.
 *
 * @return Light state after power-up (false if the attributes cannot be read)
 */
bool on_off_cluster_load_startup(void);

/**
 * @brief Write the OnOff attribute after joining, if StartUpOnOff changed the
 *        state at power-up. Zigbee task only; no-op otherwise.
 */
void on_off_cluster_sync_attribute(void);

/**
 * @brief Handle a changed On/Off cluster attribute (from Zigbee).
 *
 * Called by the attribute handler in the Zigbee task when the coordinator
 * switches the light (On, Off, Toggle ... commands set the OnOff attribute)
 * or writes StartUpOnOff. Non-blocking: the light ramp runs in the light
 * task (goal Z3).
 *
 * @param attribute Changed attribute
 */
void on_off_cluster_attribute_changed(const ezb_zcl_attribute_t *attribute);

/**
 * @brief Switch the light locally and update the OnOff attribute.
 *
 * Intended for the local button (goal Z5). The new state is reported to the
 * coordinator according to its reporting configuration (goal Z2).
 * Thread-safe: acquires the Zigbee lock. Do not call from the Zigbee task.
 *
 * @param on true = on, false = off
 * @return ESP_OK, or ESP_FAIL if the stack rejected the value
 */
esp_err_t on_off_cluster_set_state(bool on);

/**
 * @brief Last state set via Zigbee or on_off_cluster_set_state().
 */
bool on_off_cluster_get_state(void);

#endif // ON_OFF_CLUSTER_H
