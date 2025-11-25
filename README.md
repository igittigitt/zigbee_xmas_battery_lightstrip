# Rechargeable Battery Powered Zigbee LED Christmas Lightstrip

Dieses Projekt dient keinem speziellen Zweck, es ist eher eine Machbarkeitsstudie und zum lernen.

## Hardware Required

* ESP32-H2
* Li-Ion Batterie (18650) 3.000 - 5.000 mAh
* LED KSQ-Shield (~100 mA)
* Buck-Boost Controller 2,0 .. 5,0 V Eingang, 3,3 V Ausgang, 0,5 - 1 A

## Configure the project

Before project configuration and build, make sure to set the correct chip target using `idf.py --preview set-target TARGET` command.

## Erase the NVRAM

Before flash it to the board, it is recommended to erase NVRAM if user doesn't want to keep the previous examples or other projects stored info using `idf.py -p PORT erase-flash`

## Build and Flash

Build the project, flash it to the board, and start the monitor tool to view the serial output by running `idf.py -p PORT flash monitor`.

(To exit the serial monitor, type ``Ctrl-]``.)

## Example Output


## Light Control Functions

 * By toggling the switch button (BOOT) on the ESP32-H2 board loaded with the `HA_on_off_switch` example, the LED on this board loaded with `HA_on_off_light` example will be on and off.

## Troubleshooting

