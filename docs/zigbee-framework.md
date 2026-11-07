# Zigbee-Framework

Stand: 2026-11-07. Beschreibt, **womit** die Firmware gebaut wird: Plattform, Zigbee-Bibliothek, Versionen und Programmiermodell. Das Gerätemodell (Endpoint, Cluster, Commissioning) steht in [zigbee.md](zigbee.md).

Quellenkürzel:
- **Lokal** = Dateien der per Component Manager geladenen Komponente in `managed_components/espressif__esp-zigbee-lib/` (gitignored).
- **SDK 2.0.5** = Repository [espressif/esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) auf Commit `e2fdf4ad8558e5beb306b0d7040072d76244c413`. Auf diesen Commit verweist `repository_info.commit_sha` im Paket esp-zigbee-lib 2.0.5.

## Entscheidung (2026-11-07)

**esp-zigbee-lib 2.0.5, fest gepinnt, auf ESP-IDF v5.5.4.** Vorher lief das Projekt auf esp-zigbee-lib 1.6.8 + esp-zboss-lib 1.6.4 + IDF 5.5.1 und wurde komplett auf die 2.x-API umgestellt.

Gründe:

1. **Gegen 1.x:**
   - Espressif führt 1.x nur noch als „LTS version under maintenance (bug fixes only, no new features)“, 2.x dagegen als „Latest version, recommended for new designs and production“ (README des SDK, Branch `main`, Z. 15–16).
   - 1.6.8 ist die letzte 1.x-Version.
