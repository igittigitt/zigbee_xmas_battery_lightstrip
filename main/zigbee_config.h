#ifndef ZIGBEE_CONFIG_H
#define ZIGBEE_CONFIG_H

/*
 * Central Zigbee configuration (esp-zigbee-lib 2.0.5).
 * Sources and rationale: docs/zigbee-framework.md, docs/zigbee.md.
 *
 * Changing the endpoint, device ID, cluster list, manufacturer or model
 * requires a re-interview of the device in Zigbee2MQTT.
 */

#include "esp_zigbee.h"

/* ---------------------------------------------------------------------------
 * Device identity
 * ------------------------------------------------------------------------- */

/** Endpoint of the dimmable light (device ID 0x0101, ezb_zha_create_dimmable_light). */
#define ZB_ENDPOINT                     1

/**
 * Destination of the explicit OnOff report after a local switch (button, Z5):
 * the coordinator always has the short address 0x0000; zigbee-herdsman
 * (Zigbee2MQTT) uses endpoint 1 as its HA endpoint (ember adapter/endpoints.ts
 * FIXED_ENDPOINTS "primary network" endpoint 1, z-stack adapter/endpoints.ts
 * first entry endpoint 1 profile 0x0104, deconz fallback [1]).
 */
#define ZB_COORDINATOR_SHORT_ADDR       0x0000
#define ZB_COORDINATOR_ENDPOINT         1

/**
 * Basic cluster ManufacturerName / ModelIdentifier (plain C strings, max. 32
 * characters, converted to ZCL strings at runtime). Zigbee2MQTT identifies the
 * device by these values (external converter).
 */
#define ZB_MANUFACTURER_NAME            "zigbee-light"
#define ZB_MODEL_IDENTIFIER             "ZL-LightChain"

/**
 * Identify (goal Z11): blink the light chain at this rate for this duration
 * when an Identify or TriggerEffect command arrives. The IdentifyTime
 * requested by the coordinator is not used, the duration is fixed.
 */
#define ZB_IDENTIFY_BLINK_HZ            5
#define ZB_IDENTIFY_DURATION_MS         2000

/**
 * Default of the On/Off attribute StartUpOnOff (0x4003), goal Z6: behaviour
 * after power-up / battery change. 0x00 = off (EZB_ZCL_ON_OFF_START_UP_ON_OFF_*,
 * ezbee/zcl/cluster/on_off_desc.h). Changeable from Zigbee2MQTT
 * ("power_on_behavior"), stored persistently by the stack.
 */
#define ZB_START_UP_ON_OFF_DEFAULT      EZB_ZCL_ON_OFF_START_UP_ON_OFF_SET_ON_OFF_TO_OFF

/**
 * Dimming (Level Control cluster 0x0008, Dimmable Light 0x0101), added on
 * 2026-11-08 at the user's request.
 *
 * ZB_LEVEL_DEFAULT: CurrentLevel before any brightness was ever set
 * (ZCL range 0x00..0xFE; 0xFE = full brightness). Own choice.
 *
 * ZB_START_UP_CURRENT_LEVEL_DEFAULT: default of StartUpCurrentLevel (0x4000):
 * 0x00 = minimum, 0x01..0xFE = this level, 0xFF = previous level. Own choice:
 * previous (analogous to the expectation "brightness survives a battery
 * change"). Changeable from Zigbee2MQTT, stored persistently.
 */
#define ZB_LEVEL_DEFAULT                    0xFE
#define ZB_START_UP_CURRENT_LEVEL_DEFAULT   0xFF

/* ---------------------------------------------------------------------------
 * Network role and end device timing
 * ------------------------------------------------------------------------- */

/** Join without install code (standard for Zigbee2MQTT). */
#define ZB_INSTALLCODE_POLICY           false

/**
 * End device timeout: the parent ages the child out after this time without
 * keepalive; the ZED raises EZB_ZDO_SIGNAL_PARENT_LOST after keepalive failed
 * for this duration (ezbee/nwk.h, ezbee/app_signals.h).
 */
#define ZB_ED_TIMEOUT                   EZB_NWK_ED_TIMEOUT_64MIN

