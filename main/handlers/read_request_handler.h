#ifndef READ_REQUEST_HANDLER_H
#define READ_REQUEST_HANDLER_H

#include "esp_err.h"

/**
 * @brief Hook incoming ZCL Read Attributes requests (e.g. "Refresh" in Z2M / HA).
 *
 * Registers an APSDE-DATA.indication callback
 * (ezb_apsde_data_indication_handler_register(), ezbee/aps.h) that sees every
 * incoming frame before the stack processes it. For a Read Attributes request
 * to ZB_ENDPOINT it
 * - logs the cluster and each requested attribute,
 * - measures the battery again if a measured value is requested
 *   (Power Configuration 0x0020/0x0021, 0xFC00/0x0002),
 * - recomputes the remaining time if it is requested (0xFC00/0x0000, 0x0001),
 * and then hands the frame back to the stack, which answers with the fresh
 * attribute values. Other attributes are answered with the stored value.
 *
 * Call in the Zigbee task after the device is registered.
 *
 * @return ESP_OK
 */
esp_err_t read_request_handler_init(void);

#endif // READ_REQUEST_HANDLER_H
