#ifndef BUTTON_H
#define BUTTON_H

#include "esp_err.h"
#include "driver/gpio.h"

/*
 * Local button (goal Z5): BOOT button of the board.
 *
 * - short press: toggle the light (reported to the coordinator); ignored
 *   while not connected
 * - long press (>= BUTTON_LONG_PRESS_MS): short flash as "release" signal,
 *   on release factory reset + pairing mode (goal Z10)
 *
 * Active in every state (pairing, rejoin, connected, light sleep): the button
 * wakes the chip from light sleep (GPIO wakeup, low level).
 */

/** BOOT button, low = pressed. ESP32-H2-Zero: R6 10 kOhm pull-up (docs/hardware.md). */
#define BUTTON_GPIO                 GPIO_NUM_9

/** Minimum press duration that counts as a press (debounce). */
#define BUTTON_DEBOUNCE_MS          50

/** Long press: factory reset + pairing (goal Z5). */
#define BUTTON_LONG_PRESS_MS        5000

/** Poll period while the button is held (the chip stays awake meanwhile). */
#define BUTTON_POLL_MS              20

/** Duration of the "release" flash after BUTTON_LONG_PRESS_MS (goal Z5). */
#define BUTTON_RELEASE_FLASH_MS     300

/** Stack size and priority of the button task. */
#define BUTTON_TASK_STACK_SIZE      3072
#define BUTTON_TASK_PRIORITY        4

/**
 * @brief Configure the button GPIO, light sleep wakeup and the button task.
 *
 * Call after light_init() and before the Zigbee task is started. The task
 * blocks until the button is pressed (no periodic wakeups, goal Z9).
 *
 * @return ESP_OK on success
 */
esp_err_t button_init(void);

#endif // BUTTON_H
