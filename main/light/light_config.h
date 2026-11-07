#ifndef LIGHT_CONFIG_H
#define LIGHT_CONFIG_H

#include "driver/gpio.h"

/* ---------------------------------------------------------------------------
 * Common (both backends)
 * ------------------------------------------------------------------------- */

/** Maximum logical light level (level range is 0..LIGHT_LEVEL_MAX). */
#define LIGHT_LEVEL_MAX                 255

/** Level used for "On" (goal Z2). */
#define LIGHT_LEVEL_ON                  LIGHT_LEVEL_MAX

/** Soft-start / soft-stop ramp duration 0 <-> 100 % (goal Z3: approx. 1 s). */
#define LIGHT_RAMP_MS                   1000

/** Step period of the software ramp (used by backends without hardware fade). */
#define LIGHT_SW_RAMP_STEP_MS           20

/**
 * Ramp duration (for 0 <-> 100 %) of each flash edge. Short ramp instead of
 * hard switching, as specified for the pairing indication (goal Z10: ~50 ms).
 */
#define LIGHT_BLINK_RAMP_MS             50

/** Stack size and priority of the light task. */
#define LIGHT_TASK_STACK_SIZE           3072
#define LIGHT_TASK_PRIORITY             4

/* ---------------------------------------------------------------------------
 * Backend WS2812 (DEBUG): on-board LED of the Waveshare ESP32-H2-Zero
 * ------------------------------------------------------------------------- */

/** WS2812 data input. Source: ESP32-H2-Zero schematic (LED1 DI = GPIO 8). */
#define LIGHT_WS2812_GPIO               GPIO_NUM_8

/** RMT tick resolution: 10 MHz -> 0.1 us per tick. */
#define LIGHT_WS2812_RMT_RESOLUTION_HZ  (10 * 1000 * 1000)

/**
 * Bit timings in RMT ticks (0.1 us). Values taken from the ESP-IDF example
 * examples/peripherals/rmt/led_strip (led_strip_encoder.c): T0H 0.3 us,
 * T0L 0.9 us, T1H 0.9 us, T1L 0.3 us. Verified on the prototype only.
 */
#define LIGHT_WS2812_T0H_TICKS          3
#define LIGHT_WS2812_T0L_TICKS          9
#define LIGHT_WS2812_T1H_TICKS          9
#define LIGHT_WS2812_T1L_TICKS          3

/** Timeout waiting for one WS2812 frame to be sent. */
#define LIGHT_WS2812_TX_TIMEOUT_MS      100

/**
 * Byte order of the on-board LED (WS2812B-0807 per ESP32-H2-Zero schematic).
 * 1 = R,G,B / 0 = G,R,B (WS2812B standard). Determined on the prototype
 * (2026-11-07): with G,R,B the warm white target appeared yellow-green, i.e.
 * red and green were swapped -> this LED expects R,G,B.
 */
#define LIGHT_WS2812_ORDER_RGB          1

/**
 * Colour at full level, simulating the single-colour light chain.
 * Scaled linearly with the light level. Warm white; the WS2812 has no gamma
 * correction, so the visible tint may need fine tuning on the prototype.
 */
#define LIGHT_WS2812_COLOR_R            255
#define LIGHT_WS2812_COLOR_G            140
#define LIGHT_WS2812_COLOR_B            40

/* ---------------------------------------------------------------------------
 * Backend PWM (production): external constant current LED driver
 * ------------------------------------------------------------------------- */

/** PWM output to the LED driver. [VORSCHLAG] see docs/hardware.md (GPIO-Belegung). */
#define LIGHT_PWM_GPIO                  GPIO_NUM_10

/** Enable output for boost converter / load switch. [VORSCHLAG] see docs/hardware.md. */
#define LIGHT_PWM_EN_GPIO               GPIO_NUM_11

/**
 * PWM frequency. [OFFEN] Depends on the (not yet known) dimming input of the
 * LED driver, see docs/goals.md "Offene Punkte". 1 kHz is a placeholder.
 */
#define LIGHT_PWM_FREQ_HZ               1000

/**
 * Duty resolution. Clock source is RC_FAST (approx. 8.5 MHz, IDF
 * soc/esp32h2/include/soc/clk_tree_defs.h), required to keep PWM running in
 * light sleep (IDF v5.5.1 LEDC docs, "Power Management").
 * 2^12 * 1 kHz = 4.1 MHz < 8.5 MHz.
 */
#define LIGHT_PWM_DUTY_RESOLUTION       LEDC_TIMER_12_BIT

/**
 * Wait time after enabling the boost converter before the PWM ramp starts.
 * [UNBELEGT] Placeholder until the boost module is known (docs/hardware.md).
 */
#define LIGHT_PWM_EN_SETTLE_MS          10

#endif // LIGHT_CONFIG_H
