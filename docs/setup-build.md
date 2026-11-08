# Setup, Build & Flash

## Voraussetzungen

- **ESP-IDF 5.5.4** (`idf: ~5.5.4` in [main/idf_component.yml](../main/idf_component.yml), gelockt in [dependencies.lock](../dependencies.lock)). Mit einer anderen IDF-Version bricht der Component Manager ab.
- Ziel-Chip **esp32h2**
- Komponente (automatisch über den IDF Component Manager): `espressif/esp-zigbee-lib` **==2.0.5**. esp-zboss-lib entfällt seit 2.x.

Die Komponenten landen in `managed_components/` (gitignored) und werden beim ersten Build heruntergeladen. Details zu Bibliotheken, Versionen und Kompatibilität: [zigbee-framework.md](zigbee-framework.md).

## Entwicklungsumgebung

### Option A: VS Code + ESP-IDF Extension (Windows)

[.vscode/settings.json](../.vscode/settings.json) ist auf Port `COM5` und clangd aus dem Espressif-Toolchain-Ordner konfiguriert. **`idf.currentSetup` zeigt noch auf `C:\Users\…\esp\v5.5.1\esp-idf` und muss auf `…\esp\v5.5.4\esp-idf` umgestellt werden**, sonst scheitert der Build an der IDF-Version (siehe [known-issues.md](known-issues.md)).

### Option B: Devcontainer

[.devcontainer/](../.devcontainer/) baut auf dem Image `espressif/idf` auf und lädt die IDF-Umgebung automatisch. Für den Flash-Zugriff läuft der Container `--privileged`. Unter Windows muss der USB-Port ggf. per `usbipd` in WSL durchgereicht werden.

## Build

```bash
idf.py set-target esp32h2   # nur einmal bzw. nach Löschen von build/
idf.py build
```

Konfiguration:

- [sdkconfig.defaults](../sdkconfig.defaults) – versionierte Grundeinstellungen (Zigbee, Power Management, Loglevel, Partitionstabelle).
- `sdkconfig` – generiert, gitignored. Nach Änderungen an `sdkconfig.defaults` `sdkconfig` löschen oder `idf.py fullclean` ausführen, damit die Defaults greifen.
- `idf.py menuconfig` für interaktive Änderungen.

## DEBUG-Schalter

`DEBUG` ist der zentrale Build-Schalter des Projekts, mit drei Stufen: Kconfig-Auswahl **DEBUG level** (`CONFIG_ZL_DEBUG_LEVEL_0/1/2`, Menü **Zigbee Light**, definiert in [main/Kconfig.projbuild](../main/Kconfig.projbuild)). Im C-Code ([main/app_config.h](../main/app_config.h)):

- `APP_DEBUG_LEVEL`: 0, 1 oder 2.
- `APP_DEBUG`: 1 bei Level 1 und 2 (abgeleitet aus `CONFIG_ZL_DEBUG`, das automatisch gesetzt wird).
- `APP_DEBUG_LIGHT`: 1 nur bei Level 1 (Onboard-LED).
- `APP_LIGHT_SLEEP`: 0 nur bei Level 1.

