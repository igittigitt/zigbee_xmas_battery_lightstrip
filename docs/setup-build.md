# Setup, Build & Flash

## Voraussetzungen

- **ESP-IDF 5.5.4** (`idf: ~5.5.4` in [main/idf_component.yml](../main/idf_component.yml), gelockt in [dependencies.lock](../dependencies.lock)). Mit einer anderen IDF-Version bricht der Component Manager ab.
- Ziel-Chip **esp32h2**
- Komponente (automatisch über den IDF Component Manager): `espressif/esp-zigbee-lib` **==2.0.5**. esp-zboss-lib entfällt seit 2.x.

Die Komponenten landen in `managed_components/` (gitignored) und werden beim ersten Build heruntergeladen. Details zu Bibliotheken, Versionen und Kompatibilität: [zigbee-framework.md](zigbee-framework.md).

## Entwicklungsumgebung

### Option A: VS Code + ESP-IDF Extension (Windows)

[.vscode/settings.json](../.vscode/settings.json) ist auf Port `COM5` und clangd aus dem Espressif-Toolchain-Ordner konfiguriert. **Die Erweiterung muss ESP-IDF 5.5.4 verwenden** („ESP-IDF: Select Current ESP-IDF Version“, danach „Developer: Reload Window“). Verwendet sie noch 5.5.1, scheitern Build und menuconfig an der Toolchain-Prüfung (`Tool doesn't match supported version from list ['esp-14.2.0_20241119']`), weil `build/` mit der 5.5.4-Toolchain `esp-14.2.0_20260121` konfiguriert ist (siehe [known-issues.md](known-issues.md), Punkt 14). Die Auswahl listet nur Installationen aus `%USERPROFILE%\.espressif\esp_idf.json`. Eine mit `install.ps1` eingerichtete Version steht dort nicht automatisch und muss von Hand als Eintrag unter `idfInstalled` ergänzt werden (`version`, `python`, `path`, wie beim vorhandenen Eintrag). Hier am 2026-11-08 für 5.5.4 erledigt.

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

`DEBUG` ist der zentrale Build-Schalter des Projekts, mit vier Stufen: Kconfig-Auswahl **DEBUG level** (`CONFIG_ZL_DEBUG_LEVEL_0/1/2/3`, Menü **Zigbee Light**, definiert in [main/Kconfig.projbuild](../main/Kconfig.projbuild)). Im C-Code ([main/app_config.h](../main/app_config.h)):

- `APP_DEBUG_LEVEL`: 0, 1, 2 oder 3.
- `APP_DEBUG`: 1 bei Level 1, 2 und 3 (abgeleitet aus `CONFIG_ZL_DEBUG`, das automatisch gesetzt wird).
- `APP_DEBUG_LIGHT`: 1 nur bei Level 1 (Onboard-LED).
- `APP_DEBUG_ZIGBEE`: 1 nur bei Level 1 (Zigbee-Diagnose im Log).
- `APP_LIGHT_SLEEP`: 0 nur bei Level 1.
- `APP_DEBUG_MARKERS`: 1 nur bei Level 3 (PPK2-Marker, Log stumm). Ausnahme: Die Akkumessung beim Start wird einmal ausgegeben (`POWER_CFG_CLUSTER: Battery … mV`, UART0 TX GPIO 24). Mit USB als Versorgung misst sie die USB-Spannung, nicht die des PPK2.
- `APP_BATTERY_ADC`: 1 bei Level 0 und 3, bei Level 1/2 nur mit `CONFIG_ZL_DEBUG_BATTERY_ADC`.

