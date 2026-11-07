# Power Management

Umgesetzt in [power_management.c](../main/power_management.c) und [sdkconfig.defaults](../sdkconfig.defaults). Die Zigbee-Seite (Keepalive, `rx_on_when_idle`) steht in [zigbee_config.h](../main/zigbee_config.h). Grundlage ist esp-zigbee-lib 2.0.5, siehe [zigbee-framework.md](zigbee-framework.md#sleepy-end-device).

## Prinzip

In esp-zigbee-lib 2.x **steuert ESP-IDF den Schlaf, die Anwendung nicht**. Wenn kein Task läuft und niemand einen PM-Lock hält, legt der FreeRTOS-Idle-Task (Tickless Idle, `esp_pm`) den Chip in den Light Sleep. RAM und Netzzugehörigkeit bleiben erhalten. Das Gerät wacht im Keepalive-Intervall auf, holt beim Parent gepufferte Nachrichten ab und schläft wieder.

Quelle: SDK 2.0.5 `docs/en/faq.rst`, „Zigbee Light Sleep Mode“. Die 1.x-Mechanik mit `esp_zb_sleep_enable()`, Signal `CAN_SLEEP` und `esp_zb_sleep_now()` gibt es in 2.x nicht mehr; laut Migration Guide v2.x (`common.rst`) wurde sie entfernt.

## Konfiguration

| Einstellung | Wert | Wirkung / Quelle |
|---|---|---|
| `CONFIG_PM_ENABLE` | y | Power-Management-Framework (FAQ: Pflicht) |
| `CONFIG_FREERTOS_USE_TICKLESS_IDLE` | y | automatischer Light Sleep im Idle (FAQ: Pflicht) |
| `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP` | y | schaltet die Peripherie-Domäne im Sleep ab, wenn kein Treiber das verhindert (Beispiel `light_sleep_end_device`) |
| `CONFIG_IEEE802154_SLEEP_ENABLE` | y | Sleep des 802.15.4-Radios (Beispiel) |
| `CONFIG_ESP_SLEEP_POWER_DOWN_FLASH` | y | Flash im Sleep abschalten (Beispiel) |
| `CONFIG_FREERTOS_HZ` | 1000 | kürzere Mindest-Schlafdauer; Kommentar im 1.x-Beispiel: „Use 1000Hz freertos tick to lower sleep time threshold“ |
| `esp_pm_configure()` | max = min = `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` (96), `light_sleep_enable = true` | wie im Beispiel, keine Frequenzskalierung |
| `ZB_KEEP_ALIVE_MS` | 2000 | Aufwachintervall, siehe unten |
| `ZB_RX_ON_WHEN_IDLE` | false | Empfänger im Leerlauf aus (FAQ) |

**Ohne Netz** schläft das Gerät nicht. Die FAQ sagt: „In the light sleep example, the Zigbee task keeps running if the device is not part of a network.“ Deshalb ist die Netzsuche auf das Pairing-Fenster von 3 min begrenzt ([zigbee.md](zigbee.md#commissioning)).

## Zusammenspiel mit dem Licht-Modul

Aus dem Treiber-Quellcode, ESP-IDF v5.5.4, `components/esp_driver_ledc/src/ledc.c`:

- Mit `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP` registriert der LEDC-Treiber ein Sleep-Retention-Modul (Z. 435–449). Der Kommentar dort: „retention module should still be inited to avoid TOP PD“.
- Für einen Kanal mit `LEDC_SLEEP_MODE_KEEP_ALIVE` legt der Treiber **kein** Retention-Backup an (Z. 921–931). Die Peripherie-Domäne bleibt im Sleep also an, damit die PWM weiterläuft.

Ich glaube es könnte so sein: Das gilt, solange LEDC konfiguriert ist, also auch bei ausgeschaltetem Licht. Die Option spart dann im Produktiv-Build (PWM-Backend) weniger als erhofft. Messen, siehe [known-issues.md](known-issues.md).

## Reaktionszeit (Ziel Z4) und Keepalive

- Laut FAQ (2.0.5) bestimmt das Keepalive-Intervall, „how long the chip can remain in light sleep mode“. Einstellbar ist es über `keep_alive` beim Init (umgesetzt, `ZB_KEEP_ALIVE_MS`) oder über `ezb_nwk_set_keepalive_interval()`.
- Ein End Device hält die Verbindung laut FAQ mit „`Data Request` MAC commands or `End Device Timeout Request` NWK commands“. Gepufferte Befehle bekommt es nur über einen Data Request. **Welche der beiden Varianten 2.0.5 im Keepalive nutzt, ist nicht dokumentiert [UNBELEGT].** Davon hängt ab, ob 2000 ms Keepalive wirklich ≤ 2 s Reaktionszeit bedeuten.
- Weitere Stellschraube: `ezb_nwk_set_fast_poll_interval()`, laut Header Standard 200 ms. Wann der Stack „fast poll“ nutzt, ist nicht beschrieben.
- Obergrenze: Der Parent hält Nachrichten für ein schlafendes Kind `macTransactionPersistenceTime` lang vor. Mit den üblich genannten Werten (0x01F4 × 960 Symbole × 16 µs) ergibt sich 7,68 s.
  - Die Rechnung stimmt, die Konstanten selbst konnten aber nicht im (kostenpflichtigen) IEEE-802.15.4-Text geprüft werden **[UNBELEGT]**.
  - Das 16-µs-Symbol ist in ESP-IDF belegt (`esp_ieee802154.h`).
  - Bei einem Keepalive deutlich über diesem Wert gingen Befehle verloren.
- **Nächster Schritt:** Reaktionszeit und Strom bei 2000 ms messen, siehe [power-measurement.md](power-measurement.md). Den Wert danach anpassen.

## Sleep-Simulation im DEBUG-Build

Ziel: am USB debuggen können und das Sleep-Verhalten trotzdem möglichst realistisch sehen.

- `CONFIG_ZL_DEBUG=y` erzwingt per Kconfig-`select` **`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y`**.
  - Der IDF-Treiber hält dann einen `ESP_PM_NO_LIGHT_SLEEP`-Lock, solange über USB SOF-Pakete vom Host kommen (`esp_driver_usb_serial_jtag/src/usb_serial_jtag_connection_monitor.c`).
  - Eine reine Powerbank zählt laut Header nicht als verbunden (`usb_serial_jtag.h`).
- Hintergrund: Der USB-Serial/JTAG des ESP32-H2 unterstützt keinen Light Sleep. `SOC_USB_SERIAL_JTAG_SUPPORT_LIGHT_SLEEP` ist für esp32h2 in `soc_caps.h` auskommentiert. Ohne den Lock bräche die Konsole ab.

| Aspekt | Echter Light Sleep | Simuliert (DEBUG + USB) |
|---|---|---|
| CPU / Takte | Light Sleep | **läuft weiter** (USB-Lock) |
| 802.15.4-Radio zwischen Keepalives | schläft | **[UNBELEGT]**. Für 1.x ergab eine Disassembly-Analyse der Bibliotheken, dass der Zigbee-Sleep-Pfad unabhängig von der CPU lief; für 2.x ist das nicht untersucht |
| GPIOs mit `SleepSel=1` | Hardware schaltet auf Sleep-Konfiguration | **nicht nachgebildet**, dafür gibt es keine öffentliche API (`gpio_sleep_sel_en()`, `driver/gpio.h`). Stattdessen GPIO-Bericht im Log |
| GPIOs mit `SleepSel=0` (Licht-Pins, `gpio_sleep_sel_dis`) | behalten Pegel | identisch |
| LEDC `KEEP_ALIVE` / WS2812 | läuft weiter / hält Farbe | identisch |
| Ohne USB-Verbindung (DEBUG) | – | **echter** Sleep |

**Log-Ausgaben** (Tag `POWER_MGMT`, einmalig beim Start aus `power_management_report()`):

- `DEBUG: USB connected -> light sleep is SIMULATED …`, wenn USB verbunden ist;
- `GPIO report …`: für jeden Projekt-Pin (Licht-Backend + BOOT/GPIO 9), ob er im echten Sleep seine Konfiguration behält oder umschaltet.

Die Schlafstatistik aus der 1.x-Version (`SLEEP-SIM | last 10.0 s …`) ist entfallen. Sie hing am Signal `CAN_SLEEP`, das es in 2.x nicht mehr gibt. Als Ersatz kämen die ESP-IDF-Callbacks `esp_pm_light_sleep_register_cbs()` in Frage (`CONFIG_PM_LIGHT_SLEEP_CALLBACKS`, siehe Beispiel `light_sleep_end_device.c`). Bei verbundenem USB gibt es aber ohnehin keinen echten Sleep, den sie zählen könnten.

Grenzen: Ströme lassen sich so nicht messen, weil die CPU wach und USB aktiv ist. Das Wecken per Taster (GPIO-Wakeup) funktioniert in der Simulation immer, im echten Sleep erst nach entsprechender Konfiguration. Also ohne USB prüfen.

## Deep Sleep

Im Normalbetrieb wird Deep Sleep nicht genutzt (goals.md, Out of Scope): Zigbee wäre nicht erreichbar, die PWM aus, und jeder Rejoin kostet Energie. Vorgesehen ist er nur als Notabschaltung bei Unterspannung (Z8), noch nicht umgesetzt.
