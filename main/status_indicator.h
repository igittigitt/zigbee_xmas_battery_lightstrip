#ifndef STATUS_INDICATOR_H
#define STATUS_INDICATOR_H

#include <stdbool.h>
#include "light_config.h"

/*
 * Connection status shown on the light chain (no on-board LED, goal Z10).
 * All functions are non-blocking (light module queue). Called from the
 * Zigbee task.
 */

/** Brightness of all indications: ~30 % (goals Z10, Z12). */
#define IND_LEVEL                   (LIGHT_LEVEL_MAX * 30 / 100)

/**
 * Z12: searching the stored network after power-up: short flash every second.
 * 50 ms = LIGHT_BLINK_RAMP_MS, the shortest allowed "on" phase.
 */
#define IND_SEARCH_ON_MS            50
#define IND_SEARCH_OFF_MS           950

/**
 * Z12: stop the search indication after this time without connection, light
 * off until joined (own choice to protect the battery; rejoin continues).
 */
#define IND_SEARCH_TIMEOUT_MS       (3 * 60 * 1000)

/** Z10: pairing mode: fast blinking at approx. 2 Hz. */
#define IND_PAIRING_ON_MS           250
#define IND_PAIRING_OFF_MS          250

/**
 * Z10/Z12: connected (rejoined or paired): light steady for this time, then
 * the StartUpOnOff state (Z6; off if no state is stored yet).
 */
#define IND_CONNECTED_ON_MS         2000

/**
 * @brief Start the search indication (stored network, rejoin after power-up, Z12).
 *
 * Light off, short flash every second until status_indicator_connected() or
 * status_indicator_stop(), at most IND_SEARCH_TIMEOUT_MS
 * (checked by status_indicator_check_timeout()).
 */
void status_indicator_searching(void);

/** @brief Start the pairing indication (no network, Z10). Replaces the search indication. */
void status_indicator_pairing(void);

/**
 * @brief Connected: light steady for IND_CONNECTED_ON_MS, then the light state.
 *
 * Only has an effect while an indication is active (not after a silent
 * rejoin during operation, e.g. after parent loss).
 *
 * @param on Light state afterwards (StartUpOnOff result, goal Z6)
 */
void status_indicator_connected(bool on);

/**
 * @brief End the indication without confirmation and show the light state.
 *
 * Used when pairing times out (Z10: light off) or the search indication
 * times out (Z12). No effect if no indication is active.
 *
 * @param on Light state afterwards
 */
void status_indicator_stop(bool on);

/**
 * @brief End the search indication after IND_SEARCH_TIMEOUT_MS, light off.
 *
 * The light stays off until the next successful join; only then
 * status_indicator_connected() applies the StartUpOnOff state (Z12).
 * Call on every failed rejoin attempt; no timer of its own (no extra wake-ups).
 */
void status_indicator_check_timeout(void);

#endif // STATUS_INDICATOR_H