| | Level 1: allgemeine Tests | Level 2: Energiespar-Tests | Level 3: PPK2-Strommessung | Level 0: Produktiv |
|---|---|---|---|---|
| Zweck | Allgemeines und Zigbee-Debugging am USB, Energieverbrauch egal | Verhalten wie produktiv, mit Log über UART | Strommessung: Verhalten wie produktiv, Ereignisse als Marker auf den PPK2-Logikeingängen statt Log | Endgerät |
| Aufbau | ESP32-H2-Zero am USB (Strom, Flash, Log) | Akku bzw. PPK2, Log über UART-Adapter (GPIO 24), siehe [Log im Akkubetrieb](#log-im-akkubetrieb-ohne-usb) | PPK2 als Quelle, Logic-Port an GPIO 2–5, **kein UART-Adapter** (speist über seine Leitungen mit), siehe [PPK2-Marker](#ppk2-marker-level-3) | Akku, umgebautes Board, LED-Pfad |
| Lichterkette | Onboard-WS2812B-0807 an GPIO 8, warmweiß. Farbe und Byte-Reihenfolge stehen in `light_config.h`: Diese LED erwartet **R,G,B** (`LIGHT_WS2812_ORDER_RGB`), am Prototyp ermittelt. **Zusätzlich** wird parallel der PWM-Ausgang für die echte Lichterkette angesteuert (KSQ an GPIO 10, EN GPIO 11), Backend `light_driver_debug.c`; Rampe dort in Software-Stufen (20 ms) statt LEDC-Hardware-Fade | nur PWM wie produktiv; Onboard-LED bleibt aus (wird nicht initialisiert) | wie Level 2 | externer KSQ-Treiber: PWM GPIO 10, Boost-EN GPIO 11 |
| Akkustand | **emuliert**: `CONFIG_ZL_DEBUG_BATTERY_PERCENT` (Default 95 %), *menuconfig → Zigbee Light*. Mit `CONFIG_ZL_DEBUG_BATTERY_ADC=y` wird stattdessen wie im Produktivbetrieb gemessen (nur mit angeschlossenem Teiler sinnvoll, z. B. zum Kalibrieren) | wie Level 1 | ADC-Messung wie produktiv | ADC-Messung an GPIO 1 (`APP_BATTERY_ADC`), siehe [hardware.md](hardware.md#firmware-umsetzung-z7) |
| Light Sleep | **gesperrt** per PM-Lock. Außerdem `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` (per `select`). Details: [power-management.md](power-management.md#sleep-simulation-im-debug-build) | **echt**. USB Serial/JTAG fällt im Schlaf aus, daher UART-Konsole | echt; Callbacks beim Ein- und Ausschlafen setzen Marker D0 | echt |
| Diagnose-Log | GPIO-Sleep-Bericht beim Start; Zigbee-Diagnose (`APP_DEBUG_ZIGBEE`): TX-Leistung und Nachbartabelle nach dem Join, nach jedem gescheiterten Join/Rejoin Nachbartabelle + aktuelle Min-Join-LQI; Radio-Trace (`ZL_DEBUG_RADIO_TRACE`, nur Level 1, Standard an): empfangene Beacons mit LQI/RSSI, Beacon Requests, Association Request/Response, NWK-Kommandos; eigener Beacon-Scan nur mit `ZL_DEBUG_DIAG_SCAN` (nur Level 1, Standard aus) ([known-issues 1c](known-issues.md)) | GPIO-Sleep-Bericht beim Start, sonst wie produktiv (keine Zigbee-Diagnose, kein Radio-Trace), damit das Log die Wachzeit nicht verlängert | **stumm**: nach `debug_marker_init()` `esp_log_level_set("*", ESP_LOG_NONE)`. Davor kommen nur die Boot-Meldungen von ROM, Bootloader und IDF sowie die erste App-Zeile | Min-Join-LQI beim Start, Parent nach dem Join |

Jeder DEBUG-Build (Level ≥ 1) gibt direkt nach dem Start ein auffälliges Banner `***  DEBUG BUILD (LEVEL n)  ***` als Warnung (gelb) im Log aus ([main.c](../main/main.c)), auch in Level 3 vor dem Stummschalten des Logs. Produktiv-Builds geben es nicht aus.

Weitere Debug-Abweichungen an `APP_DEBUG` (alle DEBUG-Level), `APP_DEBUG_ZIGBEE` (Zigbee-Diagnose, Level 1) oder `APP_DEBUG_LEVEL` hängen und in diese Tabelle eintragen. Im Code bevorzugt `if (APP_DEBUG)` statt `#if`, damit beide Zweige kompiliert werden.

Umschalten:

- Aktuell steht `CONFIG_ZL_DEBUG_LEVEL_1=y` in [sdkconfig.defaults](../sdkconfig.defaults) (Prototyp-Phase).
- Ein vorhandenes `sdkconfig` hat Vorrang vor den Defaults. Zum Umschalten daher `idf.py menuconfig` → *Zigbee Light* → *DEBUG level*, oder `sdkconfig` löschen und die Defaults ändern.
- **Von Level 1 auf 2 nur per menuconfig oder mit neuem `sdkconfig`:** `USJ_NO_AUTO_LS_ON_CONNECTION` setzt nur Level 1 per `select`. Ein bestehendes `sdkconfig` behält den Wert aber unter Umständen auch nach dem Umschalten. Dann bleibt die CPU in Level 2 wach, solange USB verbunden ist. Das Log meldet das beim Start („light sleep is SIMULATED“). In Level 2 ohnehin kein USB anschließen.
- Test-Build einer anderen Stufe, ohne die lokale `sdkconfig` anzufassen (separates Build-Verzeichnis). Für den Produktiv-Build (Level 0) liegt [release.defaults](../release.defaults) im Repo (seit 2026-11-09):
  ```bash
  idf.py -B build_release -D SDKCONFIG=build_release/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;release.defaults" build
  ```
- **Level 2** auf dieselbe Weise mit [level2.defaults](../level2.defaults) und `-B build_l2`. Schalter `ZL_DEBUG_PM_DUMP` (nur Level 2, Standard an, setzt `CONFIG_PM_PROFILING` per `select`): alle `POWER_MGMT_PM_DUMP_INTERVAL_MS` (30 s) gibt `esp_pm_dump_locks()` über die UART-Konsole aus, welche PM-Locks wie lange gehalten wurden, die Zeit pro PM-Modus und die Zähler `light_sleep_counts` / `light_sleep_reject_counts`. Damit lässt sich klären, was den Light Sleep verhindert.
- **Level 3** auf dieselbe Weise mit [level3.defaults](../level3.defaults) (im Repo). Flashen aus diesem Verzeichnis, USB dafür kurz anstecken und den PPK2-Ausgang vorher ausschalten:
  ```bash
  idf.py -B build_l3 -D SDKCONFIG=build_l3/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;level3.defaults" build
  idf.py -B build_l3 -D SDKCONFIG=build_l3/sdkconfig -p COMx flash
  ```
  In der PowerShell das Argument in Anführungszeichen setzen: `-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;level3.defaults"`. Die Verzeichnisse `build_*/` sind gitignored.
- **Diagnose-Schalter `ZL_DEBUG_DIAG_NO_LEDC`** (Level 2/3, Standard aus, *menuconfig → Zigbee Light*): Das PWM-Backend konfiguriert kein LEDC. GPIO 10 (CE der KSQ) und GPIO 11 (EN) sind einfache Ausgänge auf Low, auch im Sleep, und das Licht bleibt aus. Er dient dazu, den Ruhestrom-Anteil der LEDC-Konfiguration (RC_FAST, `LEDC_SLEEP_MODE_KEEP_ALIVE`, [known-issues](known-issues.md) 18) per Vergleichsmessung zu bestimmen. Nie produktiv verwenden. Beispiel mit einer zusätzlichen Defaults-Datei, die nur `CONFIG_ZL_DEBUG_DIAG_NO_LEDC=y` enthält:
  ```bash
  idf.py -B build_l3diag -D SDKCONFIG=build_l3diag/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;level3.defaults;diag_noledc.defaults" build
  ```

**VS Code (ESP-IDF-Erweiterung), seit 2026-11-11:**

- **Profile:** [CMakePresets.json](../CMakePresets.json) im Repo enthält die Profile `release` (`build_release`), `debug` (Level 1, `build`), `level2` (`build_l2`) und `level3` (`build_l3`) mit denselben `SDKCONFIG`/`SDKCONFIG_DEFAULTS` wie die Befehle oben. Auswahl per Befehlspalette **`ESP-IDF: Select Project Configuration`**; das aktive Profil steht in der Statusleiste, die Auswahl merkt sich die Erweiterung je Workspace. Laut Doku wendet die Erweiterung das gewählte Profil „when building/flashing/monitoring“ an. Quelle: [Project Configuration](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/additionalfeatures/project-configuration.html), [Multiple Projects](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/additionalfeatures/multiple-projects.html) (gelesen am 2026-11-11, Doku-Stand „latest“; ob die installierte Version der Erweiterung `CMakePresets.json` schon unterstützt, ist nicht geprüft).
- **Ohne gewähltes Profil:** Die lokale `.vscode/settings.json` (gitignored) setzt `idf.buildPath`, `idf.sdkconfigFilePath` und `idf.sdkconfigDefaults` auf den Release-Build, damit der Flash-Button standardmäßig das Produktiv-Image schreibt. Ohne diese Einstellung nimmt die Erweiterung `build` (Default von `idf.buildPath`, [Settings](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/settings.html)), also Level 1.
- Kontrolle nach dem Flashen: Startlog `Light initialised, backend: pwm (DEBUG level 0)`.
- `CMakePresets.json` wirkt nur in der Erweiterung bzw. bei `cmake --preset`; `idf.py` ohne `-B`/`-D` baut weiter Level 1 nach `build`.

**Welche Firmware läuft?** Maßgeblich ist die Zeile `app_init: ELF file SHA256:` im Startlog. Sie mit dem Hash des Builds vergleichen, z. B. `sha256sum build_l3/zigbee_node.elf` (die ersten 9 Zeichen). Die Zeile `app_init: Compile time:` taugt dafür nicht: Am 2026-11-09 zeigte sie nach dem Flashen eines neuen inkrementellen Builds noch die Zeit eines älteren Builds, obwohl der SHA256 bewies, dass die neue Firmware lief. Die Monitor-Warnung „Checksum mismatch between flashed and built applications“ bedeutet nur, dass der Monitor mit einem anderen Build-Verzeichnis vergleicht. `idf.py monitor` deshalb mit denselben `-B …`/`-D SDKCONFIG=…`-Angaben wie beim Flashen starten.

### PPK2-Marker (Level 3)

Modul [main/debug_marker.c](../main/debug_marker.c), Pins in [debug_marker.h](../main/debug_marker.h). Die Marker-Pins halten ihren Pegel auch im Light Sleep (`gpio_sleep_sel_dis()`).

| PPK2 | GPIO | High bedeutet |
|---|---|---|
| D0 | 2 | wach (Low = im automatischen Light Sleep). Gesetzt von `esp_pm_light_sleep_register_cbs()`-Callbacks (`CONFIG_PM_LIGHT_SLEEP_CALLBACKS`, per `select` von Level 3) |
| D1 | 3 | **wechselt den Pegel** bei jedem Zigbee-Signal (`zigbee_signal_handler`) und jeder ZCL-Aktion (`attribute_handler_callback`): jede Flanke = ein Ereignis. Ein Puls wäre zu kurz, weil die Handler nur Arbeit einreihen (wenige µs) und der PPK2 bei 100 kS/s nur alle 10 µs abtastet (S4-Messung 2026-11-09: D1 blieb flach) |
| D2 | 4 | Licht-Task ändert den Pegel, inkl. LEDC-Fade (`light_apply_level`, der Fade wartet mit `LEDC_FADE_WAIT_DONE`) |
| D3 | 5 | Akkumessung: ADC-Unit angelegt bis gelöscht (`battery_measure_mv`) |

Anschluss: PPK2 Logic `VCC` → 3V3 des Boards (Pegelreferenz), Logic `GND` → GND, D0–D3 → GPIO 2–5. Im Power Profiler die *Digital channels* einblenden.

> ⚠️ **Befund 2026-11-09:** Mit angeschlossenem Logic-Kabel zeigte die erste Stufe-3-Messung konstant ≈ 11 mA, und auf D0–D3 gab es keinen einzigen Pegelwechsel. Ohne Logic-Kabel schläft dieselbe Firmware normal (Ruhephase 866 µA). Ursache **[UNBELEGT]**. Ich glaube es könnte so sein: Der Logic-Port wurde angesteckt bzw. VCC verbunden, während der PPK2 das Board bereits versorgte. In einem Nordic-DevZone-Thread (Forenbeitrag, nicht offiziell: [#77521](https://devzone.nordicsemi.com/f/nordic-q-a/77521/unexpected-behaviour-with-ppk2-logic-inputs-logic-port-vcc-pin)) wird für genau diese Reihenfolge ein Latch-up des Pegelwandlers im PPK2 vermutet, mit dem Symptom „keine Logikpegel“. **Vorgehen bis zur Klärung:** Logic-Kabel komplett anschließen (GND, VCC, D0–D3), **solange der PPK2-Ausgang aus ist**, erst dann den Ausgang einschalten. Wie viel Strom der Logic-Port selbst zieht, nennen die gefundenen Quellen nicht. **Nachmessung 2026-11-09:** Logic-Kabel bei ausgeschaltetem PPK2-Ausgang angesteckt, dann eingeschaltet. Die Ruhephase blieb bei 864 µA, genauso wie ohne Kabel (866 µA). Der Logic-Port zieht in dieser Reihenfolge also keinen messbaren Strom aus dem Board.

- Die Callbacks laufen laut Kconfig-Hilfe im Idle-Task, direkt vor und nach dem Schlaf, und gehen in die Berechnung der Schlafdauer ein. Sie setzen nur einen Pin (`gpio_set_level` im IRAM, `CONFIG_GPIO_CTRL_FUNC_IN_IRAM`, ebenfalls per `select`).
- **[UNBELEGT]**: ob der Logic-Port des PPK2 (VCC-Referenz, Eingänge) selbst messbar Strom aus dem Board zieht. Einmal S3 mit und ohne angestecktes Logic-Kabel vergleichen.
- Die Pins halten ihren Pegel im Light Sleep trotz `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP`: Am 2026-11-09 war D0 während der µA-Phasen Low und nur während des Polls High (PPK2-Aufnahme).

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