2. **2.0.5 statt 2.1.0:**
   - Für 2.1.0 (03.11.2026) ist eine Regression gemeldet und von Espressif bestätigt: Absturz, wenn ZCL-Datenmodell-APIs vor `esp_zigbee_init()` aufgerufen werden ([#942](https://github.com/espressif/esp-zigbee-sdk/issues/942), Antwort des Maintainers vom 06.11.2026).
   - 2.1.0 stellt außerdem die Standard-Spezifikation auf Zigbee r23 um („Change the default Zigbee specification version to r23“, `RELEASE_NOTES.md`). Ob das mit dem Coordinator (Z-Stack 3.x.0 auf SLZB-06P10) zusammenspielt, ist **[UNBELEGT]**.
3. **Inhalt von 2.0.5** (laut `RELEASE_NOTES.md`):
   - In 2.0.2–2.0.5 wurden Fehler bei Sleepy End Devices behoben: „unexpected wake-up on SED every 10s“ (2.0.2), Wiederholung indirekter MAC-Frames (2.0.4), „retry of data polling“ (2.0.5).
   - Behoben ist auch der Beitritt zu älteren Coordinatoren: „Fixed missing joined status when joining to legacy coordinator“ (2.0.2).
   - Neu ist das Signal `EZB_ZDO_SIGNAL_PARENT_LOST` (2.0.5).
   - Ich glaube es könnte so sein: 2.0.5 arbeitet mit r22 als Standard. Das schließe ich nur daraus, dass 2.1.0 den Standard *auf* r23 ändert; für 2.0.5 selbst ist es nicht ausdrücklich dokumentiert.
4. **ESP-IDF 5.5.4:** „2.0.5 version release of ESP-ZIGBEE-SDK is based on esp-idf v5.5.4“ (`RELEASE_NOTES.md`, Eintrag 16-Oct-2026).

## Überblick

```
┌──────────────────────────────────────────────────────────┐
│ Anwendung (main/)                     C17, eigener Code  │
├──────────────────────────────────────────────────────────┤
│ esp-zigbee-lib 2.0.5   API esp_zigbee_* / ezb_*          │
│   libesp-zigbee.release.a                                │
│   libesp-zigbee-core.zed.release.a   (Stack, Rolle ZED)  │
│   libesp-zigbee-idf.native.release.a (IDF-/Radio-Port)   │
│   src/utils (Quellcode)                                  │
├──────────────────────────────────────────────────────────┤
│ ESP-IDF v5.5.4   FreeRTOS, ieee802154, esp_pm, NVS,      │
│                  mbedtls, openthread (Spinel-Teile)      │
├──────────────────────────────────────────────────────────┤
│ ESP32-H2   natives IEEE-802.15.4-Radio                   │
└──────────────────────────────────────────────────────────┘
```

- Der Zigbee-Stack ist Espressifs eigener Stack. ZBOSS und esp-zboss-lib gibt es in 2.x nicht mehr; Release Notes 2.0.0: „Remove the dependency on ZBOSS.“
- Der Stack liegt nur vorkompiliert vor (lokal `lib/esp32h2/*.a`). Fehler darin lassen sich nur über Espressif-Issues klären.
- Lizenz: Apache-2.0 (lokal `LICENSE`).

## Komponenten und Versionen

| Komponente | Anforderung ([main/idf_component.yml](../main/idf_component.yml)) | Gelockt ([dependencies.lock](../dependencies.lock)) |
|---|---|---|
| ESP-IDF | `~5.5.4` (≥ 5.5.4, < 5.6) | **5.5.4** |
| `espressif/esp-zigbee-lib` | `==2.0.5` | **2.0.5** |

- **`==2.0.5` statt `~2.0.x`:** Der Component Manager soll beim Neu-Locken nicht selbstständig auf eine andere Version wechseln. Ein Update ist eine bewusste Entscheidung; vorher die Release Notes und Issues prüfen.
- **`~5.5.4`:** Das verhindert versehentliche Builds mit der weiterhin installierten IDF 5.5.1, etwa über eine veraltete VS-Code-Einstellung.
- Das Manifest von esp-zigbee-lib 2.0.5 verlangt selbst nur `idf >=5.0` (lokal `idf_component.yml`).

## Auswahl der Bibliotheken über Kconfig

Welche vorkompilierte Variante gelinkt wird, entscheidet `sdkconfig` (lokal `CMakeLists.txt`):

| Kconfig | Wirkung |
|---|---|
| `CONFIG_ZB_ENABLED=y` | Zigbee überhaupt aktiv (Default `n`) |
| `CONFIG_ZB_ZED=y` | `libesp-zigbee-core.zed.*.a` (End Device). Ohne Angabe gilt `CONFIG_ZB_ZCZR` (Coordinator/Router) |
| `CONFIG_ZB_RADIO_NATIVE=y` | `libesp-zigbee-idf.native.*.a`, internes Radio |
| `CONFIG_ZB_DEBUG_MODE=y` | `.debug`- statt `.release`-Bibliotheken |
| `CONFIG_ZB_SDK_1xx=y` | bindet die 1.x-Kompatibilitätsschicht `include/compat` ein. **Nicht** verwendet, der Code nutzt die native 2.x-API |

Alle Werte stehen in [sdkconfig.defaults](../sdkconfig.defaults).

## Programmiermodell (2.0.5)

### Header und Namensräume

- `esp_zigbee.h` – Init/Start/Lock/Factory-Reset (Präfix `esp_zigbee_`).
- `ezbee/*.h` – Stack-API (Präfix `ezb_`): `nwk.h`, `bdb.h`, `af.h`, `zcl/…`, `zha.h` (HA-Gerätevorlagen), `app_signals.h`.

### Startreihenfolge

So aufgebaut in den Beispielen `sleepy_devices/light_sleep_end_device` und `home_automation_devices/on_off_light` (SDK 2.0.5); umgesetzt in [zigbee_device.c](../main/zigbee_device.c):

1. `app_main`:
   1. `nvs_flash_init()`;
   2. `esp_pm_configure()` (Light Sleep);
   3. Task `Zigbee_main` starten (Stack 4096, Prio 5).
2. Zigbee-Task:
   1. `esp_zigbee_init()` mit Gerätekonfiguration (Rolle, `ed_timeout`, `keep_alive`) und Plattformkonfiguration (Speicher-Partition, Radio nativ);
   2. Commissioning-Parameter setzen: Kanalmasken, Signal-Handler, `ezb_nwk_set_rx_on_when_idle(false)`;
   3. Datenmodell anlegen und mit `ezb_af_device_desc_register()` registrieren;
   4. ZCL-Action-Handler registrieren;
   5. `esp_zigbee_start(false)`;
   6. `esp_zigbee_launch_mainloop()` – kehrt erst nach `esp_zigbee_stop()` zurück.
3. Mit `esp_zigbee_start(false)` kommt das Signal `EZB_ZDO_SIGNAL_SKIP_STARTUP`. Darauf startet die Anwendung `EZB_BDB_MODE_INITIALIZATION`. Laut Migration Guide v2.x (`commissioning.rst`) läuft die Initialisierung „not … by default unless the user explicitly sets it“. Ohne diesen Aufruf tritt das Gerät nach einem Neustart also nicht wieder dem bekannten Netz bei.

### Thread-Safety

`esp_zigbee.h` zu `esp_zigbee_lock_acquire()`: „It's mandatory to acquire the lock before calling any Zigbee SDK APIs, except that the call site is in Zigbee callbacks.“

Eine öffentliche, verzögerte Scheduler-Funktion wie früher `esp_zb_scheduler_alarm()` gibt es in 2.x nicht. Die Beispiele verwenden `esp_timer` und rufen im Callback `esp_zigbee_lock_acquire()` auf (`examples/utils/example_common/src/alarm_timer.c` und Aufrufer). Genauso macht es [zigbee_device.c](../main/zigbee_device.c).

### Sleepy End Device

Laut FAQ (SDK 2.0.5, `docs/en/faq.rst`, „Zigbee Light Sleep Mode“) gilt:

- **Schlaf aktivieren:** „the following configurations must be applied … `CONFIG_PM_ENABLE=y`, `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`“, dazu `esp_pm_configure()` und `rx_on_when_idle = false`. Eine eigene Schlaf-Funktion der Anwendung gibt es nicht mehr.
  - In 1.x waren dafür `esp_zb_sleep_enable()`, `esp_zb_sleep_now()` und das Signal `CAN_SLEEP` nötig.
  - Die Migration Guide v2.x (`common.rst`) listet diese als entfernt.
- **Schlafdauer:** „A Zigbee Sleepy End Device is required to maintain a keep-alive interval with its parent device. This interval determines how long the chip can remain in light sleep mode.“
  - Einstellbar über `esp_zigbee_zed_config_s::keep_alive` oder `ezb_nwk_set_keepalive_interval()`.
- **Weitere API** (`ezbee/nwk.h`):
  - `ezb_nwk_set_ed_timeout()`;
  - `ezb_nwk_set_fast_poll_interval()`, laut Header Standard 200 ms;
  - `ezb_nwk_set_keepalive_interval()`, laut Header Standard „1/4 of the ED timeout“, wenn nicht über `keep_alive` gesetzt.
- **Kconfig des Beispiels:** `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP`, `CONFIG_IEEE802154_SLEEP_ENABLE`, `CONFIG_FREERTOS_HZ=1000`, `CONFIG_ESP_SLEEP_POWER_DOWN_FLASH` – übernommen.
- **Einschränkung (FAQ):** „In the light sleep example, the Zigbee task keeps running if the device is not part of a network.“ Ohne Netz schläft das Gerät also nicht richtig. Deshalb ist das Pairing-Fenster zeitlich begrenzt (Ziel Z10).

### Persistenz

- 2.x speichert die Zigbee-Datensätze in einer **NVS**-Partition. Konfiguriert wird sie über `platform_config.storage_partition_name`, bei uns `"nvs"` wie in den Beispielen.
- Seit 2.0.4: „Changed default NVS partition name from "zb_storage" to "nvs"“.
- Die frühere FAT-Partition `zb_storage` entfällt. `zb_fct` (optionale Werksdaten) bleibt wie in den Beispielen.
- `esp_zigbee_factory_reset()`: „This function will clear all the datasets and restart the device“ (`esp_zigbee.h`).

**Folge der Umstellung:** Ein Gerät, das mit der 1.x-Firmware gepairt war, verliert das Pairing. Die 1.x-Daten lagen in `zb_storage`, und die Partition gibt es nicht mehr. Eine Migration per `esp_zigbee_nvram_migrate()` wäre möglich, ist aber nicht umgesetzt, weil noch kein Gerät im Einsatz ist. Nach dem Flashen also einmal `erase-flash` und in Z2M neu anlernen.

## Lokale Build-Umgebung (Windows)

- ESP-IDF v5.5.4 liegt unter `C:\Users\muogeisen\esp\v5.5.4\esp-idf` (Release-Zip vom 27.03.2026).
- Die Tools liegen unter `C:\Users\muogeisen\.espressif`.
- Die Python-Umgebung `idf5.5_py3.11_env` teilen sich IDF 5.5.1 und 5.5.4; beide nutzen `espidf.constraints.v5.5.txt`.
- `idf.py --version` meldet `v5.5.4-dirty`. Ich glaube es könnte so sein: Das `-dirty` kommt von den Git-Metadaten im Release-Zip, die nach dem Entpacken unter Windows abweichen. Für den Build ist es ohne Bedeutung.

Wie man den Build startet, steht in [setup-build.md](setup-build.md#build-aus-der-powershell).

## Status

- `idf.py build` mit ESP-IDF 5.5.4 läuft durch, ohne Warnungen im Projektcode (2026-11-07).
- Auf Hardware ist die Firmware **noch nicht** getestet. Join an Z2M, Schlafverhalten und Reaktionszeit sind offen, siehe [known-issues.md](known-issues.md).
