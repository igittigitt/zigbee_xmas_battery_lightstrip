#ifndef RADIO_TRACE_H
#define RADIO_TRACE_H

#include "esp_err.h"

/**
 * @brief DEBUG radio trace (Kconfig ZL_DEBUG_RADIO_TRACE): logs the 802.15.4
 * frames that matter for join and rejoin, with LQI and RSSI from the driver.
 *
 * Logged: received beacons (MAC + Zigbee beacon info), sent beacon requests
 * (summarised per scan), association request/response (= the parent the stack
 * chose and its answer), unicast NWK command frames (rejoin, end device
 * timeout, leave ...) and the TX result of the sent frames.
 *
 * Mechanism: the linker wraps the 802.15.4 driver callbacks
 * esp_ieee802154_receive_done / _transmit_done / _transmit_failed
 * (-Wl,--wrap in main/CMakeLists.txt). The wrapper copies interesting frames
 * into a queue and calls the original callback of the Zigbee library; a
 * low-priority task decodes and logs them. Without the Kconfig option this
 * module compiles to an empty init function.
 *
 * Call once before the Zigbee stack starts.
 *
 * @return ESP_OK, or ESP_ERR_NO_MEM if queue/task could not be created.
 */
esp_err_t radio_trace_init(void);

#endif // RADIO_TRACE_H
