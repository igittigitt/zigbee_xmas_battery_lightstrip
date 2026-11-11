# Architektur

Stand: esp-zigbee-lib 2.0.5 / ESP-IDF 5.5.4 (siehe [zigbee-framework.md](zigbee-framework.md)).

## Verzeichnisstruktur

```
zigbee-light/
├── CMakeLists.txt              # Projekt "zigbee_node"
├── sdkconfig.defaults          # Versionierte Kconfig-Defaults (ZED, Light Sleep, Partitionen)
├── partitions.csv              # nvs (inkl. Zigbee-Daten), otadata, phy_init, ota_0, ota_1, zb_fct
├── dependencies.lock           # Gelockte Komponenten-Versionen
├── docs/                       # Diese Dokumentation
├── z2m/
│   └── zl-lightchain.mjs       # Zigbee2MQTT External Converter (Licht, Akku, Restlaufzeit Z14)
└── main/
    ├── CMakeLists.txt          # Quellliste der Komponente "main"
    ├── idf_component.yml       # Abhängigkeiten (esp-zigbee-lib ==2.0.5, idf ~5.5.4)
    ├── Kconfig.projbuild       # Menü "Zigbee Light": DEBUG level 0/1/2
    ├── app_config.h            # APP_DEBUG_LEVEL (0/1/2), APP_DEBUG u. a. aus Kconfig
    ├── main.c                  # app_main: NVS, Power Mgmt, Licht, Zigbee-Task
    ├── zigbee_config.h         # Zentrale Zigbee-Konstanten (Identität, Keepalive, Commissioning)
    ├── zigbee_device.[ch]      # Stack-Init, Commissioning (Pairing, Rejoin), Signal-Handler, Task
    ├── power_management.[ch]   # esp_pm (automatischer Light Sleep), DEBUG-Sleep-Bericht
    ├── button.[ch]             # BOOT-Taster (Z5): kurz = Toggle, 5 s = Factory Reset + Pairing
    ├── clusters/
    │   ├── cluster_manager.[ch]       # Baut Endpoint (ZHA Dimmable Light) und registriert ihn
    │   ├── on_off_cluster.[ch]        # On/Off ↔ Licht-Modul, StartUpOnOff (Z6)
    │   ├── identify_cluster.[ch]      # Identify → Blinken (Z11)
    │   ├── power_config_cluster.[ch]  # Power Configuration (Akku, Z7)
    │   ├── battery_runtime_cluster.[ch] # Herstellerspez. Cluster 0xFC00: Restlaufzeit (Z14)
    │   └── ota_cluster.[ch]           # OTA-Upgrade-Client 0x0019 (Z16), esp_ota + Rollback
    ├── battery/
    │   ├── battery.[ch]              # Akkuspannung per ADC (Z7), Ruhespannung → %
    │   ├── battery_model.[ch]        # Verbrauchsmodell → Restlaufzeit (Z14), Zähler im NVS
    │   └── battery_config.h          # GPIO, Teiler, Kalibrierfaktor, Intervall, OCV-Tabelle, Modellströme
    ├── handlers/
    │   ├── attribute_handler.[ch]     # Routing eingehender ZCL-Aktionen
    │   └── read_request_handler.[ch]  # Read-Requests loggen, Akkuwerte vorher neu messen/rechnen
    └── light/
        ├── light.[ch]                # Lichtsteuerung: Task, Rampen (Z3), öffentliche API
        ├── light_config.h            # Pins, Rampendauer, Timings, PWM-Parameter
        ├── light_driver.h            # Backend-Schnittstelle light_driver_t
        ├── light_driver_ws2812.c     # Onboard-WS2812 über RMT
        ├── light_driver_debug.c      # Backend DEBUG: WS2812 + PWM parallel
        └── light_driver_pwm.c        # Backend Produktiv: LEDC-PWM + Boost-EN
```

## Schichten

```
┌──────────────────────────────────────────────────────────────┐
│ main.c                                                       │
│  NVS → power_management_init() → light_init() → Zigbee-Task  │
└───────────────┬──────────────────────────────────────────────┘
                │
┌───────────────▼──────────────────────────────────────────────┐
│ zigbee_device.c  (Task "Zigbee_main")                        │
│  esp_zigbee_init → Commissioning-Parameter → Datenmodell     │
│  → esp_zigbee_start(false) → esp_zigbee_launch_mainloop      │
│  zigbee_signal_handler: Pairing-Fenster, Rejoin-Backoff      │
└───────┬───────────────────────────┬──────────────────────────┘
        │ beim Init                 │ Callbacks vom Stack
┌───────▼──────────────────┐ ┌──────▼────────────────────────┐
│ cluster_manager.c        │ │ attribute_handler.c           │
│  ZHA Dimmable Light      │ │  SET_ATTR_VALUE → Cluster-    │
│  + Basic-Identität       │ │  spezifischer Handler         │
│  + power_config_cluster  │ │                               │
└──────────────────────────┘ └──────┬────────────────────────┘
                                    │
                        ┌───────────▼───────────┐
                        │ on_off_cluster.c      │──► light.c (Queue, Task "light")
                        └───────────────────────┘

power_management.c: esp_pm_configure() – Schlaf steuert ESP-IDF, nicht die Anwendung
```

