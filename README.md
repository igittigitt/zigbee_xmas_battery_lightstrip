# Rechargeable Battery Powered Zigbee LED Christmas Lightstrip

Dieses Projekt dient keinem speziellen Zweck, es ist eher eine Machbarkeitsstudie und zum Lernen.

Firmware für eine batteriebetriebene Zigbee-LED-Lichterkette: **ESP32-H2**, **ESP-IDF 5.5.4**, **esp-zigbee-lib 2.0.5**, Rolle Sleepy End Device, Ziel **Zigbee2MQTT**.

## Dokumentation

Die gesamte Dokumentation steht in [docs/](docs/README.md):

- [Ziele und Umfang](docs/goals.md)
- [Hardware](docs/hardware.md)
- [Toolchain, Build, Flash](docs/setup-build.md)
- [Zigbee-Framework (Versionen, Programmiermodell)](docs/zigbee-framework.md)
- [Bekannte Probleme](docs/known-issues.md)

Hinweise für KI-Coding-Agents: [AGENTS.md](AGENTS.md).

## Schnellstart

```bash
idf.py set-target esp32h2
idf.py build
idf.py -p PORT flash monitor
```

Details und Build aus der PowerShell: [docs/setup-build.md](docs/setup-build.md).

## Akkumessung kalibrieren

Die Firmware misst die Akkuspannung über einen Spannungsteiler an GPIO 1. ADC und Widerstände haben Toleranzen. Ohne Kalibrierung kann die angezeigte Spannung deshalb um einige zehn Millivolt danebenliegen. Am Prototyp waren es +1,35 %, bei 4,1 V also etwa 56 mV. Ein Korrekturfaktor pro Gerät gleicht das aus: **`BATTERY_CAL_PERMILLE`** in [main/battery/battery_config.h](main/battery/battery_config.h), in Promille (1000 = keine Korrektur).

Jedes Gerät einzeln kalibrieren. Der Wert eines anderen Boards passt nicht.

### Was man braucht

