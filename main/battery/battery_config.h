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

/**
 * Open circuit voltage -> state of charge, piecewise linear, ascending by mV.
 *
 * [UNBELEGT] Placeholder: linear between the planned cut-off threshold
 * (3300 mV, goal Z8, docs/hardware.md "Abschaltschwelle ca. 3,2-3,3 V") and
 * a full Li-ion cell (4200 mV, end-of-charge voltage of the charger). The real
 * curve of a Li-ion cell is flat in the middle (docs/hardware.md, TI SLUAAR3
 * Fig. 2-1), so this table over- or underestimates in the middle range.
 * Replace with points from the discharge curve of the cell actually used
 * (cell datasheet or own measurement); more rows can simply be added.
 */
#define BATTERY_OCV_TABLE                                   \
    {                                                       \
        { .mv = 3300, .percent = 0 },                       \
        { .mv = 4200, .percent = 100 },                     \
    }

/* ---- Consumption model, remaining time (goal Z14), battery_model.c ----
 *
 * All current and capacity values are PLACEHOLDERS [UNBELEGT] until they are
 * measured (docs/power-measurement.md). The remaining time is only as good as
 * these values.
 */

/** Nominal capacity in mAh. [UNBELEGT] placeholder, cells of 3000-5000 mAh are planned (docs/hardware.md). */
#define BATTERY_CAPACITY_MAH            3000

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
