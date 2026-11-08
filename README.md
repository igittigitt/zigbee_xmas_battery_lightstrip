# Rechargeable Battery Powered Zigbee LED Christmas Lightstrip

Dieses Projekt dient keinem speziellen Zweck, es ist eher eine Machbarkeitsstudie und zum Lernen.

Firmware für eine batteriebetriebene Zigbee-LED-Lichterkette: **ESP32-H2**, **ESP-IDF 5.5.4**, **esp-zigbee-lib 2.0.5**, Rolle Sleepy End Device, Ziel **Zigbee2MQTT**.

## Dokumentation

Die gesamte Dokumentation steht in [docs/](docs/README.md):

- [Ziele und Umfang](docs/goals.md)
- [Hardware](docs/hardware.md)
- [Toolchain, Build, Flash](docs/setup-build.md)
- [Zigbee-Framework (Versionen, Programmiermodell)](docs/zigbee-framework.md)
- [Bekannte Probleme](docs/known-issues.md)

Hinweise für KI-Coding-Agents: [AGENTS.md](AGENTS.md).

## Schnellstart

```bash
idf.py set-target esp32h2
idf.py build
idf.py -p PORT flash monitor
```

Details und Build aus der PowerShell: [docs/setup-build.md](docs/setup-build.md).

## Lizenz

MIT License - frei verwendbar für private und kommerzielle Projekte.

## Credits

Basierend auf Espressif ESP-IDF Zigbee Examples