| | Level 1: allgemeine Tests | Level 2: Energiespar-Tests | Level 0: Produktiv |
|---|---|---|---|
| Zweck | Funktionstests am USB, Energieverbrauch egal | Verhalten und Verbrauch wie produktiv, mit Diagnose | Endgerät |
| Aufbau | ESP32-H2-Zero am USB (Strom, Flash, Log) | Akku bzw. PPK2, Log über UART-Adapter (GPIO 24), siehe [Log im Akkubetrieb](#log-im-akkubetrieb-ohne-usb) | Akku, umgebautes Board, LED-Pfad |
| Lichterkette | Onboard-WS2812B-0807 an GPIO 8, warmweiß. Farbe und Byte-Reihenfolge stehen in `light_config.h`: Diese LED erwartet **R,G,B** (`LIGHT_WS2812_ORDER_RGB`), am Prototyp ermittelt. **Zusätzlich** wird parallel der PWM-Ausgang für die echte Lichterkette angesteuert (KSQ an GPIO 10, EN GPIO 11), Backend `light_driver_debug.c`; Rampe dort in Software-Stufen (20 ms) statt LEDC-Hardware-Fade | nur PWM wie produktiv; Onboard-LED bleibt aus (wird nicht initialisiert) | externer KSQ-Treiber: PWM GPIO 10, Boost-EN GPIO 11 |
| Akkustand | **emuliert**: `CONFIG_ZL_DEBUG_BATTERY_PERCENT` (Default 95 %), *menuconfig → Zigbee Light*. Mit `CONFIG_ZL_DEBUG_BATTERY_ADC=y` wird stattdessen wie im Produktivbetrieb gemessen (nur mit angeschlossenem Teiler sinnvoll, z. B. zum Kalibrieren) | wie Level 1 | ADC-Messung an GPIO 1 (`APP_BATTERY_ADC`), siehe [hardware.md](hardware.md#firmware-umsetzung-z7) |
| Light Sleep | **gesperrt** per PM-Lock. Außerdem `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` (per `select`). Details: [power-management.md](power-management.md#sleep-simulation-im-debug-build) | **echt**. USB Serial/JTAG fällt im Schlaf aus, daher UART-Konsole | echt |
| Diagnose-Log | GPIO-Sleep-Bericht beim Start, TX-Leistung und Nachbartabelle nach dem Join, nach jedem gescheiterten Join/Rejoin Nachbartabelle + Beacon-Scan ([known-issues 1c](known-issues.md)) | wie Level 1 | nur Parent nach dem Join |

Weitere Debug-Abweichungen an `APP_DEBUG` (beide Level) oder `APP_DEBUG_LEVEL` hängen und in diese Tabelle eintragen. Im Code bevorzugt `if (APP_DEBUG)` statt `#if`, damit beide Zweige kompiliert werden.

Umschalten:

- Aktuell steht `CONFIG_ZL_DEBUG_LEVEL_1=y` in [sdkconfig.defaults](../sdkconfig.defaults) (Prototyp-Phase).
- Ein vorhandenes `sdkconfig` hat Vorrang vor den Defaults. Zum Umschalten daher `idf.py menuconfig` → *Zigbee Light* → *DEBUG level*, oder `sdkconfig` löschen und die Defaults ändern.
- **Von Level 1 auf 2 nur per menuconfig oder mit neuem `sdkconfig`:** `USJ_NO_AUTO_LS_ON_CONNECTION` setzt nur Level 1 per `select`. Ein bestehendes `sdkconfig` behält den Wert aber unter Umständen auch nach dem Umschalten. Dann bleibt die CPU in Level 2 wach, solange USB verbunden ist. Das Log meldet das beim Start („light sleep is SIMULATED“). In Level 2 ohnehin kein USB anschließen.
- Test-Build einer anderen Stufe, ohne die lokale `sdkconfig` anzufassen (separates Build-Verzeichnis). `release.defaults` soll `CONFIG_ZL_DEBUG_LEVEL_0=y` enthalten; **die Datei fehlt noch im Repo**:
  ```bash
  idf.py -B build_release -D SDKCONFIG=build_release/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;release.defaults" build
  ```

### Build aus der PowerShell

`export.ps1` nimmt das `python` aus dem PATH. Passt dessen Version nicht zur installierten IDF-Python-Umgebung (hier `idf5.5_py3.11_env`), bricht es mit „ESP-IDF Python virtual environment … not found“ ab. Workaround: `activate.py` direkt mit dem Python der vorhandenen Umgebung aufrufen. Der erste Aufruf mit einer neuen IDF-Version dauert mehrere Minuten.

```powershell
$env:IDF_PATH = "C:\Users\muogeisen\esp\v5.5.4\esp-idf"
$env:IDF_TOOLS_PATH = "C:\Users\muogeisen\.espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\muogeisen\.espressif\python_env\idf5.5_py3.11_env"
. (& "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\activate.py" --export)
idf.py build
```

## Flash & Monitor

```bash
idf.py -p COM5 erase-flash     # empfohlen beim ersten Mal / nach Netzwerkwechsel
idf.py -p COM5 flash monitor   # Monitor beenden mit Ctrl+]
```

`erase-flash` löscht auch die Zigbee-Netzwerkinformationen. Sie liegen seit esp-zigbee-lib 2.x in der Partition `nvs`. Ohne Löschen versucht das Gerät nach dem Start, dem zuletzt bekannten Netz wieder beizutreten. **Nach dem Umstieg von 1.x auf 2.x einmal `erase-flash` ausführen**, weil sich die Partitionstabelle geändert hat.

## Logging

Default-Level ist `INFO`, maximal `DEBUG` kompilierbar (`CONFIG_LOG_MAXIMUM_LEVEL_DEBUG`). Jedes Modul nutzt ein eigenes Tag:

| Tag | Modul |
|---|---|
| `MAIN` | main.c |
| `ZIGBEE_DEVICE` | zigbee_device.c |
| `CLUSTER_MANAGER` | cluster_manager.c |
| `ON_OFF_CLUSTER` | on_off_cluster.c |
| `IDENTIFY_CLUSTER` | identify_cluster.c |
| `POWER_CFG_CLUSTER` | power_config_cluster.c |
| `ATTR_HANDLER` | attribute_handler.c |
| `BATTERY` | battery/battery.c |
| `POWER_MGMT` | power_management.c |
| `BUTTON` | button.c |
| `LIGHT` | light/light.c |
| `LIGHT_WS2812` | light/light_driver_ws2812.c (DEBUG Level 1) |
| `LIGHT_PWM` | light/light_driver_pwm.c (alle Stufen) |
| `LIGHT_DEBUG` | light/light_driver_debug.c (DEBUG Level 1: WS2812 + PWM parallel) |

Einzelne Tags lassen sich zur Laufzeit per `esp_log_level_set("TAG", ESP_LOG_DEBUG)` anpassen.

## Log im Akkubetrieb (ohne USB)

Am Akku darf kein USB-Kabel stecken (5V-Pin = VBUS, siehe [hardware.md](hardware.md#esp-regler)). Die Konsole läuft aber **primär über UART0** (`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`, 115200 Baud) und nur sekundär über USB Serial/JTAG (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`). Das Log liegt also immer auch an **GPIO 24 (U0TXD)** an (IDF `soc/esp32h2/include/soc/uart_pins.h`).

Anschluss eines USB-UART-Adapters mit **3,3-V-Pegel**:

```
ESP GPIO 24 (TX) ──► RX des Adapters
ESP GND          ──── GND des Adapters
(VCC des Adapters NICHT verbinden, TX des Adapters nicht nötig)
```

- Nur RX + GND: Das Log ist reine Ausgabe, Eingaben braucht die Firmware nicht. Ich glaube es könnte so sein: Ohne Verbindung zum ESP-RX (GPIO 23) kann der Adapter das Board bei ausgeschaltetem Akku nicht über die Schutzdioden mitversorgen – nicht gemessen.
- Kein 5-V-Adapter: Dessen RX-Eingang kann einen Pull-up auf 5 V haben, der dann an GPIO 24 läge.
- Anzeigen: `idf.py -p COMx monitor` mit dem COM-Port des Adapters. DTR/RTS sind nicht angeschlossen, das Board wird also nicht zurückgesetzt.
- **Light Sleep im DEBUG-Build:** In Level 1 sperrt ein PM-Lock den Schlaf auch am Akku. Für realistisches Verhalten am Akku **Level 2** verwenden (siehe [DEBUG-Schalter](#debug-schalter)). Die UART-Konsole verträgt Light Sleep: Vor dem Schlaf wartet IDF, bis die UART-Ausgabe gesendet ist (`flush_uarts()` in `esp_hw_support/sleep_modes.c`). USB Serial/JTAG funktioniert dagegen im und nach dem Light Sleep nicht (Kconfig-Hilfe `USJ_NO_AUTO_LS_ON_CONNECTION`).
- **Strommessung:** Jede Log-Zeile hält die CPU wach, bis sie gesendet ist (115200 Baud ≈ 87 µs pro Zeichen, Rechnung: 10 Bit / 115200). Für Strommessungen das Log reduzieren, siehe [power-measurement.md](power-measurement.md#firmware-für-messungen-vorbereiten).

Alternative: USB-Kabel mit unterbrochener VBUS-Ader (nur Daten). Das ist für den Akkubetrieb ungeeignet: Solange USB verbunden ist, verhindert `USJ_NO_AUTO_LS_ON_CONNECTION` den Light Sleep. Ohne diese Option bricht die USB-Konsole beim ersten Schlaf ab.

## Partitionstabelle

[partitions.csv](../partitions.csv) ist über `CONFIG_PARTITION_TABLE_CUSTOM=y` aktiv. Das Layout folgt den 2.0.5-Beispielen, nur ohne `otadata`, weil es noch kein OTA gibt:

| Partition | Größe | Inhalt |
|---|---|---|
| `nvs` | 24 KB | NVS der Anwendung **und** Zigbee-Datensätze (`ZB_STORAGE_PARTITION_NAME`) |
| `phy_init` | 4 KB | PHY-Kalibrierdaten |
| `factory` | 940 KB | Firmware (Build 2026-11-07: 586 KB) |
| `zb_fct` | 1 KB | optionale Zigbee-Werksdaten |

Die Offsets vergibt das Build-System automatisch.
