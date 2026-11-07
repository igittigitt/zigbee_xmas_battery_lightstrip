#ifndef IDENTIFY_CLUSTER_H
#define IDENTIFY_CLUSTER_H

#include "esp_zigbee.h"

/*
 * The Identify cluster itself is part of the ZHA On/Off Light template
 * (cluster_manager.c); this module makes identification visible (goal Z11):
 * the light chain blinks at ZB_IDENTIFY_BLINK_HZ for ZB_IDENTIFY_DURATION_MS.
 */

/**
 * @brief Handle a changed Identify cluster attribute (Zigbee task).
 *
 * An Identify command sets the attribute IdentifyTime (0x0000) to the
 * requested time. Blinking starts when IdentifyTime becomes > 0 or rises;
 * the stack's countdown (falling values) does not restart it.
 *
 * @param attribute Changed attribute
 */
void identify_cluster_attribute_changed(const ezb_zcl_attribute_t *attribute);

/**
 * @brief Handle an Identify TriggerEffect command (Zigbee task).
 *
 * Every effect is shown as the same blinking (no effect-specific patterns).
 *
 * @param message TriggerEffect message from the stack
 */
void identify_cluster_trigger_effect(ezb_zcl_identify_effect_message_t *message);

#endif // IDENTIFY_CLUSTER_H
