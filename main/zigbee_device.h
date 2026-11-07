#ifndef ZIGBEE_DEVICE_H
#define ZIGBEE_DEVICE_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Zigbee task: initialises and starts the stack, then runs its main loop.
 *
 * Create with xTaskCreate(zigbee_device_task, ..., ZB_TASK_STACK_SIZE, NULL,
 * ZB_TASK_PRIORITY, NULL) after NVS and power management are initialised.
 * Does not return while the stack is running.
 *
 * Commissioning behaviour:
 * - No stored network: pairing window (network steering) for
 *   ZB_PAIRING_TIMEOUT_MS, then the device stops searching (goal Z10).
 * - Stored network (reboot) or parent lost: rejoin with exponential backoff
 *   between ZB_REJOIN_RETRY_MIN_MS and ZB_REJOIN_RETRY_MAX_MS.
 *
 * @param arg unused
 */
void zigbee_device_task(void *arg);

/**
 * @brief Open a new pairing window (network steering for ZB_PAIRING_TIMEOUT_MS).
 *
 * Only has an effect if the device is not on a network. Intended for the local
 * button (goal Z5) after a timed-out pairing window.
 * Thread-safe: acquires the Zigbee lock.
 *
 * @return ESP_OK if steering was started, ESP_ERR_INVALID_STATE if the device
 *         is already on a network or the stack is not running.
 */
esp_err_t zigbee_device_start_pairing(void);

/**
 * @brief Factory reset: leave the network, erase the Zigbee data and restart.
 *
 * Does not return. Thread-safe: acquires the Zigbee lock.
 */
void zigbee_device_factory_reset(void);

/**
 * @brief Whether the device is currently on a Zigbee network.
 *
 * Thread-safe: acquires the Zigbee lock.
 */
bool zigbee_device_is_joined(void);

#endif // ZIGBEE_DEVICE_H