## Startablauf

1. `app_main()` ([main.c](../main/main.c)) initialisiert NVS. Die Partition `nvs` enthält auch die Zigbee-Daten; muss sie wegen eines Layoutwechsels gelöscht werden, ist das Pairing weg.
2. `power_management_init()` aktiviert den automatischen Light Sleep: `esp_pm_configure`, feste 96 MHz.
3. `light_init()` wählt das Licht-Backend anhand von `APP_DEBUG_LIGHT` (DEBUG Level 1), initialisiert es (Licht aus) und startet den Task `light`. `button_init()` richtet den BOOT-Taster ein (GPIO-Interrupt + Light-Sleep-Wakeup auf Low-Pegel, Task `button`, der bis zum Tastendruck blockiert). Danach gibt `power_management_report()` im DEBUG-Build den GPIO-Sleep-Bericht aus.
4. Task `Zigbee_main` (`ZB_TASK_STACK_SIZE` 4096, `ZB_TASK_PRIORITY` 5) führt `zigbee_device_task()` aus ([zigbee_device.c](../main/zigbee_device.c)):
   1. `esp_zigbee_init()`: Rolle ZED, `ed_timeout`, `keep_alive`, Speicher `nvs`, Radio nativ;
   2. Kanalmasken, Signal-Handler, `rx_on_when_idle = false`;
   3. `cluster_manager_register_device()`: Endpoint 1 anlegen und registrieren;
   4. `ezb_zcl_core_action_handler_register(attribute_handler_callback)`;
   4a. `read_request_handler_init()`: APS-Indication-Callback für Read-Requests (siehe [zigbee.md](zigbee.md));
   5. `esp_zigbee_start(false)`, danach `esp_zigbee_launch_mainloop()` (kehrt nicht zurück).
