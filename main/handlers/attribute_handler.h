#ifndef ATTRIBUTE_HANDLER_H
#define ATTRIBUTE_HANDLER_H

#include "esp_zigbee.h"

/**
 * @brief ZCL core action callback (register with ezb_zcl_core_action_handler_register()).
 *
 * Routes incoming ZCL actions, e.g. attribute changes from the coordinator,
 * to the cluster modules. Runs in the Zigbee task and must not block.
 *
 * @param callback_id Type of the action, see ezb_zcl_core_action_callback_id_t
 * @param message     Action specific message, type depends on callback_id
 */
void attribute_handler_callback(ezb_zcl_core_action_callback_id_t callback_id, void *message);

#endif // ATTRIBUTE_HANDLER_H
