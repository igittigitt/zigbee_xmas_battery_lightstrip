#ifndef BATTERY_CONFIG_H
#define BATTERY_CONFIG_H

/*
 * Battery voltage measurement (goal Z7). Circuit and rationale:
 * docs/hardware.md, section "Akkumessung".
 */

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

/**
 * ADC input: battery voltage via divider. GPIO 1 = ADC1_CH0 (ESP32-H2
 * datasheet, pin table; IDF soc/esp32h2/include/soc/adc_channel.h).
 * Not a strapping pin, input disabled and floating after reset (datasheet,
 * IO MUX table, reset value 0). [VORSCHLAG] see docs/hardware.md.
 */
#define BATTERY_ADC_GPIO                GPIO_NUM_1

/**
 * Voltage divider battery+ -> R_TOP -> ADC pin -> R_BOTTOM -> GND, values in
 * kOhm (docs/hardware.md: 1 MOhm / 1 MOhm, 100 nF at the ADC pin).
 * V_battery = V_adc * (R_TOP + R_BOTTOM) / R_BOTTOM.
 */
#define BATTERY_DIVIDER_R_TOP_KOHM      1000
#define BATTERY_DIVIDER_R_BOTTOM_KOHM   1000

/**
 * Per-device gain correction in 1/1000 (1000 = none). Compensates divider
 * tolerance, ADC gain error and pad leakage together, see docs/hardware.md
 * "Kalibrierung": feed the divider from a known voltage (PPK2 source meter,
 * 3.70 V; board on USB, common GND), read the logged battery voltage and set
 * CAL_new = CAL_old * V_known / V_logged.
 * Prototype 2026-11-08 (docs/hardware.md "Kalibrierung"): the ADC read 1.32-1.38 %
 * high against a multimeter at the 5V pin at 2.89 / 3.17 / 3.38 / 4.07 V; the factor
 * is constant over the range: 1000 * 2890/2928 = 987, 1000 * 3170/3212 = 987,
 * 1000 * 3380/3426 = 987,
 * 1000 * 4070/4126 = 986.
 */
#define BATTERY_CAL_PERMILLE            987

/**
 * Attenuation. With the 1:1 divider a full cell (4.2 V) gives 2.1 V at the
 * pin, which needs ATTEN3 = ADC_ATTEN_DB_12 (0-3300 mV, total error +-23 mV
 * after calibration; ESP32-H2 datasheet, Tab. 4-6).
 */
#define BATTERY_ADC_ATTEN               ADC_ATTEN_DB_12

/**
 * Samples averaged per measurement. Own choice (docs/hardware.md: "Mittelung
 * über mehrere Samples"); a measurement is accepted if at least half of the
 * samples were read successfully.
 */
#define BATTERY_ADC_SAMPLES             16

/**
 * Measurement interval. docs/hardware.md proposes 10-30 min; 15 min is our
 * own choice within that range. Every measurement wakes the chip once.
 */
#define BATTERY_MEASURE_INTERVAL_MS     (15 * 60 * 1000)

/**
 * Smoothing of the reported state of charge: it only falls; a rise is
 * accepted only if it is at least this many percent (battery charged or
 * replaced). Suppresses flapping between two values caused by ADC noise.
 * Own choice, agreed with the user on 2026-11-08.
 */
#define BATTERY_PERCENT_RISE_MIN        5

/* ---- Cell type (selectable in Home Assistant, cluster 0xFC00) ----
 *
 * Each cell type has its own voltage tables (rest and load). Only cell types
 * with real data belong here. Values of the enum are stored in NVS and sent
 * over Zigbee (attribute CellType): never renumber, only append.
 */
typedef enum {
    BATTERY_CELL_GENERIC_LIION = 0,     /**< Any Li-ion cell, 4.2 V end-of-charge, capacity entered by the user */
    BATTERY_CELL_SAMSUNG_35E = 1,       /**< Samsung INR18650-35E */
    BATTERY_CELL_TYPE_COUNT
} battery_cell_type_t;

/** Cell type, capacity and parallel cells without stored setting (the prototype). */
#define BATTERY_CELL_DEFAULT_TYPE       BATTERY_CELL_SAMSUNG_35E
#define BATTERY_CELLS_PARALLEL_DEFAULT  1

/**
 * Capacity of the Samsung INR18650-35E in mAh: "Standard Discharge Capacity
 * 3,350mAh" (minimum; 0.2C = 680 mA to 2.65 V at 23 °C, datasheet
 * datasheets/INR18650-35E.pdf Ver. 1.1, 3.1 / 7.2).
 */
#define BATTERY_CAPACITY_35E_MAH        3350

/** Allowed range of the capacity per cell (CellCapacity attribute), own choice. */
#define BATTERY_CELL_CAPACITY_MIN_MAH   100
#define BATTERY_CELL_CAPACITY_MAX_MAH   20000

/** Allowed range of the number of parallel cells (CellsParallel attribute), own choice. */
#define BATTERY_CELLS_PARALLEL_MIN      1
#define BATTERY_CELLS_PARALLEL_MAX      8

