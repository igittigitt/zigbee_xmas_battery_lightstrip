# Zigbee

Gerätemodell und Commissioning der Firmware. Grundlage: esp-zigbee-lib 2.0.5. Das Framework selbst ist in [zigbee-framework.md](zigbee-framework.md) beschrieben, die Konstanten stehen in [zigbee_config.h](../main/zigbee_config.h).

## Geräterolle

**Sleepy End Device** (Ziel Z1): `EZB_NWK_DEVICE_TYPE_END_DEVICE` mit `rx_on_when_idle = false`. Das Gerät routet nicht. Der Parent (Coordinator oder Router) puffert Nachrichten, bis das Gerät sie beim nächsten Aufwachen abholt.

| Konstante | Wert | Bedeutung / Quelle |
|---|---|---|
| `ZB_INSTALLCODE_POLICY` | `false` | Join ohne Install Code (Standard für Z2M) |
| `ZB_ED_TIMEOUT` | `EZB_NWK_ED_TIMEOUT_64MIN` | Der Parent entfernt das Kind nach dieser Zeit ohne Keepalive. Das Gerät meldet `PARENT_LOST`, wenn der Keepalive so lange scheitert (`ezbee/nwk.h`, `ezbee/app_signals.h`) |
| `ZB_KEEP_ALIVE_MS` | 2000 | Keepalive-Intervall. Bestimmt laut FAQ, wie lange der Chip schläft, und damit die Reaktionszeit (Ziel Z4: ≤ ca. 1–2 s). Ob jeder Keepalive ein MAC Data Request ist, also eine Abfrage gepufferter Befehle, ist **[UNBELEGT]** → messen |
| `ZB_RX_ON_WHEN_IDLE` | `false` | Empfänger im Leerlauf aus (FAQ, Light Sleep) |
| `ZB_PRIMARY_CHANNEL_MASK` | `0x07FFF800` | Kanäle 11–26. Ist der Z2M-Kanal bekannt, verkürzt ein einzelner Kanal den Scan beim Pairing (FAQ) |
| `ZB_SECONDARY_CHANNEL_MASK` | `0` | keine zweite Scanrunde |

## Endpoint und Cluster

Endpoint `ZB_ENDPOINT` = **1**, Profil Home Automation (`0x0104`), **Device ID `0x0101` (Dimmable Light)**, seit 2026-11-08; vorher `0x0100` On/Off Light. Erzeugt wird er mit `ezb_zha_create_dimmable_light()`, siehe [cluster_manager.c](../main/clusters/cluster_manager.c). Nach dem Wechsel muss Z2M das Gerät neu interviewen.