- Das Gerät mit angeschlossenem Spannungsteiler an GPIO 1 (Schaltung: [docs/hardware.md](docs/hardware.md#akkumessung)).
- Eine Spannungsquelle am **5V-Pin**: der Akku selbst oder ein einstellbares Netzteil (z. B. PPK2 als Source Meter), im Bereich 3,0–4,2 V.
- Ein Multimeter.
- Einen USB-UART-Adapter mit **3,3-V-Pegel** für das Log, angeschlossen an GPIO 24 → RX und GND ([docs/setup-build.md](docs/setup-build.md#log-im-akkubetrieb-ohne-usb)).

> ⚠️ **Kein USB am Board, solange am 5V-Pin eine Quelle hängt.** Der 5V-Pin ist direkt mit USB-VBUS verbunden. USB würde 5 V in Akku bzw. Netzteil drücken.

### Ablauf

1. **Firmware mit Akkumessung bauen.** In `idf.py menuconfig` → *Zigbee Light* entweder *DEBUG level* = **0: production** wählen, oder bei Level 1/2 zusätzlich **„Measure the battery via ADC in DEBUG builds“** einschalten. Bei Level 1/2 ohne diese Option wird der Akkustand nur simuliert, und es gibt keinen Messwert. Flashen, danach USB abziehen.
   ⚠️ In Level 0 (und 3) ist der Tiefentladeschutz aktiv: Unter **3,0 V** geht das Gerät in den Notschlaf und läuft erst ab 3,6 V wieder an. Messpunkte unter 3,0 V deshalb mit Level 1/2 aufnehmen.
2. **Den aktuellen Faktor notieren**, also den Wert von `BATTERY_CAL_PERMILLE` in `battery_config.h` (beim ersten Mal 1000). Das ist **CAL_alt**.
3. **Aufbauen:** Quelle an 5V-Pin und GND, UART-Adapter anschließen, `idf.py -p COMx monitor` starten (COMx = Port des Adapters).
4. **Messen:** Reset-Taste drücken. Das Gerät misst beim Start und danach nur alle 15 min. Im Log erscheint:
   ```
   I (560) POWER_CFG_CLUSTER: Battery 4126 mV (at rest), state of charge 91 %
   ```
   Der Wert nach `Battery` ist **U_Log**. Gleichzeitig mit dem Multimeter **direkt an 5V-Pin und GND des Boards** messen, nicht an der Quelle (die Leitungen verlieren Spannung). Das ist **U_Multimeter**.
5. **Wiederholen** bei mindestens zwei weiteren Spannungen im Bereich 3,0–4,2 V, z. B. 3,3 V und 4,2 V.
6. **Neuen Faktor ausrechnen**, für jeden Messpunkt:
   ```
   CAL_neu = CAL_alt × U_Multimeter / U_Log
   ```
   Die Ergebnisse mitteln und auf eine ganze Zahl runden.
7. **Eintragen:** In `main/battery/battery_config.h` die Zeile ändern:
   ```c
   #define BATTERY_CAL_PERMILLE            987
   ```
   Neu bauen und flashen (`idf.py build`, dann `idf.py -p PORT flash`, dafür USB wieder anstecken und die Quelle am 5V-Pin vorher abklemmen).
8. **Prüfen:** Schritt 3 und 4 wiederholen. U_Log und U_Multimeter sollten jetzt auf wenige mV übereinstimmen.

### Beispiel (Prototyp, 2026-11-08)

CAL_alt = 1000, Multimeter Fluke 179:

| Spannung am 5V-Pin (Multimeter) | U_Log | CAL_neu = 1000 × U_Multimeter / U_Log |
|---|---|---|
| 2890 mV | 2928 mV | 987,0 |
| 3170 mV | 3212 mV | 986,9 |
| 3380 mV | 3426 mV | 986,6 |
| 4070 mV | 4126 mV | 986,4 |

Mittelwert 986,7 → gerundet **987**. Probe für den Punkt 4126 mV: 4126 × 987 / 1000 = 4072 mV, also 2 mV neben dem Multimeter.

Liefern die einzelnen Messpunkte deutlich verschiedene Faktoren (mehr als etwa ±3), reicht ein einziger Faktor nicht. Häufige Ursache ist dann eine Messung an der Quelle statt direkt am Board. Weitere Hintergründe: [docs/hardware.md](docs/hardware.md#kalibrierung).

## Einrichtung in Zigbee2MQTT / Home Assistant

Getestet am 2026-11-08 mit Zigbee2MQTT als Home-Assistant-Add-on.

### 1. Gerät anlernen

1. In Z2M **„Beitritt erlauben“** (Permit join) aktivieren.
2. Gerät in den Pairing-Modus bringen:
   - **Neues Gerät** (noch nie angelernt): einfach einschalten.
   - **Schon einmal angelernt** (z. B. aus Z2M entfernt, neues Netz, Rejoin klappt nicht): **BOOT-Taster ≥ 5 s halten**. Bei 5 s blitzt die Lichterkette kurz auf → loslassen. Das Gerät löscht seine Netzdaten (Factory Reset), startet neu und geht in den Pairing-Modus. Funktioniert in jedem Zustand, auch während das Gerät noch das alte Netz sucht.
3. Im Pairing-Modus blinkt die Lichterkette schnell (ca. 2 Hz). Das Gerät sucht **3 min** lang ein Netz. Danach hört es auf, das Licht geht aus (schont den Akku). Für einen neuen Versuch den BOOT-Taster erneut ≥ 5 s halten.
4. Nach erfolgreichem Beitritt leuchtet die Lichterkette 2 s, dann gilt der eingestellte Einschaltzustand (bei neuem Gerät: aus).
5. Das Gerät erscheint als `zigbee-light` / `ZL-LightChain`.

Kurzer Druck auf den BOOT-Taster schaltet das Licht im Betrieb ein/aus (nur wenn das Gerät verbunden ist).

Ohne den External Converter (Schritt 2) zeigt Z2M im Reiter *Info* **„Generated support“**. Dann gibt es nur Licht, Identify und den Akkustand in %. Es fehlen die Spannung und die Restlaufzeit.

### 2. External Converter installieren

Für dieses Gerät gibt es keine offizielle Unterstützung in Z2M. Der Converter liegt im Repo unter [z2m/zl-lightchain.mjs](z2m/zl-lightchain.mjs).

1. **Ordner anlegen:** Im Z2M-Datenordner, also dort, wo die `configuration.yaml` liegt, den Ordner **`external_converters`** anlegen. Beim HA-Add-on ist das `homeassistant/zigbee2mqtt/` bzw. `/config/zigbee2mqtt/`. Z2M legt den Ordner nicht selbst an.
   > Nicht mit `external_extensions` verwechseln. Dort liegen Z2M-Extensions, ein Converter wird dort nicht geladen.
2. **Datei kopieren:** `zl-lightchain.mjs` in diesen Ordner legen. Ergebnis: `…/zigbee2mqtt/external_converters/zl-lightchain.mjs`.
3. **External JS erlauben:** In der Z2M-`configuration.yaml` unter dem bestehenden Abschnitt `advanced:` ergänzen. Bei neuen Installationen ab Z2M 2.11.0 ist das standardmäßig aus.
   ```yaml
   advanced:
     # … bestehende Einträge …
     enable_external_js: true
   ```
4. **Z2M neu starten.** Im Log muss stehen:
   ```
   info:  z2m: Loaded external converter 'zl-lightchain.mjs'.
   ```
   Die vorangehende Debug-Zeile `External JS folder 'external_converters' is outside the Z2M install dir, creating a symlink to 'node_modules'` ist normal.
5. **Prüfen:** Im Gerät, Reiter *Info*, steht jetzt **„External support“**. Unter *Definition* steht „Battery powered LED light chain (ESP32-H2, sleepy end device)“.

### 3. Reporting einrichten

Direkt nach dem Laden des Converters zeigen die neuen Werte **N/A**. Z2M hat sie noch nicht gelesen, und das Reporting ist noch nicht eingerichtet.

1. Gerät neu starten (Reset bzw. Stromversorgung kurz trennen). Direkt nach dem Start ist das Gerät wach.
2. Sofort in Z2M **„Reconfigure“** ausführen (Symbol mit den kreisenden Pfeilen). Damit richtet Z2M das Reporting ein und liest die Werte einmal aus.
3. Schlägt das fehl, weil das Gerät schon wieder schläft (Sleepy End Device): Schritt 1 und 2 wiederholen. Einzelne Werte lassen sich im wachen Zustand auch über das Refresh-Symbol rechts neben dem Wert lesen.

### Werte in Zigbee2MQTT / Home Assistant

| Anzeige | Quelle | Hinweis |
|---|---|---|
| State, Brightness, Power-on behavior | On/Off, Level Control | Licht |
| Identify | Identify | Lichterkette blinkt kurz |
| Battery (%) | Power Configuration `0x0001` | Licht aus: aus der Ruhespannung. Licht an mit voller Helligkeit: aus der Spannung unter Last (eigene Tabelle). Gedimmt bleibt der letzte Wert stehen |
| Voltage (mV) | Herstellercluster `0xFC00` | Messwert in voller Auflösung (das Standardattribut hätte nur 100-mV-Schritte). Messung alle 15 min, Report höchstens stündlich, spätestens alle 4 h oder bei ≥ 10 mV Änderung. Bei eingeschaltetem Licht unter Last gemessen |
| Restlaufzeit Licht an (h) | Herstellercluster `0xFC00` | Schätzung aus dem Verbrauchsmodell, stündlich aktualisiert |
| Restlaufzeit (d) | Herstellercluster `0xFC00` | Schätzung bei der Nutzung der letzten 7 Tage. Bis ein voller Tag gezählt ist, unbekannt (Anzeige vermutlich 6553,5) |

Die Restlaufzeit-Werte sind **noch nicht belastbar**. Die Ströme im Verbrauchsmodell sind Platzhalter, bis sie gemessen sind (siehe [docs/known-issues.md](docs/known-issues.md)). Battery, Voltage und die Restlaufzeit sind in HA Diagnose-Entitäten. Sie stehen auf der Geräteseite im Abschnitt „Diagnose“.

> Ändert sich in der Firmware die Clusterliste, der Endpoint oder die Device ID, das Gerät in Z2M **neu interviewen** und anschließend „Reconfigure“ ausführen.

Technische Details zum Converter: [docs/zigbee.md](docs/zigbee.md#z2m-external-converter).

## Lizenz

MIT License - frei verwendbar für private und kommerzielle Projekte.

## Credits

Basierend auf Espressif ESP-IDF Zigbee Examples