/*
 * Voltage -> state of charge per cell type, piecewise linear, ascending by mV;
 * more rows can simply be added. OCV = at rest (light off), LOAD = light on at
 * full brightness (LIGHT_LEVEL_MAX). Parallel cells of one type share the
 * voltage, the tables apply per cell.
 *
 * Generic Li-ion: [UNBELEGT] linear between the emergency shutdown threshold
 * (BATTERY_SHUTDOWN_MV = 3000 mV, goal Z8: 0 % = the light goes off; agreed
 * with the user on 2026-11-10) and a full cell (4200 mV, end-of-charge
 * voltage). The real curve of a Li-ion cell is flat in the middle
 * (docs/hardware.md, TI SLUAAR3 Fig. 2-1), so it over- or underestimates in
 * the middle range. The load table equals the OCV table: under load the
 * voltage reads lower, i.e. the percentage too low (the safe direction).
 *
 * Samsung INR18650-35E: [UNBELEGT] placeholder = generic table. The datasheet
 * (datasheets/INR18650-35E.pdf, Ver. 1.1) contains no discharge curve. Replace
 * with the own discharge measurement (full cell, light permanently on at
 * 100 %, log every measurement until the shutdown; rest phases for the OCV).
 */
#define BATTERY_OCV_TABLE_GENERIC                               {                                                               { .mv = 3000, .percent = 0 },                               { .mv = 4200, .percent = 100 },                         }
#define BATTERY_LOAD_TABLE_GENERIC      BATTERY_OCV_TABLE_GENERIC

#define BATTERY_OCV_TABLE_35E           BATTERY_OCV_TABLE_GENERIC
#define BATTERY_LOAD_TABLE_35E          BATTERY_OCV_TABLE_GENERIC

/* ---- Deep discharge protection (goal Z8), battery_guard.c ----
 *
 * Thresholds given by the user (2026-11-09). They apply to every measurement,
 * also under load (light on): the voltage then reads lower, so the reaction is
 * earlier, i.e. on the safe side. A BMS in the battery pack is the last
 * hardware protection below these thresholds.
 */

/** Below this voltage: warning, the chain flashes BATTERY_WARN_FLASH_COUNT times (inverse if on). */
#define BATTERY_WARN_MV                 3300

/**
 * Below this voltage: light off, emergency deep sleep. Measured on the
 * prototype (ME6217, 2026-11-08): stable down to 3.00 V, reboot loop from 2.9 V.
 */
#define BATTERY_SHUTDOWN_MV             3000

/**
 * After an emergency deep sleep the device only resumes from this voltage on
 * (battery charged); below it, it goes back to deep sleep. Own choice: clearly
 * above BATTERY_WARN_MV, so a resting cell that recovered a little does not
 * cycle between running and emergency sleep.
 */
#define BATTERY_RESUME_MV               3600

/**
 * Timer wakeup in emergency deep sleep to check whether the battery has been
 * charged (besides the button). Own choice; a wakeup costs one boot plus one
 * ADC measurement.
 */
#define BATTERY_EMERGENCY_WAKE_MS       (6ULL * 60 * 60 * 1000)

/** Warning pattern: number of flashes, flash and pause duration (own choice). */
#define BATTERY_WARN_FLASH_COUNT        3
#define BATTERY_WARN_FLASH_MS           200
#define BATTERY_WARN_PAUSE_MS           300

/**
 * Time for the light to ramp down before the emergency deep sleep. Covers
 * LIGHT_RAMP_MS (measured ramp currently ~2 s instead of 1 s,
 * docs/known-issues.md 17) plus the PWM stop latch.
 */
#define BATTERY_SHUTDOWN_LIGHT_WAIT_MS  3000

/**
 * "Battery empty" signal before the emergency deep sleep and after a button
 * wakeup that stays in it (not after a timer wakeup): BATTERY_EMPTY_FLASH_COUNT
 * short flashes at this brightness (BATTERY_WARN_FLASH_MS / BATTERY_WARN_PAUSE_MS),
 * then one more flash that fades out with LIGHT_RAMP_MS. Own choice, agreed
 * with the user on 2026-11-10.
 */
#define BATTERY_EMPTY_FLASH_COUNT       3
#define BATTERY_EMPTY_LEVEL_PERCENT     30

/* ---- Consumption model, remaining time (goal Z14), battery_model.c ----
 *
 * All current and capacity values are PLACEHOLDERS [UNBELEGT] until they are
 * measured (docs/power-measurement.md). The remaining time is only as good as
 * these values.
 */

/* Capacity: cell type, capacity per cell and parallel cells above (set in HA). */

/**
 * Usable share of the capacity until the cut-off threshold (goal Z8), in per
 * mille. [UNBELEGT] docs/hardware.md lists 6-15 % below 3.3 V depending on the
 * source; 880 is a conservative start value.
 */
#define BATTERY_USABLE_PERMILLE         880

/** Mean battery current with the light off (sleep + polls) in uA. [UNBELEGT] placeholder. */
#define BATTERY_I_IDLE_UA               500

/**
 * Additional battery current at full brightness in uA. [UNBELEGT] placeholder.
 * Assumption: the current scales linearly with the PWM duty (docs/goals.md, Dimmen).
 */
#define BATTERY_I_LIGHT_FULL_UA         150000

/**
 * Battery voltage of the first measurement after boot (light off) from which
 * the cell counts as freshly charged: the consumption counter is reset.
 * Own choice below the 4.2 V end-of-charge voltage.
 */
#define BATTERY_FULL_MV                 4150

/** Update interval of the model and of the remaining-time attributes. */
#define BATTERY_MODEL_INTERVAL_MS       (60 * 60 * 1000)

/** Save the counter to NVS every N updates (power loss loses at most N intervals). */
#define BATTERY_NVS_SAVE_EVERY          6

/** Length of the moving average of the daily consumption, in days. */
#define BATTERY_DAYS_AVG_N              7

/** NVS namespace of the model. */
#define BATTERY_NVS_NAMESPACE           "battery"

#endif // BATTERY_CONFIG_H
