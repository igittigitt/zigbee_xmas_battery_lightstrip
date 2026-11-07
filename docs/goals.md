# Fachliche Zielbeschreibung

Stand: 2026-11-07. Verbindliche Grundlage für alle weiteren Entscheidungen. Änderungen am Umfang hier zuerst eintragen.

Markierung: **[FEST]** entschieden · **[OFFEN]** noch zu klären · **[SPÄTER]** bewusst zurückgestellt

## Zweck

Akkubetriebene, einfarbige LED-Lichterkette, die sich als **Zigbee-Licht** in **Home Assistant** (via Zigbee2MQTT) einbindet, von dort geschaltet wird und ihren Akkustand meldet. Ziel ist, mit einer Akkuladung möglichst lange – idealerweise eine Weihnachtssaison – auszukommen.

**Leitprinzip: Energieeffizienz hat oberste Priorität.** Jede Funktion wird zuerst nach ihrer Wirkung auf Ruhestrom und Akkulaufzeit bewertet.

## Was wir tun (In Scope)

| # | Anforderung | Status |
|---|---|---|
| Z1 | Zigbee als **Sleepy End Device** (End Device mit Light Sleep, kein Router) | [FEST] |
| Z2 | **An/Aus** über On/Off-Cluster, Zustand wird an HA zurückgemeldet | [FEST] |
| Z3 | **Soft-Start / Soft-Stop**: Rampe 0 ↔ 100 % in ca. 1 s per PWM (LEDC), schützt Akku und ESP vor Spannungseinbrüchen | [FEST] |
| Z4 | **Reaktionszeit** auf HA-Befehle ≤ ca. 1–2 s (Poll-Intervall des Sleepy End Device). ⚠️ In esp-zigbee-lib 2.0.5 über das Keepalive-Intervall einstellbar (`ZB_KEEP_ALIVE_MS`, derzeit 2000 ms). Ob jeder Keepalive Befehle abholt, ist nicht dokumentiert → messen, siehe [power-management.md](power-management.md#reaktionszeit-ziel-z4-und-keepalive) | [FEST], Umsetzbarkeit [OFFEN] |
| Z5 | **Lokaler Taster** (BOOT): kurz = An/Aus (wird an HA gemeldet), lang (≥ 5 s) = Factory Reset + Pairing-Modus (siehe Z10). Bei Erreichen der 5 s kurzes Aufleuchten als „loslassen“-Signal | [FEST] |
| Z6 | **Einschaltverhalten nach Neustart/Akkuwechsel** per Zigbee konfigurierbar (ZCL On/Off-Attribut `StartUpOnOff` 0x4003: 0x00 aus, 0x01 an, 0x02 toggle, 0xFF vorheriger Zustand – ZCL Rev. 8 §3.8.2.2.5; in Z2M als `power_on_behavior`, siehe zigbee-herdsman-converters `toZigbee.ts`/`fromZigbee.ts`). Default: aus | [FEST] |
| Z7 | **Akkuüberwachung**: Spannung und Ladestand an HA (Power Configuration Cluster) | [FEST] |
| Z8 | **Tiefentladeschutz** in Software (Warnung → LED aus → Notschlaf), da der Buck-Boost bis 2,0 V weiterläuft | [FEST] |
| Z9 | PWM-Erzeugung (LEDC) bei LED aus stoppen, keine unnötigen periodischen Wakeups | [FEST] |
| Z10 | **Pairing-Modus mit Anzeige über die Lichterkette** (keine Onboard-LED). Siehe unten | [FEST] |
| Z12 | **Verbindungsanzeige nach dem Neustart** (Gerät hat ein gespeichertes Netz, Rejoin): vom Boot bis zur Verbindung ist die Lichterkette aus und blitzt jede Sekunde kurz auf (~30 %). Nach erfolgreicher Verbindung 3× Blinken im 0,5-s-Takt (~30 %), danach Zustand gemäß Z6. Kommt keine Verbindung zustande, endet die Anzeige nach 3 min (eigene Wahl zum Akkuschutz); das Licht geht dann in den Zustand gemäß Z6, der Rejoin läuft still weiter. Beim Pairing (Z10) gilt weiter das Z10-Muster. Aufgenommen am 2026-11-07 auf Wunsch des Nutzers | [FEST] |
| Z11 | **Identify**: Auf einen Identify- oder TriggerEffect-Befehl (z. B. „Identify“ in Z2M) blinkt die Lichterkette 2 s lang mit 5 Hz (kurze Rampen wie Z10), danach gilt wieder der vorherige Zustand. Die vom Coordinator gewünschte IdentifyTime wird nicht verwendet. Aufgenommen am 2026-11-07 auf Wunsch des Nutzers | [FEST] |
| Z11 | **Build-Schalter DEBUG** (Kconfig `CONFIG_ZL_DEBUG`, im Code `APP_DEBUG`): Prototyp am USB zum Flashen/Debuggen. Im DEBUG-Build übernimmt die Onboard-WS2812 die Rolle der Lichterkette; die Lichtsteuerung ist ein Treibermodul mit austauschbarem Backend (WS2812 ↔ externer PWM-LED-Treiber). Weitere Debug-Abweichungen werden über denselben Schalter gesteuert. Siehe [setup-build.md](setup-build.md#debug-schalter) | [FEST] |

### Z10 – Pairing-Modus

| Aspekt | Festlegung |
|---|---|
| Auslöser | Taster ≥ 5 s (Factory Reset, danach Pairing) **oder** Start ohne gespeichertes Netz |
| Anzeige | Lichterkette blinkt schnell, ca. 2 Hz, mit **reduzierter Helligkeit (~30 %)** und kurzen Rampen (~50 ms) – kein hartes Schalten (vgl. Z3) |
| Timeout | **3 min**. Danach Steering beenden, LED aus, Gerät schläft bis zum nächsten Tastendruck (kurzer Druck startet Pairing erneut) |
| Erfolg | Blinken stoppt, 2 langsame Pulse als Bestätigung, danach Zustand gemäß Z6 |
| Abbruch | Timeout oder Unterspannung (Z8) beendet den Pairing-Modus |
| Anzeige-LED | Nur die Lichterkette – Onboard-LED des ESP wird **nicht** genutzt. Ausnahme DEBUG-Build (Z11): dort *ist* die Onboard-WS2812 die simulierte Lichterkette |

Werte (5 s, 2 Hz, 30 %, 3 min) sind Startwerte und als `#define` konfigurierbar.

## Was wir nicht tun (Out of Scope)

| Thema | Begründung |
|---|---|
| Zeitpläne, RTC, EEPROM auf dem Gerät | übernimmt Home Assistant; spart Bauteile und Ruhestrom |
| Zigbee-Router-Rolle | Router müssen dauerhaft empfangen – nicht batterietauglich |
| Deep Sleep im Normalbetrieb | Zigbee nicht erreichbar, PWM aus, Rejoin teuer; nur als Notabschaltung bei Unterspannung |
| Lichteffekte (Blinken, Pulsieren) per Zigbee | wecken die CPU permanent; nur internes Feedback (Pairing, Bestätigungen, Z10). Einzige Ausnahme: das zeitlich begrenzte Identify-Blinken (Z11, 2 s) |
| Onboard-LED des ESP32-H2 (Produktivbetrieb) | Anzeige erfolgt ausschließlich über die Lichterkette; die WS2812 des ESP32-H2-Zero wird für das Produktivgerät ausgelötet. Nur im DEBUG-Build (Z11) dient sie als Ersatz für die Lichterkette |
| Endloses Network Steering ohne Netz | leert den Akku; ersetzt durch Pairing-Modus mit Timeout (Z10) |
| Farbe / adressierbare LEDs | Hardware ist einfarbig mit Konstantstromquelle |
| Akkuladung durch das Gerät | externe Ladeelektronik |
| WLAN, BLE, UART-Konsole im Betrieb | Strom; Logging nur im Debug-Build |
| Temperatursensor | Platzhalter aus Beispielcode, entfällt |

## Zurückgestellt

| Thema | Bedingung für Aufnahme |
|---|---|
| **Dimmen** (Level Control) | Nur, wenn es Strom spart. PWM-Dimmen hinter der KSQ senkt den mittleren Strom etwa linear zum Tastgrad (30 % ≈ 30 mA) (Rechnung unter der Annahme, dass die KSQ per PWM voll ein-/ausgetastet wird). PWM im Light Sleep ist möglich: laut [ESP-IDF-LEDC-Doku](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32h2/api-reference/peripherals/ledc.html) (Abschnitt Power Management) mit `sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE` **und** einer Light-Sleep-fähigen Taktquelle (RC_FAST) – mit höherem Sleep-Strom (Wert dort nicht angegeben). Die PWM-Infrastruktur entsteht durch Z3 ohnehin. Varianten: (a) Live-Dimmen per Level Control, (b) nur einstellbare **Maximalhelligkeit**, (c) separate einstellbare KSQ (Hardware). |
| Zigbee OTA-Update | wenn Grundfunktion stabil läuft |

## Offene Punkte

- [OFFEN] KSQ-Shield: Hat es einen Enable-/Dimm-Eingang? Welche PWM-Frequenz verträgt er? Wird er aus dem Akku oder aus 3,3 V gespeist?
- [VORSCHLAG] Board: Waveshare ESP32-H2-Zero mit Umbau (WS2812 aus, Low-Iq-LDO statt ME6217), Akku am 5V-Pin – siehe [hardware.md](hardware.md#board-umbau-esp32-h2-zero).
- [VORSCHLAG] Akkumessung an GPIO 1 (ADC1_CH0, kein Strapping-Pin), Schaltung siehe [hardware.md](hardware.md#akkumessung).
- [VORSCHLAG] PWM-Pin für die KSQ: GPIO 10 statt GPIO 8 (WS2812 + Strapping-Pin). GPIO 13/14 bleiben für einen optionalen 32-kHz-Quarz frei.
- [GEKLÄRT] LEDC im Light Sleep (ESP-IDF v5.5.1, LEDC-Doku, Abschnitt Power Management): Default `LEDC_SLEEP_MODE_NO_ALIVE_NO_PD` = **kein PWM-Signal im Sleep**. Für LED an im Sleep: `LEDC_SLEEP_MODE_KEEP_ALIVE` + RC_FAST. Ein laufender Hardware-Fade läuft im Sleep weiter, „the target duty might not be reached exactly. It will adjust to the target duty after wake-up.“ Mehrverbrauch laut Doku vorhanden, Wert nicht angegeben → messen.
- [OFFEN] Ziel-Laufzeit konkret beziffern (z. B. Saison Nov–Jan, X h/Tag an).
- [OFFEN] Gemessener Ruhestrom (Ist) als Ausgangswert – Messkonzept mit PPK2 siehe [power-measurement.md](power-measurement.md).

## Referenz

Konzepte zu Sleepy End Device, Akkuüberwachung und gemeinsamer Versorgung sind im Nachbarprojekt `esp32-xmas-lightstrip` (docs/power-management.md, docs/zigbee-integration.md) ausgearbeitet und können übernommen werden. Der dortige `components/led_controller` ist Kandidat für Z3.