/**
 * Keepalive interval in ms. Determines how long the chip can stay in light
 * sleep (esp-zigbee-sdk 2.0.5 docs/en/faq.rst) and thus the reaction time to
 * commands from Home Assistant (goal Z4: <= approx. 1-2 s). Shorter = more
 * wake-ups = more current. Whether every keepalive is a MAC Data Request
 * (= poll for buffered commands) is not documented [UNBELEGT] - verify by
 * measurement (docs/power-measurement.md).
 */
#define ZB_KEEP_ALIVE_MS                2000

/** Do not keep the receiver on while idle (Sleepy End Device, faq.rst). */
#define ZB_RX_ON_WHEN_IDLE              false

/**
 * Minimum LQI of a beacon for the parent to be considered during join
 * (ezb_nwk_set_min_join_lqi, ezbee/nwk.h). The 2.0.x default is reported to be
 * 75 and 32 in 1.x - user comments only, not confirmed by Espressif, see
 * https://github.com/espressif/esp-zigbee-sdk/issues/840 (same symptoms as
 * known-issues 1c: 0x2c3 NOT_PERMITTED despite permit-open beacons). The
 * actual default is logged at startup: 75 with 2.0.5 (log 2026-11-08). With
 * 75 every candidate failed (parent LQI after join 37-44); 32 = value that
 * fixed the join there and here (known-issues 1c).
 */
#define ZB_MIN_JOIN_LQI                 32

/* ---------------------------------------------------------------------------
 * Commissioning
 * ------------------------------------------------------------------------- */

/**
 * Channels for join / rejoin (ezbee/bdb.h; valid 0x00000800..0x07FFF800).
 * Primary: only the channel of the own Zigbee2MQTT network (channel 11, seen
 * in the join log 2026-11-07); a single channel reduces the scan effort
 * (esp-zigbee-sdk faq.rst). Secondary: all channels 11-26, used by BDB when
 * the primary set fails (e.g. after a channel change of the coordinator).
 */
#define ZB_NETWORK_CHANNEL              11
#define ZB_PRIMARY_CHANNEL_MASK         (1UL << ZB_NETWORK_CHANNEL)
#define ZB_SECONDARY_CHANNEL_MASK       0x07FFF800UL

/** Pairing window (network steering) before giving up, goal Z10: 3 min. */
#define ZB_PAIRING_TIMEOUT_MS           (3 * 60 * 1000)

/** Pause between two network steering attempts within the pairing window. */
#define ZB_STEERING_RETRY_MS            1000

/**
 * Rejoin of a known network (after reboot or parent loss).
 *
 * esp-zigbee-lib 2.0.5 typically needs several BDB initialization attempts
 * after a reboot (~5-6 s each): the unmodified Espressif example
 * light_sleep_end_device (1 s retry) rejoined at the 5th attempt after ~25 s
 * on 2026-11-07, see docs/known-issues.md 1c and esp-zigbee-sdk #894.
 * Therefore: ZB_REJOIN_FAST_ATTEMPTS attempts with ZB_REJOIN_RETRY_MIN_MS pause
 * (as the example), then exponential backoff up to ZB_REJOIN_RETRY_MAX_MS,
 * without time limit. Counts and MAX are our own choice (reaction time vs.
 * battery), not from a source.
 */
#define ZB_REJOIN_FAST_ATTEMPTS         10
#define ZB_REJOIN_RETRY_MIN_MS          1000
#define ZB_REJOIN_RETRY_MAX_MS          (5 * 60 * 1000)

/**
 * DEBUG level 1 with ZL_DEBUG_DIAG_SCAN only: after every failed join / rejoin attempt an
 * active scan on ZB_PRIMARY_CHANNEL_MASK logs every received beacon (sender,
 * PAN, permit join, end device capacity) before the next attempt is scheduled.
 * Duration per channel ((1 << n) + 1) * 15.36 ms (ezbee/nwk.h): 3 = ~138 ms,
 * same order as the BDB default scan (known-issues 1c). Own choice.
 */
#define ZB_DIAG_SCAN_DURATION           3

/* ---------------------------------------------------------------------------
 * Platform
 * ------------------------------------------------------------------------- */

/** NVS partition holding the Zigbee datasets (default since 2.0.4: "nvs"). */
#define ZB_STORAGE_PARTITION_NAME       "nvs"

/** Zigbee task, values from the esp-zigbee-sdk 2.0.5 examples. */
#define ZB_TASK_STACK_SIZE              4096
#define ZB_TASK_PRIORITY                5

#endif // ZIGBEE_CONFIG_H
