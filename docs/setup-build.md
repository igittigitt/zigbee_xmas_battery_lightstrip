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

`DEBUG` ist der zentrale Build-Schalter des Projekts: Kconfig-Option `CONFIG_ZL_DEBUG` (Menü **Zigbee Light**, definiert in [main/Kconfig.projbuild](../main/Kconfig.projbuild)), im C-Code als `APP_DEBUG` (immer 0 oder 1, [main/app_config.h](../main/app_config.h)).

| | DEBUG (`CONFIG_ZL_DEBUG=y`) | Produktiv (`n`) |
|---|---|---|
| Aufbau | ESP32-H2-Zero am USB (Strom, Flash, Log) | Akku, umgebautes Board, LED-Pfad |
| Lichterkette | Onboard-WS2812B-0807 an GPIO 8, warmweiß. Farbe und Byte-Reihenfolge stehen in `light_config.h`: Diese LED erwartet **R,G,B** (`LIGHT_WS2812_ORDER_RGB`), am Prototyp ermittelt | externer KSQ-Treiber: PWM GPIO 10, Boost-EN GPIO 11 |
| Akkustand | **emuliert**: `CONFIG_ZL_DEBUG_BATTERY_PERCENT` (Default 95 %), *menuconfig → Zigbee Light* | `0xFF` (unbekannt) bis Z7 |
| Light Sleep | **simuliert**, solange USB verbunden ist: CPU bleibt wach (`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION`, per `select`), GPIO-Bericht beim Start. Details: [power-management.md](power-management.md#sleep-simulation-im-debug-build) | echt |

Weitere Debug-Abweichungen ebenfalls an `APP_DEBUG` hängen und in diese Tabelle eintragen. Im Code bevorzugt `if (APP_DEBUG)` statt `#if`, damit beide Zweige kompiliert werden.

Umschalten:

- Aktuell steht `CONFIG_ZL_DEBUG=y` in [sdkconfig.defaults](../sdkconfig.defaults) (Prototyp-Phase).
- Ein vorhandenes `sdkconfig` hat Vorrang vor den Defaults. Zum Umschalten daher `idf.py menuconfig` → *Zigbee Light* → *DEBUG mode*, oder `sdkconfig` löschen und die Defaults ändern.
- Produktiv-Testbuild, ohne die lokale `sdkconfig` anzufassen (separates Build-Verzeichnis). `release.defaults` soll `CONFIG_ZL_DEBUG=n` enthalten; **die Datei fehlt noch im Repo**:
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
| `POWER_MGMT` | power_management.c |
| `LIGHT` | light/light.c |
| `LIGHT_WS2812` | light/light_driver_ws2812.c (DEBUG) |
| `LIGHT_PWM` | light/light_driver_pwm.c (Produktiv) |

Einzelne Tags lassen sich zur Laufzeit per `esp_log_level_set("TAG", ESP_LOG_DEBUG)` anpassen.

## Partitionstabelle

[partitions.csv](../partitions.csv) ist über `CONFIG_PARTITION_TABLE_CUSTOM=y` aktiv. Das Layout folgt den 2.0.5-Beispielen, nur ohne `otadata`, weil es noch kein OTA gibt:

| Partition | Größe | Inhalt |
|---|---|---|
| `nvs` | 24 KB | NVS der Anwendung **und** Zigbee-Datensätze (`ZB_STORAGE_PARTITION_NAME`) |
| `phy_init` | 4 KB | PHY-Kalibrierdaten |
| `factory` | 940 KB | Firmware (Build 2026-11-07: 586 KB) |
| `zb_fct` | 1 KB | optionale Zigbee-Werksdaten |

Die Offsets vergibt das Build-System automatisch.