| Cluster | ID | Herkunft | Inhalt / Verhalten |
|---|---|---|---|
| Basic | `0x0000` | ZHA-Vorlage | ManufacturerName `zigbee-light`, ModelIdentifier `ZL-LightChain` (`ZB_MANUFACTURER_NAME`/`ZB_MODEL_IDENTIFIER`), SWBuildID z. B. `1.0.0` (aus `ZB_OTA_APP_RELEASE`/`_BUILD`), DateCode = Build-Datum `YYYYMMDD`, PowerSource = `0x03` Battery |
| Identify | `0x0003` | ZHA-Vorlage | Identify → Lichterkette blinkt `ZB_IDENTIFY_DURATION_MS` (2 s) mit `ZB_IDENTIFY_BLINK_HZ` (5 Hz), danach vorheriger Zustand (Z11), [identify_cluster.c](../main/clusters/identify_cluster.c) |
| Groups | `0x0004` | ZHA-Vorlage | Verwaltet der Stack |
| Scenes | `0x0005` | ZHA-Vorlage | Verwaltet der Stack |
| On/Off | `0x0006` | ZHA-Vorlage + Erweiterung | `OnOff` → Licht-Modul (Rampe, Z3). Zusätzlich `StartUpOnOff` (`0x4003`, Default `ZB_START_UP_ON_OFF_DEFAULT` = aus, Z6, in Z2M `power_on_behavior`). `OnOff` und `StartUpOnOff` sind persistent ([on_off_cluster.c](../main/clusters/on_off_cluster.c)) |
| Level Control | `0x0008` | ZHA-Vorlage + Erweiterung | Die Befehle (MoveToLevel, Move, Step, Stop, jeweils auch WithOnOff) verarbeitet der Stack. Jede Änderung von `CurrentLevel` meldet er per `SET_ATTR_VALUE`, wie im 2.0.5-Beispiel `color_dimmable_light`. `CurrentLevel` 0x00–0xFE wird auf die Ein-Helligkeit 1–255 des Licht-Moduls abgebildet (`light_set_on_level()`, Rampe Z3). Ist das Licht aus, wird die Helligkeit nur gespeichert. Zusätzlich `StartUpCurrentLevel` (`0x4000`, Default `ZB_START_UP_CURRENT_LEVEL_DEFAULT` = 0xFF vorherige, Z13). `CurrentLevel` und `StartUpCurrentLevel` sind persistent ([level_cluster.c](../main/clusters/level_cluster.c)) |
| Power Configuration | `0x0001` | [power_config_cluster.c](../main/clusters/power_config_cluster.c) | `BatteryVoltage` (100 mV, abgerundet: 3379 mV → 33 = 3,3 V, damit der Wert nie über der Messung liegt) und `BatteryPercentageRemaining` (0,5 %) aus der Akkumessung (Z7, [battery.c](../main/battery/battery.c)): beim Start und alle 15 min, Ladestand nur bei LED aus und geglättet. Beide Attribute sind meldefähig (`EZB_ZCL_ATTR_ACCESS_REPORTING`). Schlägt die Messung fehl: `0xFF` (ungültig/unbekannt). **DEBUG-Build** ohne `CONFIG_ZL_DEBUG_BATTERY_ADC`: `BatteryPercentageRemaining` = `CONFIG_ZL_DEBUG_BATTERY_PERCENT` × 2 (Default 95 % → 190), `BatteryVoltage` = `0xFF`. Details: [hardware.md](hardware.md#firmware-umsetzung-z7) |
| Restlaufzeit (herstellerspezifisch) | `0xFC00` | [battery_runtime_cluster.c](../main/clusters/battery_runtime_cluster.c) | Eigener Cluster (`ezb_zcl_custom_create_cluster_desc`, `ezbee/zcl/cluster/custom.h`), Attribute ohne Herstellercode, nur lesbar und meldefähig. `0x0000` RemainingOnTime (uint16, 0,1 h): wie lange das Licht mit der aktuellen Ein-Helligkeit noch leuchten kann. `0x0001` RemainingDays (uint16, 0,1 d): Tage bei der durchschnittlichen Nutzung der letzten 7 Tage. `0x0002` BatteryVoltageMv (uint16, 1 mV): letzte Akkumessung in voller Auflösung, gesetzt bei jeder Messung zusammen mit `BatteryVoltage` ([power_config_cluster.c](../main/clusters/power_config_cluster.c)); das Standardattribut kann nur 100-mV-Schritte. **Zellkonfiguration (Z15), beschreibbar:** `0x0010` CellType (enum8: 0 = generisch Li-Ion, 1 = Samsung INR18650-35E), `0x0011` CellCapacity (uint16, mAh je Zelle, 100–20000), `0x0012` CellsParallel (uint8, 1–8). Werte außerhalb lehnt `check_value_cb` mit INVALID_VALUE ab; `write_attr_cb` koppelt Typ und Kapazität, speichert im NVS, setzt den Zähler auf den Ladestand aus der Spannung und schickt die Werte per Report an den Coordinator. `0xFFFF` = unbekannt, die Tage sind das bis zum ersten vollen Tag Laufzeit, die Spannung ohne Messung (DEBUG ohne ADC). Quelle ist das Verbrauchsmodell (Z14, [battery_model.c](../main/battery/battery_model.c)), Aktualisierung stündlich. Die Ströme sind Platzhalter, die Werte also Schätzungen. Z2M zeigt den Cluster nur mit dem External Converter an (siehe unten) |
| OTA Upgrade (**Client**) | `0x0019` | [ota_cluster.c](../main/clusters/ota_cluster.c) | Firmware-Update über Zigbee (Z16). Client-Rolle = Output-Cluster; daran erkennt Z2M, dass das Gerät OTA kann. ManufacturerID `0x131B`, ImageTypeID `0x5A4C`, CurrentFileVersion `ZB_OTA_FILE_VERSION` ([zigbee_config.h](../main/zigbee_config.h)). Ablauf siehe [OTA-Update](#ota-update-z16) |

### Einschaltverhalten (Z6)

- Die ZHA-Vorlage „On/Off Light“ legt nur `OnOff` an. Ein Schreibzugriff von Z2M auf `StartUpOnOff` lief deshalb bisher ins Leere: Das Attribut fehlte, und unser Code erhielt keinen Callback.
- Jetzt ergänzt `on_off_cluster_add()` das Attribut (`ezb_zcl_on_off_cluster_desc_add_attr`) und setzt bei `OnOff` und `StartUpOnOff` das Flag `EZB_ZCL_ATTR_ACCESS_PERSISTENT` („Attribute is stored in the dataset“, `ezbee/zcl/zcl_type.h`; seit 2.0.5 laut Release Notes).
- Beim Signal `SKIP_STARTUP` sind die gespeicherten Daten geladen („Stack framework (scheduler, buffer pool, NVRAM, etc.) startup complete“, `app_signals.h`). Dort liest `on_off_cluster_apply_startup()` beide Werte und schaltet:

| `StartUpOnOff` | Ergebnis |
|---|---|
| `0x00` | aus |
| `0x01` | an |
| `0x02` | Gegenteil des letzten Zustands |
| `0xFF` | letzter Zustand |
| sonstige | aus (eigene Wahl, spart Akku) |

- Ob der Stack von sich aus etwas mit `StartUpOnOff` macht, ist nicht dokumentiert. Deshalb wertet die Anwendung das Attribut selbst aus.
- Ob die Persistenz so wie angenommen funktioniert, ist am Gerät zu prüfen: `power_on_behavior` in Z2M setzen, Gerät neu starten, Log `StartUpOnOff 0x.., previous … -> …` ansehen.

Quellen zu `0xFF`:
- ZBOSS `zb_zcl_power_config.h`: `ZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_INVALID 0xff` und `ZB_ZCL_POWER_CONFIG_BATTERY_REMAINING_UNKNOWN 0xff`.
- zigbee-herdsman-converters `src/converters/fromZigbee.ts` verarbeitet beide Werte nur, wenn sie `< 255` sind.
- Home Assistant zeigt also keinen erfundenen Akkustand.

### Dimmen (Z13), offene Punkte

- **[UNBELEGT]** Ob der Stack bei Befehlen mit Übergangszeit (TransitionTime > 0) bzw. bei Move/Step `CurrentLevel` in Zwischenschritten setzt und dafür die CPU periodisch weckt, ist nicht dokumentiert. Am Gerät prüfen: Helligkeit in Z2M mit `transition` ändern und die Log-Zeilen `Level …` zählen.
- **[UNBELEGT]** Welche Einstellungen Z2M ohne eigenen Converter anbietet (Helligkeit sicher erwartet; `current_level_startup` bzw. `level_config` offen). Nach dem Neu-Interview in Z2M unter „Exposes“ prüfen.

Identify, Groups und Scenes sind Pflicht-Cluster der ZHA-Vorlage „Dimmable Light“. Sie kosten keinen Strom, solange niemand sie benutzt.

> Änderungen an Endpoint, Device ID, Clusterliste, Manufacturer oder Model erfordern in Z2M ein erneutes Interview.

## Commissioning

Umgesetzt in [zigbee_device.c](../main/zigbee_device.c), Signal-Handler `zigbee_signal_handler()`:

```
esp_zigbee_start(false)
        │
SKIP_STARTUP ──► BDB INITIALIZATION
                        │
        DEVICE_FIRST_START / DEVICE_REBOOT
          │ Fehler            │ OK + factory new          │ OK + Netz gespeichert
          ▼                   ▼                            ▼
   Rejoin-Backoff      Pairing-Fenster (3 min)        verbunden
                              │
                        NETWORK STEERING
                       │ OK          │ Fehler
                       ▼             ▼
                   verbunden    vor Fristende: nach 1 s erneut
                                nach Fristende: Suche beendet

PARENT_LOST ──► Rejoin-Backoff (BDB INITIALIZATION, 1 s … 5 min, verdoppelnd, ohne Ende)
LEAVE (Reset, z. B. "Remove" in Z2M) ──► Pairing-Fenster
```

- **Pairing-Fenster** (Ziel Z10): Netzsuche für `ZB_PAIRING_TIMEOUT_MS` = 3 min. Bis dahin wird nach jedem Fehlversuch nach `ZB_STEERING_RETRY_MS` = 1 s erneut gesucht. Danach sucht das Gerät nicht mehr, weil Steering ohne Netz den Akku leert (goals.md, Out of Scope „Endloses Network Steering“). Ein neues Fenster öffnet der lange Tastendruck (Z5, Factory Reset); `zigbee_device_start_pairing()` steht zusätzlich als API bereit.
- **Rejoin-Backoff:** Beim Neustart mit gespeichertem Netz, bei Fehlern in der BDB-Initialisierung und bei `PARENT_LOST` startet erneut `EZB_BDB_MODE_INITIALIZATION`, im Abstand von 1 s, 2 s, 4 s … bis höchstens 5 min, ohne Zeitlimit.
  - Die Werte `ZB_REJOIN_RETRY_MIN_MS`/`_MAX_MS` sind **eigene Wahl**, nicht aus einer Quelle.
  - Dass BDB INITIALIZATION die richtige Reaktion auf `PARENT_LOST` ist, ist nicht dokumentiert. Grundlage ist der Eintrag in den 2.0.5-Release-Notes: „Extended BDB initialization process to try secure rejoin and TC rejoin on all the network channel“. Auf Hardware prüfen.
- **Factory Reset:** `zigbee_device_factory_reset()` löscht die Zigbee-Daten und startet neu (`esp_zigbee_factory_reset()`). Danach öffnet sich das Pairing-Fenster. Ausgelöst durch den langen Tastendruck (Z5, [button.c](../main/button.c)), in jedem Zustand – auch während eines endlosen Rejoins.

## Eingehende Aktionen

[attribute_handler.c](../main/handlers/attribute_handler.c) ist per `ezb_zcl_core_action_handler_register()` registriert:

| Callback-ID | Behandlung |
|---|---|
| `EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID` | Prüft Status, Endpoint und Server-Rolle; On/Off → `on_off_cluster_attribute_changed()`, Identify → `identify_cluster_attribute_changed()`, andere Cluster nur Debug-Log |
| `EZB_ZCL_CORE_IDENTIFY_EFFECT_CB_ID` | `identify_cluster_trigger_effect()` → abgelehnt mit `UNSUP_CMD` (0x81), Licht unverändert. Lichteffekte sind out of scope ([goals.md](goals.md)) |
| `EZB_ZCL_CORE_DEFAULT_RSP_CB_ID` | Debug-Log |
| sonstige | Info-Log `ZCL action 0x....` (vorübergehend, für die Hardware-Tests) |

**Identify:** Ein Identify-Befehl setzt das Attribut `IdentifyTime`. Ich glaube es könnte so sein: Der Stack meldet das als `SET_ATTR_VALUE` und eventuell auch das sekündliche Herunterzählen. Belegt ist das nicht. Deshalb startet das Blinken nur, wenn der Wert steigt; fallende Werte lösen es nicht erneut aus. Im Log erscheint `IDENTIFY_CLUSTER: IdentifyTime N s`. Fehlt diese Zeile beim Identify aus Z2M, kommt der Befehl auf einem anderen Weg an; dann die `ZCL action`-Zeilen ansehen.

Lokales Schalten (Taster, Z5) läuft über `on_off_cluster_set_state()`. Die Funktion setzt das Attribut unter dem Zigbee-Lock und schickt danach sofort einen **Report Attributes** (`ezb_zcl_report_attr_cmd_req`) für OnOff direkt an den Coordinator (Kurzadresse 0x0000, Endpoint 1, `ZB_COORDINATOR_*` in `zigbee_config.h`).

- **Grund:** Nur mit `ezb_zcl_set_attr_value()` kam ein Tastendruck in HA nicht an (beobachtet am 2026-11-10). Ob der Setter allein einen Report auslöst, ist in esp-zigbee-sdk 2.0.5 nicht dokumentiert. Weder `zcl_common.h` noch `docs/en/user-guide/zcl_general_report.rst` sagen dazu etwas.
- **Endpoint 1:** zigbee-herdsman registriert Endpoint 1 als HA-Endpoint des Coordinators. Belegt ist das so:
  - ember: `src/adapter/ember/adapter/endpoints.ts`, `FIXED_ENDPOINTS`, „primary network“ `endpoint: 1`.
  - z-stack: `src/adapter/z-stack/adapter/endpoints.ts`, erster Eintrag `endpoint: 1, appprofid: 0x0104`.
  - deconz: Fallback `[1]`.
- **Auswertung in Z2M:** `fz.on_off` verarbeitet `attributeReport` für `genOnOff` (zigbee-herdsman-converters `src/converters/fromZigbee.ts`). Alle Quellen wurden am 2026-11-10 auf dem Branch master gelesen.
- **Einschränkung:** Laut `zcl_general_cmd.h` hängt der Inhalt des Reports von der Reporting-Konfiguration ab. Fehlt dafür der Eintrag, schreibt die Firmware eine Warnung ins Log. Ob der Report auf der Hardware ankommt, ist **ungetestet**.

## Zigbee2MQTT

Coordinator: Z-Stack 3.x.0 auf SLZB-06P10.

1. Nach dem Flashen der 2.x-Firmware einmalig `idf.py -p COM5 erase-flash`. Die 1.x-Netzdaten lagen in einer Partition, die es nicht mehr gibt (siehe [zigbee-framework.md](zigbee-framework.md#persistenz)). Ein vorhandener Eintrag des alten Geräts in Z2M sollte vorher entfernt werden.
2. In Z2M „Permit join“ aktivieren und das Gerät starten. Das Pairing-Fenster ist 3 min offen.
3. Das Gerät meldet sich als `zigbee-light` / `ZL-LightChain`.

Für diese Kennung gibt es keinen offiziellen Z2M-Converter. Ohne eigenen Converter zeigt Z2M „Generated support“ („Automatically generated definition“). Dann erscheinen nur Licht, Identify, Effect und Akku-%. Spannung und Restlaufzeit (Cluster `0xFC00`) fehlen (am Gerät beobachtet am 2026-11-08).

### Z2M External Converter

Der Converter liegt im Repo unter [z2m/zl-lightchain.mjs](../z2m/zl-lightchain.mjs). Er stellt bereit: Licht (An/Aus, Helligkeit, Einschaltverhalten; ohne „Effect“, `light({effect: false})`), Identify, Akku-% (Standardcluster), `voltage` in mV aus `0xFC00`/`0x0002` sowie die Sensoren `remaining_light_hours` (h) und `remaining_days` (d). Unter Konfiguration: `battery_cell_type` (Auswahl, `enumLookup`), `battery_cell_capacity` (mAh) und `battery_cells_parallel` (beide `numeric` mit `access: 'ALL'`; Felder laut `EnumLookupArgs`/`NumericArgs` in modernExtend.ts, gelesen am 2026-11-10). In HA erscheinen sie als Entitäten beim Gerät.

Installation laut [Z2M-Doku „External converters“](https://www.zigbee2mqtt.io/advanced/more/external_converters.html):
1. Die Datei in den Ordner `external_converters` legen. Er liegt neben der `configuration.yaml` von Z2M.
2. Ab Z2M 2.11.0 sind External Converter in neuen Installationen standardmäßig aus. In dem Fall `advanced.enable_external_js` aktivieren.
3. Z2M neu starten. Danach das Gerät **neu interviewen** (die Clusterliste hat sich geändert) und „Reconfigure“ ausführen, damit Z2M das Reporting einrichtet. Das Gerät muss dabei wach sein, z. B. kurz nach einem Neustart.

Schritt-für-Schritt-Anleitung für Anwender: [README.md](../README.md#einrichtung-in-zigbee2mqtt--home-assistant).

**Am Gerät bestätigt (2026-11-08, Z2M als HA-Add-on):**
- Der Ordner `external_converters` existierte nicht und musste angelegt werden. `external_extensions` ist ein anderer Mechanismus.
- `advanced.enable_external_js: true` war nötig.
- Log nach dem Neustart: `Loaded external converter 'zl-lightchain.mjs'.` Vorher (debug): `External JS folder 'external_converters' is outside the Z2M install dir, creating a symlink to 'node_modules'`.
- Danach zeigt Z2M „External support“. Unter *Exposes* stehen Battery, Voltage, „Restlaufzeit Licht an“ und „Restlaufzeit“. Der Converter wird also ohne Fehler geladen, auch mit `deviceAddCustomCluster` und `numeric()` mit `cluster`/`attribute`.
- Vor „Reconfigure“ zeigten Voltage und beide Restlaufzeiten `N/A`. Ob danach die Werte ankommen und richtig skaliert sind, ist noch offen.

Grundlage der Converter-Funktionen ist `src/lib/modernExtend.ts` von zigbee-herdsman-converters (Branch master, gelesen am 2026-11-08). Daraus belegt sind `deviceAddCustomCluster`, die Optionen von `battery()`, `light()` und `identify()` sowie das Format von `reporting` (`min`/`max`/`change`, Zeitangaben wie `"1_HOUR"`). Nur aus einer Zusammenfassung des Quelltexts stammen und sind deshalb **[UNBELEGT]**:
- die Feldnamen `cluster`/`attribute` von `numeric()` (Laden und Exposes am 2026-11-08 bestätigt, Werte noch nicht),
- dass `scale: 10` den Rohwert durch 10 teilt,
- ~~dass `numeric()` den Wert `0xFFFF` nicht ausfiltert~~ Widerlegt (2026-11-11, Z2M-Log `Exception while calling fromZigbee converter: Value is not a number, got number (NaN)`, `remaining_days` fehlte im Payload). zigbee-herdsman dekodiert den ZCL-Ungültigwert eines uint16 (`0xFFFF`) als `NaN` (`src/zspec/zcl/utils.ts`, `ZCL_TYPE_INVALID_BY_TYPE`, Branch master, gelesen am 2026-11-11). Der Standard-Converter von `numeric()` wirft dann in `assertNumber` (`src/lib/modernExtend.ts`). Deshalb haben `voltage`, `remaining_light_hours` und `remaining_days` im Converter ein eigenes `fzConvert` (`unknownAsNull`), das `NaN` als `null` meldet. HA zeigt dann „unbekannt“. Am Gerät bestätigt (2026-11-11): Payload `"remaining_days": null`, keine Exception mehr.

Prüfen, wenn das Gerät gepairt ist.

**Akku-Reporting** (`battery()` in modernExtend.ts, verbatim gelesen am 2026-11-08):
- Standard ist `{min: "1_HOUR", max: "MAX", change: 10}` mit `MAX` = 65000 s. Der Converter setzt für den Ladestand eigene Werte: min 1 h, max 4 h, Änderung 1 % (`change: 2`, Einheit 0,5 %). Eigene Wahl; die Funkkosten eines Reports sind nicht gemessen.
- Spannung: `battery({voltage: false, voltageReporting: false})`. Mit `voltage: false` legt `battery()` kein `voltage`-Expose an und veröffentlicht keinen `voltage`-Schlüssel aus `batteryVoltage`; Voltage-Reporting wird nur bei `voltageReporting: true` eingerichtet (modernExtend.ts, gelesen am 2026-11-10). Stattdessen liefert `numeric({name: 'voltage', …})` den mV-Wert aus `0xFC00`/`0x0002`, Reporting min 1 h, max 4 h, Änderung 10 mV (eigene Wahl). Ein bereits auf dem Gerät eingerichtetes Reporting für `batteryVoltage` bleibt bestehen, wird aber nicht mehr angezeigt. **Reihenfolge im Converter:** `numeric('voltage')` muss vor `battery()` stehen. Z2M nimmt für ein `/get` den ersten toZigbee-Converter, dessen `key` passt (`converters.find` in zigbee2mqtt `lib/extension/publish.ts`), und hängt die Extends in Array-Reihenfolge an (`processExtensions` in zigbee-herdsman-converters `src/index.ts`, beide gelesen am 2026-11-10). Der Converter von `battery()` hat den Key `["battery", "voltage"]`. Stand er vorne, las ein Voltage-Refresh nur `genPowerCfg.batteryVoltage`, und der angezeigte Wert blieb alt (am Gerät beobachtet am 2026-11-10).
- Beim Configure (Interview/„Reconfigure“) liest Z2M beide Attribute einmal (`setupAttributes` mit `read`). Lesefehler verschluckt Z2M still (nur Debug-Log `Reading attribute failed`). Werte `0xFF` verwirft der Converter (`< 255`).
- **Read-Requests** (Refresh in Z2M/HA, `convertGet`): [read_request_handler.c](../main/handlers/read_request_handler.c) sieht jeden eingehenden Frame vor dem Stack (`ezb_apsde_data_indication_handler_register`, `ezbee/aps.h`: Rückgabe `false` = der Stack verarbeitet ihn danach). Bei *Read Attributes* (globaler Befehl `0x00`, Header nach ZCL-Spezifikation 07-5123 Kap. 2.4.1) auf Endpoint 1 loggt er Cluster und jedes Attribut (Tag `READ_REQUEST`). Messwerte (`0x0001`/`0x0020`, `0x0021`, `0xFC00`/`0x0002`) werden vorher neu gemessen, berechnete Werte (`0xFC00`/`0x0000`, `0x0001`) neu berechnet; die Antwort des Stacks enthält dann die frischen Werte. Alles andere (z. B. `OnOff`, `CurrentLevel`) ist ein Zustand und wird so beantwortet, wie es gespeichert ist. Die Messung blockiert den Zigbee-Task einige ms (16 ADC-Samples). Ohne ADC (DEBUG) bleibt es beim gespeicherten Wert.
- Manuell lesen: in Z2M unter *Exposes* beim Akku auf „Refresh“ klicken (`convertGet` liest `batteryPercentageRemaining` und `batteryVoltage`, letzteres auch mit `voltage: false`, ohne Anzeige). Die mV-Spannung hat ein eigenes „Refresh“.
- In HA sind `battery` und `voltage` Diagnose-Entitäten (`withCategory("diagnostic")` bzw. `entityCategory: 'diagnostic'`), auf der Geräteseite im Abschnitt „Diagnose“.

### Gerätebild

Z2M hat für `ZL-LightChain` kein offizielles Gerätebild und zeigt einen Platzhalter. Ein eigenes Bild geht nur **pro Gerät** über die Geräteoption `icon` in der `configuration.yaml` von Z2M ([Z2M-Doku „Devices and Groups“](https://www.zigbee2mqtt.io/guide/configuration/devices-groups.html), gelesen am 2026-11-11). Im Converter lässt es sich nicht hinterlegen: `Definition` in zigbee-herdsman-converters `src/lib/types.ts` hat kein Feld `icon` (Branch master, gelesen am 2026-11-11).

```yaml
devices:
  '0x<ieee-adresse>':
    friendly_name: Lichterkette
    icon: device_icons/zl-lightchain.png
```

- Erlaubt sind laut Doku ein Pfad im Ordner `device_icons` neben der `configuration.yaml` (andere Ordner nicht) oder eine vollständige URL.
- **Eintragen über das Z2M-Frontend**, nicht von Hand in der YAML: Gerät → Einstellungen → Feld `icon` → speichern. So am Gerät bestätigt (2026-11-11, Z2M als HA-Add-on). Von Hand bei laufendem Z2M eingetragene `icon`-Zeilen waren nach einem Neustart wieder aus der `configuration.yaml` verschwunden. Ich glaube es könnte so sein: Z2M schreibt die Datei aus seinem Speicherstand neu. Wer von Hand editiert, Z2M vorher stoppen.
- Hochladen nach `device_icons` über das HA-Add-on „File editor“ scheiterte mit HTTP 413 (Datei zu groß; Grenze **[UNBELEGT]**). Abhilfe: Bild verkleinern oder per Samba share / Studio Code Server kopieren.
- Z2M-Frontend → Werkzeuge → „Gerätebilder lokalisieren“ meldet für `ZL-LightChain`-Geräte „Error“ (beobachtet 2026-11-11). Ich glaube es könnte so sein: Die Funktion lädt offizielle Bilder von zigbee2mqtt.io, und für dieses Modell gibt es keins. Ob sie ein eigenes `icon` überschreibt, ist **[UNBELEGT]**.
- Bildformat: Für `icon` nennt die Doku keine Anforderungen **[UNBELEGT]**: welche Dateitypen gehen, Höchstgröße, Anzeigegröße. Offizielle Gerätebilder müssen PNG, 512 × 512 px und mit transparentem Hintergrund sein ([Z2M-Doku „Support new devices“, Abschnitt 5](https://www.zigbee2mqtt.io/advanced/support-new-devices/01_support_new_devices.html)). Eigene Wahl: dieselben Werte für das eigene Bild.
- Das Bild erscheint im Z2M-Frontend. Ein Weg, ein Gerätebild auf die HA-Geräteseite zu bringen, ist nicht bekannt. Über `homeassistant:` (gleiche Doku) lassen sich nur die Icons der Entitäten ändern (z. B. `icon: mdi:string-lights`).
- Die Update-Entität „Firmware“ (siehe [OTA-Update](#ota-update-z16)) zeigt in HA das Z2M-Logo. Laut einer Zusammenfassung von zigbee2mqtt `lib/extension/homeassistant.ts` setzt Z2M dort fest `entity_picture` auf `…/images/logo.png`; verbatim nicht gelesen, **[UNBELEGT]**.

### Gerät neu anlernen

`idf.py -p COM5 erase-flash` löscht auch die NVS-Partition und damit das Pairing. Später geht das per Taster (Z5) mit `zigbee_device_factory_reset()`.

## OTA-Update (Z16)

Umgesetzt am 2026-11-11, **auf Hardware ungetestet** (offene Punkte: [known-issues.md](known-issues.md), Punkt 20).

### Ablauf

1. In HA an der Entität „Firmware“ (Typ `update`) auf **Installieren** klicken. Z2M legt sie an, weil der Converter `ota: true` setzt (zigbee2mqtt `lib/extension/homeassistant.ts`). Der Button sendet `zigbee2mqtt/bridge/request/device/ota_update/update`.
2. Z2M schickt ein **Image Notify** und wartet **60 s** auf einen QueryNextImageRequest des Geräts (zigbee-herdsman `src/controller/model/device.ts`, `#notifyOta`). Das Gerät holt das Image Notify beim nächsten Poll beim Parent ab. Ob das mit dem 2-s-Keepalive klappt, ist [UNBELEGT]. Antwortet das Gerät nicht, meldet Z2M „Device didn't respond to OTA request“.
3. Der Stack lädt die Datei blockweise. [ota_cluster.c](../main/clusters/ota_cluster.c) prüft Kennung, Herstellercode und Image-Typ im Dateikopf und schreibt das Sub-Element „Upgrade Image“ (Tag `0x0000`) mit `esp_ota_write` in den freien OTA-Slot. Währenddessen steht der Keepalive auf `ZB_OTA_KEEP_ALIVE_MS`. HA zeigt den Fortschritt (Z2M meldet ihn alle 30 s, Z2M-Doku „OTA updates“).
4. Nach dem Download prüft `esp_ota_end` das Image („validate newly written app image“, ESP-IDF API-Referenz `esp_ota_end`). `esp_ota_set_boot_partition` schaltet um, und nach `ZB_OTA_REBOOT_DELAY_MS` startet das Gerät neu.
5. Das neue Image startet im Zustand PENDING_VERIFY (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Nach dem Rejoin bestätigt es sich (`ota_cluster_confirm_image()` in `on_network_joined`). Ohne Join und nach einem Reset läuft wieder die alte Firmware: „otherwise the application will be rolled back upon reboot“ (ESP-IDF-Doku [OTA, App rollback](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32h2/api-reference/system/ota.html)).

Ein Download wird abgelehnt, wenn die letzte Akkumessung unter `BATTERY_WARN_MV` (3,3 V) liegt. Das Gerät fragt nie von sich aus nach Updates. Es gibt also kein zusätzliches Wecken (Entscheidung 2026-11-11).

Nach dem Update gilt `StartUpOnOff` (Z6): Mit dem Default „aus“ ist ein vorher eingeschaltetes Licht danach aus.

### Versionsnummer in HA

- **Entität „Firmware“** (Abschnitt *Konfiguration* auf der Geräteseite): „Installierte Version“ und „Neueste Version“ sind `update.installed_version`/`latest_version` aus dem Z2M-Status. Das ist die 32-Bit-`fileVersion` als **Dezimalzahl** (`installed_version: state.current.fileVersion`, zigbee2mqtt `lib/extension/otaUpdate.ts`). Beispiel: `0x10002000` (aktueller Stand) erscheint als 268443648. Die Z2M-Oberfläche (Seite *OTA*) zerlegt die Zahl dagegen: Release-Byte als `Nibble.Nibble`, Build dezimal, also „App: 1.0 build 0, Stack: 2.0 build 0“ (`formatOtaFileVersion` in zigbee2mqtt-windfront `src/components/ota-page/index.ts`, gelesen am 2026-11-11). Deshalb ist `ZB_OTA_APP_RELEASE` `0x10` für Version 1.0. Solange Z2M noch nicht nach Updates gesucht hat, steht dort `-1` (`latest_version == null` → `-1`, `lib/extension/homeassistant.ts`; beide gelesen am 2026-11-11, Branch master). Abhilfe: in Z2M „Check for update“ beim Gerät.
- **Gerätekarte, Feld „Firmware“**: Z2M übernimmt das Basic-Attribut SWBuildID (`0x4000`) als `sw_version` (`homeassistant.ts`). Die Firmware setzt es seit 2026-11-11 auf `<Major>.<Minor>.<Build>` aus den Nibbles von `ZB_OTA_APP_RELEASE` und `ZB_OTA_APP_BUILD`, z. B. „1.0.0“ (`ZB_SW_BUILD_ID_FORMAT` in [zigbee_config.h](../main/zigbee_config.h)), passend zu „App: 1.0 build 0“. DateCode (`0x0006`, auf der Z2M-OTA-Seite unter der Firmware-ID) ist das Build-Datum `YYYYMMDD` aus `esp_app_get_description()->date`. Das ist das `__DATE__` beim Übersetzen von `esp_app_desc.c` (ESP-IDF `components/esp_app_format`). Ich glaube es könnte so sein: Bei einem inkrementellen Build ohne Änderung an dieser Datei bleibt das Datum alt. Für OTA-Releases deshalb frisch bauen (`fullclean`). Z2M liest es beim Interview. Nach einem OTA-Update liest es `swBuildId` neu (`dateCode`/`swBuildId`-Read in `otaUpdate.ts`). Ob HA die Gerätekarte danach ohne Neustart aktualisiert, ist [UNBELEGT].

### OTA-Datei bauen

1. In [zigbee_config.h](../main/zigbee_config.h) `ZB_OTA_APP_BUILD` (oder `ZB_OTA_APP_RELEASE`) erhöhen. Z2M bietet ein Image nur an, wenn dessen `fileVersion` **größer** ist als die des Geräts (zigbee-herdsman `checkOta`). Gleiche Version wird nicht unterstützt (Z2M-Doku).
2. Produktiv-Firmware bauen (`build_release`, siehe [setup-build.md](setup-build.md#debug-schalter)).
3. Die Zigbee-OTA-Datei erzeugen mit `tools/image_builder_tool/image_builder_tool.py` aus [esp-zigbee-sdk @ e2fdf4ad](https://github.com/espressif/esp-zigbee-sdk/tree/e2fdf4ad/tools/image_builder_tool) (Python-Script, nicht Teil der Komponente):

   ```powershell
   python image_builder_tool.py -m 0x131B -i 0x5A4C -v 0x10012000 --tag 0x0000 build_release/zigbee_node.bin
   ```

   `-v` = neue `ZB_OTA_FILE_VERSION` (Beispiel: App-Release 0x10 = „1.0“, Build 1, Stack 0x20/0x00). `--tag` erwartet Tag-ID und Datei **durch Leerzeichen getrennt**, so steht es im argparse des Scripts. Die SDK-Doku `zcl_ota_upgrade.rst` zeigt `0x0000:datei`; das passt nicht zum Script. Ergebnis: `131B-5A4C-10012000-ota-file.zigbee`.

### Z2M einrichten

1. Die `.zigbee`-Datei ins Z2M-Datenverzeichnis legen (neben `configuration.yaml`).
2. Daneben einen OTA-Index `zl-ota-index.json` anlegen. Das Format entspricht dem Index von [Koenkk/zigbee-OTA](https://github.com/Koenkk/zigbee-OTA/blob/master/README.md). Ist `url` ein lokaler Pfad, liest zigbee-herdsman fehlende Felder (`imageType`, `manufacturerCode`, `fileVersion`) aus dem Dateikopf (`getOtaIndex` in `src/controller/helpers/ota.ts`; aus dem Quelltext gelesen, nicht getestet):

   ```json
   [
     {
       "url": "131B-5A4C-10012000-ota-file.zigbee",
       "modelId": "ZL-LightChain"
     }
   ]
   ```

   `modelId` begrenzt das Image auf dieses Gerät, weil `0x131B` der Espressif-Code ist.
3. In der `configuration.yaml` eintragen und Z2M neu starten (Option mit `requiresRestart`):

   ```yaml
   ota:
     zigbee_ota_override_index_location: zl-ota-index.json
     # optional, Default 50, Maximum 100 (Bytes je Block); größer = schneller, ungetestet
     # default_maximum_data_size: 100
   ```

4. Nach der Erstinstallation der OTA-Firmware das Gerät **neu interviewen**, denn die Clusterliste hat sich geändert (neuer Output-Cluster `0x0019`). Erst dann erscheint die Update-Entität. In HA bzw. Z2M „Auf Update prüfen“ ausführen. Danach zeigt die Entität installierte und neueste Version.

**Dauer:** nicht gemessen. Rechnung (640 KiB, 50 B je Block, 250 ms Z2M-Blockverzögerung `image_block_response_delay`, ohne Funkfehler): ≥ 55 min. Zum Vergleich das Espressif-Beispiel (Router, 223 B je Block): ≈ 5,5 min für 501 KB (Log im README von `examples/ota_upgrade/ota_client`).
