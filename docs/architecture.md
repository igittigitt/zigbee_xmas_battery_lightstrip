# Architektur

Stand: esp-zigbee-lib 2.0.5 / ESP-IDF 5.5.4 (siehe [zigbee-framework.md](zigbee-framework.md)).

## Verzeichnisstruktur

```
zigbee-light/
├── CMakeLists.txt              # Projekt "zigbee_node"
├── sdkconfig.defaults          # Versionierte Kconfig-Defaults (ZED, Light Sleep, Partitionen)
├── partitions.csv              # nvs (inkl. Zigbee-Daten), phy_init, factory, zb_fct
├── dependencies.lock           # Gelockte Komponenten-Versionen
├── docs/                       # Diese Dokumentation
└── main/
    ├── CMakeLists.txt          # Quellliste der Komponente "main"
    ├── idf_component.yml       # Abhängigkeiten (esp-zigbee-lib ==2.0.5, idf ~5.5.4)
    ├── Kconfig.projbuild       # Menü "Zigbee Light": Schalter ZL_DEBUG
    ├── app_config.h            # APP_DEBUG (0/1) aus CONFIG_ZL_DEBUG
    ├── main.c                  # app_main: NVS, Power Mgmt, Licht, Zigbee-Task
    ├── zigbee_config.h         # Zentrale Zigbee-Konstanten (Identität, Keepalive, Commissioning)
    ├── zigbee_device.[ch]      # Stack-Init, Commissioning (Pairing, Rejoin), Signal-Handler, Task
    ├── power_management.[ch]   # esp_pm (automatischer Light Sleep), DEBUG-Sleep-Bericht
    ├── clusters/
    │   ├── cluster_manager.[ch]       # Baut Endpoint (ZHA Dimmable Light) und registriert ihn
    │   ├── on_off_cluster.[ch]        # On/Off ↔ Licht-Modul, StartUpOnOff (Z6)
    │   ├── identify_cluster.[ch]      # Identify → Blinken (Z11)
    │   └── power_config_cluster.[ch]  # Power Configuration (Akku, Z7)
    ├── handlers/
    │   └── attribute_handler.[ch]     # Routing eingehender ZCL-Aktionen
    └── light/
        ├── light.[ch]                # Lichtsteuerung: Task, Rampen (Z3), öffentliche API
        ├── light_config.h            # Pins, Rampendauer, Timings, PWM-Parameter
        ├── light_driver.h            # Backend-Schnittstelle light_driver_t
        ├── light_driver_ws2812.c     # Backend DEBUG: Onboard-WS2812 über RMT
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
3. `light_init()` wählt das Licht-Backend anhand von `APP_DEBUG`, initialisiert es (Licht aus) und startet den Task `light`. Danach gibt `power_management_report()` im DEBUG-Build den GPIO-Sleep-Bericht aus.
4. Task `Zigbee_main` (`ZB_TASK_STACK_SIZE` 4096, `ZB_TASK_PRIORITY` 5) führt `zigbee_device_task()` aus ([zigbee_device.c](../main/zigbee_device.c)):
   1. `esp_zigbee_init()`: Rolle ZED, `ed_timeout`, `keep_alive`, Speicher `nvs`, Radio nativ;
   2. Kanalmasken, Signal-Handler, `rx_on_when_idle = false`;
   3. `cluster_manager_register_device()`: Endpoint 1 anlegen und registrieren;
   4. `ezb_zcl_core_action_handler_register(attribute_handler_callback)`;
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

Akkuwerte folgen mit Z7.

## Lichtmodul

```
on_off_cluster.c ──light_set_on()/light_fade_to()──► light.c
                                                      │ Queue (Länge 1, neuester Auftrag gewinnt)
                                                      ▼
                                                 Task "light"
                                                      │ light_driver_t
                                  ┌───────────────────┴───────────────────┐
                            APP_DEBUG = 1                           APP_DEBUG = 0
                       light_driver_ws2812.c                    light_driver_pwm.c
                       RMT → WS2812 (GPIO 8)        LEDC-PWM (GPIO 10) + Boost-EN (GPIO 11)
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
