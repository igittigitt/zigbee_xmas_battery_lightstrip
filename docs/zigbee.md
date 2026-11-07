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

Endpoint `ZB_ENDPOINT` = **1**, Profil Home Automation (`0x0104`), **Device ID `0x0100` (On/Off Light)**. Erzeugt wird er mit `ezb_zha_create_on_off_light()`, siehe [cluster_manager.c](../main/clusters/cluster_manager.c).

| Cluster | ID | Herkunft | Inhalt / Verhalten |
|---|---|---|---|
| Basic | `0x0000` | ZHA-Vorlage | ManufacturerName `zigbee-light`, ModelIdentifier `ZL-LightChain` (`ZB_MANUFACTURER_NAME`/`ZB_MODEL_IDENTIFIER`), PowerSource = `0x03` Battery |
| Identify | `0x0003` | ZHA-Vorlage | Identify/TriggerEffect → Lichterkette blinkt `ZB_IDENTIFY_DURATION_MS` (2 s) mit `ZB_IDENTIFY_BLINK_HZ` (5 Hz), danach vorheriger Zustand (Z11), [identify_cluster.c](../main/clusters/identify_cluster.c) |
| Groups | `0x0004` | ZHA-Vorlage | Verwaltet der Stack |
| Scenes | `0x0005` | ZHA-Vorlage | Verwaltet der Stack |
| On/Off | `0x0006` | ZHA-Vorlage + Erweiterung | `OnOff` → Licht-Modul (Rampe, Z3). Zusätzlich `StartUpOnOff` (`0x4003`, Default `ZB_START_UP_ON_OFF_DEFAULT` = aus, Z6, in Z2M `power_on_behavior`). `OnOff` und `StartUpOnOff` sind persistent ([on_off_cluster.c](../main/clusters/on_off_cluster.c)) |
| Power Configuration | `0x0001` | [power_config_cluster.c](../main/clusters/power_config_cluster.c) | `BatteryVoltage` und `BatteryPercentageRemaining` = `0xFF` (ungültig/unbekannt), bis die Akkumessung (Z7) umgesetzt ist. **DEBUG-Build:** `BatteryPercentageRemaining` = `CONFIG_ZL_DEBUG_BATTERY_PERCENT` × 2 (Default 95 % → 190), einstellbar per `menuconfig` → *Zigbee Light* |

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

Identify, Groups und Scenes sind Pflicht-Cluster der ZHA-Vorlage „On/Off Light“. Sie kosten keinen Strom, solange niemand sie benutzt.

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

- **Pairing-Fenster** (Ziel Z10): Netzsuche für `ZB_PAIRING_TIMEOUT_MS` = 3 min. Bis dahin wird nach jedem Fehlversuch nach `ZB_STEERING_RETRY_MS` = 1 s erneut gesucht. Danach sucht das Gerät nicht mehr, weil Steering ohne Netz den Akku leert (goals.md, Out of Scope „Endloses Network Steering“). `zigbee_device_start_pairing()` öffnet ein neues Fenster; das ist für den Taster (Z5) vorgesehen.
- **Rejoin-Backoff:** Beim Neustart mit gespeichertem Netz, bei Fehlern in der BDB-Initialisierung und bei `PARENT_LOST` startet erneut `EZB_BDB_MODE_INITIALIZATION`, im Abstand von 1 s, 2 s, 4 s … bis höchstens 5 min, ohne Zeitlimit.
  - Die Werte `ZB_REJOIN_RETRY_MIN_MS`/`_MAX_MS` sind **eigene Wahl**, nicht aus einer Quelle.
  - Dass BDB INITIALIZATION die richtige Reaktion auf `PARENT_LOST` ist, ist nicht dokumentiert. Grundlage ist der Eintrag in den 2.0.5-Release-Notes: „Extended BDB initialization process to try secure rejoin and TC rejoin on all the network channel“. Auf Hardware prüfen.
- **Factory Reset:** `zigbee_device_factory_reset()` löscht die Zigbee-Daten und startet neu (`esp_zigbee_factory_reset()`). Danach öffnet sich das Pairing-Fenster. Vorgesehen für den langen Tastendruck (Z5).
- Visuelle Anzeige des Pairing-Modus (Z10) und Taster (Z5) sind **noch nicht** umgesetzt.

## Eingehende Aktionen

[attribute_handler.c](../main/handlers/attribute_handler.c) ist per `ezb_zcl_core_action_handler_register()` registriert:

| Callback-ID | Behandlung |
|---|---|
| `EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID` | Prüft Status, Endpoint und Server-Rolle; On/Off → `on_off_cluster_attribute_changed()`, Identify → `identify_cluster_attribute_changed()`, andere Cluster nur Debug-Log |
| `EZB_ZCL_CORE_IDENTIFY_EFFECT_CB_ID` | `identify_cluster_trigger_effect()` → Blinken |
| `EZB_ZCL_CORE_DEFAULT_RSP_CB_ID` | Debug-Log |
| sonstige | Info-Log `ZCL action 0x....` (vorübergehend, für die Hardware-Tests) |

**Identify:** Ein Identify-Befehl setzt das Attribut `IdentifyTime`. Ich glaube es könnte so sein: Der Stack meldet das als `SET_ATTR_VALUE` und eventuell auch das sekündliche Herunterzählen. Belegt ist das nicht. Deshalb startet das Blinken nur, wenn der Wert steigt; fallende Werte lösen es nicht erneut aus. Im Log erscheint `IDENTIFY_CLUSTER: IdentifyTime N s`. Fehlt diese Zeile beim Identify aus Z2M, kommt der Befehl auf einem anderen Weg an; dann die `ZCL action`-Zeilen ansehen.

Lokales Schalten (Taster, Z5) läuft über `on_off_cluster_set_state()`. Die Funktion setzt das Attribut unter dem Zigbee-Lock. Ob Z2M den neuen Zustand per Report erfährt, hängt von der Reporting-Konfiguration ab, die Z2M beim Interview einrichtet (Ziel Z2). Auf Hardware prüfen.

## Zigbee2MQTT

Coordinator: Z-Stack 3.x.0 auf SLZB-06P10.

1. Nach dem Flashen der 2.x-Firmware einmalig `idf.py -p COM5 erase-flash`. Die 1.x-Netzdaten lagen in einer Partition, die es nicht mehr gibt (siehe [zigbee-framework.md](zigbee-framework.md#persistenz)). Ein vorhandener Eintrag des alten Geräts in Z2M sollte vorher entfernt werden.
2. In Z2M „Permit join“ aktivieren und das Gerät starten. Das Pairing-Fenster ist 3 min offen.
3. Das Gerät meldet sich als `zigbee-light` / `ZL-LightChain`.

Für diese Kennung gibt es keinen offiziellen Z2M-Converter. Z2M führt das Gerät deshalb als „unsupported“. Ich glaube es könnte so sein: Z2M erzeugt aus den Clustern trotzdem automatisch die Exposes `state` und `battery`. Prüfen, wenn das Gerät gepairt ist. Für ein sauberes Gerät kann ein [External Converter](https://www.zigbee2mqtt.io/advanced/support-new-devices/01_support_new_devices.html) angelegt werden.

### Gerät neu anlernen

`idf.py -p COM5 erase-flash` löscht auch die NVS-Partition und damit das Pairing. Später geht das per Taster (Z5) mit `zigbee_device_factory_reset()`.
