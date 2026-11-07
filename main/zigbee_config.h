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

/**
 * TEMPORARY (2026-11-07, docs/known-issues.md 1c): 1 = no own DEBUG awake PM
 * lock (as in the first working firmware), to test one change at a time.
 */
#define ZB_TEST_NO_AWAKE_LOCK           1

/* ---------------------------------------------------------------------------
 * Device identity
 * ------------------------------------------------------------------------- */

/** Endpoint of the On/Off light (device ID 0x0100, ezb_zha_create_on_off_light). */
#define ZB_ENDPOINT                     1

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

/**
 * Scan duration per channel (ezb_bdb_set_scan_duration): ((1 << n) + 1) * 15.36 ms.
 * 5 = ~507 ms. Stack default is logged at startup ("default scan duration").
 * Longer scans to catch beacons reliably (rejoin found 0 neighbors, known-issues 1c).
 */
#define ZB_SCAN_DURATION                5

/** Pairing window (network steering) before giving up, goal Z10: 3 min. */
#define ZB_PAIRING_TIMEOUT_MS           (3 * 60 * 1000)

/** Pause between two network steering attempts within the pairing window. */
#define ZB_STEERING_RETRY_MS            1000

/**
 * Rejoin of a known network (after reboot or parent loss): retry with
 * exponential backoff from MIN to MAX, without time limit. The values are
 * our own choice (trade-off reaction time vs. battery), not from a source.
 */
#define ZB_REJOIN_RETRY_MIN_MS          1000
#define ZB_REJOIN_RETRY_MAX_MS          (5 * 60 * 1000)

/* ---------------------------------------------------------------------------
 * Platform
 * ------------------------------------------------------------------------- */

/** NVS partition holding the Zigbee datasets (default since 2.0.4: "nvs"). */
#define ZB_STORAGE_PARTITION_NAME       "nvs"

/** Zigbee task, values from the esp-zigbee-sdk 2.0.5 examples. */
#define ZB_TASK_STACK_SIZE              4096
#define ZB_TASK_PRIORITY                5

#endif // ZIGBEE_CONFIG_H
