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
                batteryVoltageMv: {name: 'batteryVoltageMv', ID: 0x0002, type: ZCL_UINT16},
            },
            commands: {},
            commandsResponse: {},
        }),
        // effect: false - light() adds the "effect" expose by default (genIdentify triggerEffect);
        // the firmware rejects TriggerEffect, effects are out of scope (docs/goals.md)
        light({effect: false, powerOnBehavior: true, configureReporting: true}),
        identify({isSleepy: true}),
        // Voltage in mV from 0xFC00/0x0002. MUST stand before battery(): Z2M picks the FIRST
        // toZigbee converter whose key matches (publish.ts, `converters.find`), extends are
        // appended in array order (index.ts processExtensions). battery()'s converter has the key
        // ["battery", "voltage"] and would otherwise answer a "voltage" refresh by reading
        // genPowerCfg.batteryVoltage, whose value is not published (voltage: false).
        numeric({
            name: 'voltage',
            label: 'Voltage',
            cluster: RUNTIME_CLUSTER,
            attribute: 'batteryVoltageMv',
            // 65535 = unknown (not measured, e.g. DEBUG build without ADC)
            description: 'Battery voltage, last measurement (with the light on: under load)',
            unit: 'mV',
            access: 'STATE_GET',
            // change 10 = 10 mV; same timing as battery %
            reporting: {min: '1_HOUR', max: '4_HOURS', change: 10},
            entityCategory: 'diagnostic',
        }),
        // Battery %: reporting at least every 4 hours, at most hourly (the device measures every
        // 15 min), smallest ZCL step 1 % (change 2, unit 0.5 %).
        // Voltage: the standard genPowerCfg.batteryVoltage has only 100 mV steps, so battery()
        // does not expose it (voltage: false publishes no `voltage` key, modernExtend.ts). The
        // exact value comes from the own attribute 0xFC00/0x0002 above.
        battery({
            voltage: false,
            voltageReporting: false,
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
