// Zigbee2MQTT external converter for the zigbee-light chain (ZL-LightChain).
//
// Installation: docs/zigbee.md, section "Z2M External Converter".
// Format per https://www.zigbee2mqtt.io/advanced/more/external_converters.html
// modernExtend functions per zigbee-herdsman-converters src/lib/modernExtend.ts (master).
//
// Exposes: light (on/off, brightness, power-on behaviour), identify, battery
// (% and voltage, goal Z7) and the remaining time of the consumption model
// (goal Z14) from the manufacturer-specific cluster 0xFC00.

import {battery, deviceAddCustomCluster, identify, light, numeric} from 'zigbee-herdsman-converters/lib/modernExtend';

// ZCL data type "unsigned 16-bit integer" (ZCL spec; esp-zigbee-lib zcl_type.h EZB_ZCL_ATTR_TYPE_UINT16)
const ZCL_UINT16 = 0x21;

// Must match main/clusters/battery_runtime_cluster.h
const RUNTIME_CLUSTER = 'zlBatteryRuntime';

export default {
    zigbeeModel: ['ZL-LightChain'],
    model: 'ZL-LightChain',
    vendor: 'zigbee-light',
    description: 'Battery powered LED light chain (ESP32-H2, sleepy end device)',
    extend: [
        deviceAddCustomCluster(RUNTIME_CLUSTER, {
            name: RUNTIME_CLUSTER,
            ID: 0xfc00,
            attributes: {
                remainingOnTime: {name: 'remainingOnTime', ID: 0x0000, type: ZCL_UINT16},
                remainingDays: {name: 'remainingDays', ID: 0x0001, type: ZCL_UINT16},
            },
            commands: {},
            commandsResponse: {},
        }),
        light({powerOnBehavior: true, configureReporting: true}),
        identify({isSleepy: true}),
        // Reporting thresholds: the battery() defaults are {min: '1_HOUR', max: 'MAX', change: 10}
        // (modernExtend.ts). For batteryVoltage (unit 100 mV) change 10 means 1.0 V, i.e. reports
        // practically only every 65000 s. Own choice: smallest ZCL step (100 mV / 1 %), at most
        // hourly (the device measures every 15 min), at least every 4 hours.
        battery({
            voltage: true,
            voltageReporting: true,
            voltageReportingConfig: {min: '1_HOUR', max: '4_HOURS', change: 1},
            percentageReportingConfig: {min: '1_HOUR', max: '4_HOURS', change: 2},
        }),
        numeric({
            name: 'remaining_light_hours',
            label: 'Restlaufzeit Licht an',
            cluster: RUNTIME_CLUSTER,
            attribute: 'remainingOnTime',
            description: 'Remaining light-on time at the current brightness (consumption model, estimate)',
            unit: 'h',
            scale: 10,
            precision: 1,
            access: 'STATE_GET',
            reporting: {min: '1_HOUR', max: 'MAX', change: 1},
            entityCategory: 'diagnostic',
        }),
        numeric({
            name: 'remaining_days',
            label: 'Restlaufzeit',
            cluster: RUNTIME_CLUSTER,
            attribute: 'remainingDays',
            // 6553.5 = unknown (no full day of usage data yet), see docs/known-issues.md
            description: 'Remaining days at the average daily use of the last 7 days (estimate, 6553.5 = unknown)',
            unit: 'd',
            scale: 10,
            precision: 1,
            access: 'STATE_GET',
            reporting: {min: '1_HOUR', max: 'MAX', change: 1},
            entityCategory: 'diagnostic',
        }),
    ],
};