5. Der Stack meldet `SKIP_STARTUP`. Daraufhin folgen BDB-Initialisierung und Pairing bzw. Rejoin, siehe [zigbee.md](zigbee.md#commissioning).

## Threads und Thread-Safety

| Kontext | Darf Zigbee-API aufrufen? |
|---|---|
| Task `Zigbee_main`: Signal-Handler, ZCL-Action-Callbacks | ja, ohne Lock (`esp_zigbee.h`) |
| `esp_timer`-Callback `retry_timer_cb` | nur mit `esp_zigbee_lock_acquire()` (umgesetzt) |
| öffentliche Funktionen `zigbee_device_start_pairing()`, `_factory_reset()`, `_is_joined()`, `on_off_cluster_set_state()` | holen den Lock selbst; **nicht** aus dem Zigbee-Task aufrufen |
| Task `light` | ruft keine Zigbee-API auf |

## Datenfluss

**Eingehend (Coordinator → Gerät):**
1. Z2M sendet z. B. `On`.
2. Der Stack setzt das Attribut `OnOff`.
3. `attribute_handler_callback(EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID)` prüft Endpoint und Rolle und routet nach Cluster-ID.
4. `on_off_cluster_attribute_changed()` ruft `light_set_on(true)` auf. Das ist nur ein Auftrag in die Queue; der Zigbee-Task blockiert nicht.
5. Der Task `light` fährt die Rampe über das gewählte Backend.

**Ausgehend (Gerät → Coordinator):**
1. `on_off_cluster_set_state()` setzt das Attribut unter dem Lock (für den Taster, Z5).
2. Ob daraus ein Report wird, hängt von der Reporting-Konfiguration ab, die Z2M beim Interview einrichtet.

**Akkuwerte (Z7):** Ein `esp_timer` (alle `BATTERY_MEASURE_INTERVAL_MS` = 15 min) ruft `power_config_cluster_update()`. Die Funktion misst ohne Lock (`battery_measure_mv()`, einige ms) und setzt dann die Attribute unter dem Zigbee-Lock. Ob daraus ein Report wird, hängt wie oben von der Reporting-Konfiguration ab.

**Restlaufzeit (Z14):** [battery_model.c](../main/battery/battery_model.c) zählt den Verbrauch in µA·µs. Jede Pegeländerung im Lichttask (`light_output()` in [light.c](../main/light/light.c)) bucht die verstrichene Zeit mit dem bisherigen Strom (`BATTERY_I_IDLE_UA` + `BATTERY_I_LIGHT_FULL_UA` × Pegel/255) und merkt sich den neuen Strom. Das ist nur Arithmetik unter einem Spinlock, ohne zusätzliches Aufwachen. Ein eigener `esp_timer` (stündlich, `BATTERY_MODEL_INTERVAL_MS`) schreibt den Tagesmittelwert fort, speichert alle 6 h im NVS (Namespace `battery`) und ruft `cluster_manager_update_runtime()`. Diese Funktion setzt die beiden Restlaufzeit-Attribute des Clusters `0xFC00` unter dem Zigbee-Lock. Das dritte Attribut (Spannung in mV) setzt `power_config_cluster_update()` bei jeder Akkumessung. Die erste Messung in Ruhe nach dem Start (`power_config_cluster.c`) meldet die Spannung an `battery_model_boot_voltage()`. Ab `BATTERY_FULL_MV` (4,15 V) gilt der Akku als frisch geladen, und der Zähler wird zurückgesetzt.

## Lichtmodul

```
on_off_cluster.c ──light_set_on()/light_fade_to()──► light.c
                                                      │ Queue (Länge 1, neuester Auftrag gewinnt)
                                                      ▼
                                                 Task "light"
                                                      │ light_driver_t
                                  ┌───────────────────┴───────────────────┐
                        APP_DEBUG_LIGHT = 1                   APP_DEBUG_LIGHT = 0
                         (DEBUG Level 1)                    (Level 0 und 2)
                       light_driver_debug.c                     light_driver_pwm.c
                       WS2812 (GPIO 8) + PWM (10/11) LEDC-PWM (GPIO 10) + Boost-EN (GPIO 11)
                       Rampe in Software (20 ms)        Rampe per LEDC-Hardware-Fade
```

- **Schnittstelle** `light_driver_t` ([light_driver.h](../main/light/light_driver.h)): `init`, `power(on)` (Endstufe/Boost, optional), `set_level(0..255)`, `fade(target, ms)` (optional – fehlt sie, rampt `light.c` in Software).
- **Muster:** `light_pattern_start(&pattern)` und `light_pattern_then(&pattern, on)` spielen ein `light_pattern_t` ab (Helligkeit, An-/Aus-Zeit, Anzahl, 0 = endlos) mit kurzen Rampen (`LIGHT_BLINK_RAMP_MS`, 50 ms). `_start` kehrt danach zum zuletzt gesetzten Zustand zurück, `_then` setzt den neuen Zustand `on` und fährt ihn mit `LIGHT_RAMP_MS` an. `off_ms = 0` mit `count = 1` ergibt einen Dauerpuls, nach dem direkt der Endzustand angefahren wird. Ein neuer Auftrag bricht das Muster ab. Genutzt für Identify (Z11) und die Statusanzeige `status_indicator.c` (Z10, Z12).
- **Rampen:** `LIGHT_RAMP_MS` (1 s) gilt für 0 ↔ 100 %; Teilstrecken werden anteilig kürzer. Ein neuer Auftrag bricht eine Software-Rampe ab und setzt am erreichten Wert fort; ein Hardware-Fade läuft zu Ende.
- **Einschalten:** `power(true)` (Boost EN, Settle-Zeit) → Rampe hoch. **Ausschalten:** Rampe auf 0 → `power(false)` (PWM stoppen, Timer anhalten, Boost aus – Z9).
- **Neues Backend:** `light_driver_<name>.c` mit einer `const light_driver_t` anlegen, in `light_driver.h` deklarieren, Auswahl in `light_init()` ([light.c](../main/light/light.c)), Quelle in `main/CMakeLists.txt`.
- Beide Backends werden immer kompiliert; Fehler im gerade nicht gewählten Backend fallen so sofort beim Build auf.

## Konventionen

- Jedes Cluster-Modul bietet `<name>_cluster_add(ep_desc)` (falls der Cluster nicht aus der ZHA-Vorlage kommt) und optional `<name>_cluster_attribute_changed(attribute)` / `<name>_cluster_update(...)`.
- Modulzustand liegt in `static`-Variablen der jeweiligen `.c`-Datei.
- Konfiguration erfolgt über `#define`s in Headern (`zigbee_config.h`, `light_config.h`, `power_management.h`) bzw. über `sdkconfig`.
- Logging ausschließlich über `ESP_LOGx` mit modulspezifischem `TAG`.
