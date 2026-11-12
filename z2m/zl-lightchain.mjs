// Zigbee2MQTT external converter for the zigbee-light chain (ZL-LightChain).
//
// Installation: docs/zigbee.md, section "Z2M External Converter".
// Format per https://www.zigbee2mqtt.io/advanced/more/external_converters.html
// modernExtend functions per zigbee-herdsman-converters src/lib/modernExtend.ts (master).
//
// Exposes: light (on/off, brightness, power-on behaviour), identify, battery
// (% and voltage, goal Z7), the remaining time of the consumption model
// (goal Z14) from the manufacturer-specific cluster 0xFC00 and firmware updates
// over Zigbee (OTA, goal Z16; image via ota.zigbee_ota_override_index_location, docs/zigbee.md).

import {battery, deviceAddCustomCluster, enumLookup, identify, light, numeric} from 'zigbee-herdsman-converters/lib/modernExtend';

// ZCL data type "unsigned 16-bit integer" (ZCL spec; esp-zigbee-lib zcl_type.h EZB_ZCL_ATTR_TYPE_UINT16)
const ZCL_UINT16 = 0x21;
// ZCL data types "unsigned 8-bit integer" and "8-bit enumeration" (zcl_type.h EZB_ZCL_ATTR_TYPE_UINT8 / _ENUM8)
const ZCL_UINT8 = 0x20;
const ZCL_ENUM8 = 0x30;

// Must match main/clusters/battery_runtime_cluster.h
const RUNTIME_CLUSTER = 'zlBatteryRuntime';

// fromZigbee for uint16 attributes that use 0xFFFF as "unknown" (the ZCL invalid value for uint16).
// zigbee-herdsman decodes that value as NaN (src/zspec/zcl/utils.ts, ZCL_TYPE_INVALID_BY_TYPE), and
// the default converter of numeric() then throws "Value is not a number, got number (NaN)"
// (assertNumber in src/lib/modernExtend.ts). Publish null instead, HA shows "unknown".
function unknownAsNull(attribute, property, scale = 1) {
    return (model, msg) => {
        if (!(attribute in msg.data)) return;
        const raw = msg.data[attribute];
        if (typeof raw !== 'number' || Number.isNaN(raw)) return {[property]: null};
        return {[property]: raw / scale};
    };
}

export default {
    zigbeeModel: ['ZL-LightChain'],
    model: 'ZL-LightChain',
    vendor: 'zigbee-light',
    description: 'Battery powered LED light chain (ESP32-H2, sleepy end device)',
    // OTA (goal Z16): plain definition field (Definition.ota, zigbee-herdsman-converters src/lib/types.ts);
    // Z2M then offers updates and HA gets an "update" entity. The device has the genOta client cluster.
    ota: true,
    // Device image for every ZL-LightChain without per-device setup. Not in the Definition type, but
    // read by Z2M for external definitions (zigbee2mqtt lib/extension/bridge.ts getDefinitionPayload:
    // device option `icon` first, then definition.icon). The file lies in device_icons/ next to
    // Z2M's configuration.yaml (repo: z2m/device_icons/zl-lightchain.png).
    icon: 'device_icons/zl-lightchain.png',
    extend: [
        deviceAddCustomCluster(RUNTIME_CLUSTER, {
            name: RUNTIME_CLUSTER,
            ID: 0xfc00,
            attributes: {
                remainingOnTime: {name: 'remainingOnTime', ID: 0x0000, type: ZCL_UINT16},
                remainingDays: {name: 'remainingDays', ID: 0x0001, type: ZCL_UINT16},
                batteryVoltageMv: {name: 'batteryVoltageMv', ID: 0x0002, type: ZCL_UINT16},
                // write: true is required: zigbee-herdsman refuses to send a write to an attribute
                // without it (zspec/zcl/utils.ts processAttributeWrite, NOT_AUTHORIZED "is not writable").
                // min/max are checked there too (processRestrictions); same limits as the firmware
                // (battery_config.h BATTERY_CELL_*).
                cellType: {name: 'cellType', ID: 0x0010, type: ZCL_ENUM8, write: true, max: 1},
                cellCapacity: {name: 'cellCapacity', ID: 0x0011, type: ZCL_UINT16, write: true, min: 100, max: 20000},
                cellsParallel: {name: 'cellsParallel', ID: 0x0012, type: ZCL_UINT8, write: true, min: 1, max: 8},
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
            // 0xFFFF = unknown (not measured, e.g. DEBUG build without ADC) -> null
            description: 'Battery voltage, last measurement (with the light on: under load)',
            unit: 'mV',
            fzConvert: unknownAsNull('batteryVoltageMv', 'voltage'),
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
            fzConvert: unknownAsNull('remainingOnTime', 'remaining_light_hours', 10),
            access: 'STATE_GET',
            reporting: {min: '1_HOUR', max: 'MAX', change: 1},
            entityCategory: 'diagnostic',
        }),
        numeric({
            name: 'remaining_days',
            label: 'Restlaufzeit',
            cluster: RUNTIME_CLUSTER,
            attribute: 'remainingDays',
            // 0xFFFF = unknown (no full day of usage data yet) -> null, see unknownAsNull()
            description: 'Remaining days at the average daily use of the last 7 days (estimate, empty = no full day of data yet)',
            unit: 'd',
            scale: 10,
            precision: 1,
            fzConvert: unknownAsNull('remainingDays', 'remaining_days', 10),
            access: 'STATE_GET',
            reporting: {min: '1_HOUR', max: 'MAX', change: 1},
            entityCategory: 'diagnostic',
        }),
        // Cell configuration (writable, stored in the device). The firmware couples type and
        // capacity: selecting the Samsung cell sets 3350 mAh, entering another capacity switches
        // to "Generisch Li-Ion" (main/clusters/battery_runtime_cluster.h); it reports the result.
        // Values must match battery_cell_type_t in main/battery/battery_config.h.
        enumLookup({
            name: 'battery_cell_type',
            label: 'Zelltyp',
            lookup: {'Generisch Li-Ion': 0, 'Samsung INR18650-35E': 1},
            cluster: RUNTIME_CLUSTER,
            attribute: 'cellType',
            description: 'Battery cell type (voltage curve; Samsung INR18650-35E sets 3350 mAh)',
            access: 'ALL',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            entityCategory: 'config',
        }),
        numeric({
            name: 'battery_cell_capacity',
            label: 'Kapazität je Zelle',
            cluster: RUNTIME_CLUSTER,
            attribute: 'cellCapacity',
            description: 'Capacity per cell (another value than the datasheet switches to generic Li-ion)',
            unit: 'mAh',
            valueMin: 100,
            valueMax: 20000,
            valueStep: 50,
            access: 'ALL',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            entityCategory: 'config',
        }),
        numeric({
            name: 'battery_cells_parallel',
            label: 'Zellen parallel',
            cluster: RUNTIME_CLUSTER,
            attribute: 'cellsParallel',
            description: 'Number of cells in parallel (1S)',
            valueMin: 1,
            valueMax: 8,
            valueStep: 1,
            access: 'ALL',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            entityCategory: 'config',
        }),
    ],
};
