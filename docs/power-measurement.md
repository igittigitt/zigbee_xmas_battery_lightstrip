# Strommessung im Live-Betrieb (Nordic PPK2)

Messwerkzeug: **Nordic Power Profiler Kit II (PPK2)** mit der App *Power Profiler* in *nRF Connect for Desktop*.

Ziel: reale Ströme des Geräts in allen Betriebszuständen erfassen, die Annahmen in [hardware.md](hardware.md) bestätigen bzw. korrigieren und daraus die Akkulaufzeit ableiten. Die Messergebnisse stehen am Ende dieses Dokuments.

## PPK2 in Kürze

| Eigenschaft | Wert |
|---|---|
| Betriebsarten | **Source Meter** (PPK2 versorgt das Gerät, 0,8–5,0 V) und **Ampere Meter** (PPK2 in Reihe zu externer Quelle) |
| Messbereich | 500 nA – 1 A laut User Guide, „genau bis etwa 200 nA“; Produktseite nennt 200 nA – 1 A. 5 automatisch umgeschaltete Bereiche |
| Abtastrate | bis 100 kS/s |
| Logikeingänge | 8 Kanäle (D0–D7), Referenz über `VCC` am Logic-Port: 1,65–5,5 V (User Guide; Produktseite: 1,6–5,5 V) |
| USB | Port „USB DATA/POWER“ immer. Für Source Meter **über 400 mA** zusätzlich „USB POWER ONLY“ (Netzteil ≥ 1 A empfohlen) |

Quellen: [PPK2 User Guide](https://docs.nordicsemi.com/r/bundle/ug_ppk2/page/ug/ppk/ppk_user_guide_intro.html) (Abschnitte Intro, Connectors, Power supply, Specs), [Produktseite](https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2). Wo sich beide widersprechen, ist das angegeben.

Source Meter ist für unser Gerät die erste Wahl: Der PPK2 ersetzt den Akku, und die Spannung ist frei einstellbar. Damit lassen sich auch Akkuzustände nachstellen (4,2 V voll, 3,7 V nominal, 3,3 V Abschaltschwelle Z8).

## ⚠️ Wichtig vor jeder Messung: USB abziehen

Der **5V-Pin des ESP32-H2-Zero ist direkt mit VBUS verbunden** (siehe [hardware.md](hardware.md#esp-regler)). Ist während der Messung USB gesteckt:

- speist der PC das Board, und der PPK2 misst Unsinn,
- drückt USB 5 V in den Ausgang des PPK2 bzw. in den Akku.

**Ablauf:** flashen → USB abziehen → PPK2 anschließen → Versorgung im Power Profiler einschalten (das ist zugleich ein sauberer Kaltstart, den man mit aufzeichnen kann).

Folge: Während der Messung gibt es **keine Log-Ausgabe** über USB. Ereignisse werden stattdessen über die Logikeingänge des PPK2 sichtbar gemacht (siehe [Ereignismarker](#ereignismarker-über-die-logikeingänge)). Auch eine UART auf GPIO 23/24 mit externem USB-UART-Adapter ist keine gute Lösung: Über die TX/RX-Leitungen kann das Board parasitär mitversorgt werden, und das verfälscht den Ruhestrom.

## Messaufbauten

### M1 – Gesamtgerät aus „Akku“ (Referenzmessung)

```
PPK2 VOUT ──► Akku+ des Geräts (statt 18650)
PPK2 GND  ──► Akku−
```

- Modus: Source Meter, Spannung 3,7 V (Varianten: 4,2 V / 3,3 V)
- Misst alles, was später den Akku belastet: LDO, ESP, Spannungsteiler, Leckstrom des abgeschalteten Boost-Pfads, LED-Pfad.
- **Diese Messung ist maßgeblich für die Laufzeitrechnung.**

### M2 – Nur ESP-Pfad

```
PPK2 VOUT ──► 5V-Pin ESP32-H2-Zero
PPK2 GND  ──► GND
(LED-Pfad / Boost abgeklemmt)
```

- Modus: Source Meter, 3,7 V
- Zeigt Ruhestrom und Zigbee-Aktivität (Polls, TX, RX) ohne Einfluss des LED-Pfads.
- Vorher/Nachher-Vergleich für den [Board-Umbau](hardware.md#board-umbau-esp32-h2-zero) (WS2812 aus, LDO-Tausch). Soll nach dem Umbau ≈ 90 µA im Light Sleep.

### M3 – LED-Pfad unter Last

- Wie M1, aber mit Fokus auf die Szenarien „LED an“, Rampe und Pairing-Blinken.
- Erwartet ~150 mA bei Dauerlicht (Boost + KSQ). Das liegt sicher unter 1 A, aber den **Einschaltstromstoß des Boost-Wandlers** prüfen. Wird dabei der Bereich des PPK2 überschritten oder bricht die Spannung ein, beide USB-Ports des PPK2 anschließen.
- Alternativ Ampere Meter in Reihe mit dem echten 18650 (PPK2 `VIN` ← Akku+, `VOUT` → Gerät). Damit misst man unter realem Innenwiderstand des Akkus.

### M4 – Original-Timer statt 3× AA (Referenz, S9)

```
PPK2 VOUT ──► Batterie+ des Original-Timers (AA ausgebaut)
PPK2 GND  ──► Batterie−
Lichterkette am Timer wie im Normalbetrieb
```

- Modus: Source Meter. Spannungsreihe **4,8 / 4,5 / 4,2 / 3,9 / 3,6 / 3,3 / 3,0 V**, pro Stufe den Mittelwert des Stroms notieren (Timer-Ausgang „an“). 4,8 V entspricht grob drei frischen Zellen im Leerlauf, 3,0 V etwa 1,0 V pro Zelle – Rechnung, keine Herstellerangabe.
- Zusätzlich bei einer Stufe ≥ 1 s mit voller Abtastrate aufzeichnen: Ein gleichmäßiger Strom spricht für Gleichstrom, ein Rechteck für Taktung (PWM) durch den Timer.
- Strom im ausgeschalteten Zustand des Timers messen (Eigenverbrauch, 18 h am Tag).
- **Hintergrund:** Gemessen wurden 2,9 V / 100 mA mit frischen Batterien, im Alltag halten die AA aber 1–2 Wochen statt der ≈ 4 Tage aus der Rechnung mit konstant 100 mA. Hypothese und Auswertung: [hardware.md](hardware.md#messung-led-kette-2026-11-08).
- Ergebnis als Tabelle U / I ins Messprotokoll; Datei z. B. `JJJJ-MM-TT_S9_M4_Reihe.ppk2`.

## Ereignismarker über die Logikeingänge

Damit man Stromspitzen Firmware-Ereignissen zuordnen kann, setzt die Firmware in einem Debug-Build GPIOs, die auf D0–D7 des PPK2 gehen.

```
PPK2 Logic VCC ──► 3V3 des Boards (Pegelreferenz)
PPK2 Logic GND ──► GND
PPK2 D0..D3    ──► Marker-GPIOs
```

Vorschlag für freie GPIOs (nicht belegt, kein Strapping-Pin, nicht 13/14 für den 32-kHz-Quarz, nicht GPIO 1 für die Akkumessung):

| PPK2 | GPIO | Bedeutung (Vorschlag) |
|---|---|---|
| D0 | 2 | Zigbee-Signal-Handler aktiv |
| D1 | 3 | ZCL-Aktion empfangen (z. B. On/Off) |
| D2 | 4 | LED-Rampe läuft |
| D3 | 5 | Periodischer Update-/Messzyklus |

- Nur in Debug-Builds aktiv (Schalter per `#define` bzw. Kconfig). Im Produktivbuild bleiben die Pins aus.
- Die Logikeingänge des PPK2 sind hochohmig, der Messfehler ist vernachlässigbar.
- Der GPIO-Zustand bleibt im Light Sleep erhalten. Ein Marker, der „hängen bleibt“, verrät also auch, wo die Firmware eingeschlafen ist.
- *Status: umgesetzt als **DEBUG-Stufe 3** (2026-11-08), siehe [setup-build.md](setup-build.md#ppk2-marker-level-3). Abweichend von der Tabelle oben: D0 = wach/schläft (statt Signal-Handler), D1 = Pegelwechsel bei jedem Zigbee-Signal bzw. jeder ZCL-Aktion, D2 = Licht-Pegeländerung inkl. Fade, D3 = Akkumessung. Am 2026-11-09 auf Hardware bestätigt: D0 wechselt bei jedem Poll. Logic-Kabel nur bei ausgeschaltetem PPK2-Ausgang anstecken, siehe dort.*

## Firmware für Messungen vorbereiten

| Punkt | Warum |
|---|---|
| Log-Level auf `WARN` oder `NONE` (`CONFIG_LOG_DEFAULT_LEVEL_*`) | Log-Ausgabe hält die CPU wach. Ohne USB ist sie ohnehin nutzlos |
| Debug- und Mess-Build getrennt halten | Messwerte gelten nur für den Build, mit dem gemessen wurde. Git-Hash im Messprotokoll notieren |
| Zigbee-Sleep-Status beachten | Solange [known-issues.md](known-issues.md) Punkt 5 offen ist, empfängt das Radio dauerhaft. Dann sind **mA statt µA** zu erwarten. Diese Messung ist trotzdem sinnvoll als **Ist-Ausgangswert** |

## Messszenarien

| # | Szenario | Aufbau | Dauer / Auslöser | Was interessiert |
|---|---|---|---|---|
| S1 | Kaltstart mit bekanntem Netz | M1 | Versorgung ein, ~30 s | Ladung bis zum ersten Schlaf, Spitzenstrom |
| S2 | Kaltstart ohne Netz → Pairing | M1 | bis Join oder Timeout (3 min, Z10) | Kosten des Pairing-Modus inkl. Blinken |
| S3 | **Ruhe, verbunden, LED aus** | M1 + M2 | **≥ 10 min**, viele Poll-Zyklen | **Mittelwert = Standby-Strom**, Poll-Intervall und -Ladung |
| S4 | Einschalten per HA | M1 | Befehl aus Z2M | Reaktionszeit (Z4), Rampe (Z3), Einschaltstromstoß |
| S5 | LED an, Dauerbetrieb | M1/M3 | ≥ 1 min | Dauerstrom, Wirkungsgrad LED-Pfad |
| S6 | Ausschalten per HA und per Taster | M1 | Befehl / Tastendruck | Rampe, Nachlauf bis zum Schlaf |
| S7 | Unterspannung | M1, Source Meter | Spannung schrittweise 3,7 → 3,0 V | Greift Z8 (Warnung → LED aus → Notschlaf)? Strom im Notschlaf |
| S8 | Parent weg (Router/Koordinator aus) | M1 | ≥ 5 min | Verhalten bei Verbindungsverlust. Endloses Suchen würde den Akku leeren |
| S9 | **Referenz: Original-Timer** mit Kette | M4 | Spannungsreihe, je ≥ 10 s | Strom über der Batteriespannung: regelt der Timer oder begrenzt nur ein Vorwiderstand? |
| S10 | **Reboot-Schleife am Akku** (2026-11-08) | M1 → M2 → M3 (Ampere Meter mit echtem Akku) | Kaltstart, ~5 s, 100 kS/s | Spitzenstrom bei ≈ 0,6 s nach dem Start (Funk-Start + Licht an). Tritt die Schleife auch am Source Meter auf? Verschwindet sie ohne LED-Pfad? Siehe [Ablauf S10](#ablauf-s10-reboot-schleife) |

Pro Szenario mit dem Power Profiler aufzeichnen, das Fenster markieren und **Mittelwert, Maximum und Ladung (µC bzw. mAh)** ablesen. Die Aufnahme als `.ppk2` speichern.

### Ablauf S10 (Reboot-Schleife)

> **Gelöst (2026-11-08): Ursache war ein minderwertiges Steckbrett.** Nach dem Umbau auf ein hochwertiges Steckbrett startet das Gerät auch bei 3,7 V einwandfrei. Die Kontaktwiderstände des billigen Steckbretts ließen die Spannung bei den Funk-Spitzen (≈ 100 mA PHY-Kalibrierung, ≈ 180 mA TX) einbrechen. Dass der Elko nicht half, erklärt sich vermutlich damit, dass er im selben Steckbrett hinter denselben schlechten Kontakten saß. Das ist eine Vermutung, nicht gemessen.
> **Konsequenz:** Für Messungen und Dauerbetrieb Akku und Board kurz und gelötet verbinden, keine billigen Steckbretter oder Dupont-Verbindungen im Versorgungspfad. Der Verlauf unten bleibt als Diagnoseprotokoll stehen.

Symptom: Am Akku (Ruhespannung 3488 mV) startet das Gerät ständig neu. Das UART-Log bricht ≈ 590–600 ms nach dem Start mitten in einer Zeile ab, danach `rst:0x1 (POWERON)`. Es gibt keinen Panic und keine Brownout-Meldung. Ich glaube es könnte so sein: Die Versorgung bricht ganz weg, weil Funk-Start und Einschalten des Lichts zusammenfallen (Einschaltstrom des Boost) und die Schutzschaltung des Akkus abschaltet. **[UNBELEGT]**, das soll S10 klären.

Stand 2026-11-08 nach Schritt 1: Die Schleife tritt auch am PPK2 (Source Meter, 3,5 V) auf, der Akku allein ist also nicht die Ursache. Offen ist noch, ob eine Schutzplatine zwischen PPK2 und Gerät lag.

Nach Schritt 2: Die Schleife bleibt auch **ohne LED-Pfad**, die Spitze ist sogar höher (≈ 180–190 mA). Im Zoom dauert sie ≈ 0,1–0,15 ms, direkt danach fällt der Strom auf 0 (Reset), dann folgt der ROM-Boot mit wenigen mA. Das Licht ist damit **nicht** die Ursache.

- Ich glaube es könnte so sein: Die Spitze ist der erste Sendevorgang des Funks. Das Datenblatt nennt für 802.15.4-TX 153 mA bei 20 dBm (Tab. 4-2, siehe [hardware.md](hardware.md#esp-regler)), dazu kommt die Grundlast von ≈ 25–30 mA. `CONFIG_ESP_PHY_MAX_TX_POWER=20`, die Firmware setzt keine eigene Sendeleistung (`ezb_set_tx_power` wird nicht aufgerufen). Mit welcher Leistung der Stack ohne Aufruf sendet, ist **[UNBELEGT]**.
- Bei dieser Spitze bricht die 3,3-V-Schiene ein. Mögliche Ursachen: zu wenig Abstand zwischen Eingang und Ausgang des ME6217 bei 3,5 V Eingang, oder ein hoher Widerstand in der Zuleitung. Der ADC meldet 3,29 V statt 3,5 V, entweder ein ADC-Fehler oder echter Spannungsabfall. Ein Spannungsabfall von 0,2 V schon bei ≈ 20 mA würde ≈ 10 Ω in der Zuleitung bedeuten. **[UNBELEGT]**
- Am USB (5 V) läuft das Gerät.

Nächste Schritte:

1. Spannung am 5V-Pin direkt am Board mit dem Multimeter messen.
2. Spannungsreihe 5,0 / 4,2 / 3,9 / 3,7 / 3,5 V am Source Meter: Ab welcher Spannung läuft das Gerät?

Ergebnis Spannungsreihe (2026-11-08): **stabil ab 4,0 V, darunter Reboot-Schleife.** Damit wäre der Akku nur im obersten Bereich (4,0–4,2 V) nutzbar.

Ob der ME6217 die Ursache ist, ist **nicht belegt**:

- Das Datenblatt (V02, S. 1) nennt den Spannungsabfall nur für die 5,0-V-Variante: 100 mV bei 300 mA. Die Kennwerte sind bei V_IN = V_OUT + 1,0 V angegeben, für die 3,3-V-Variante also bei 4,3 V. Wie sich der Regler unterhalb davon bei schnellen Lastsprüngen verhält, steht nicht im Datenblatt.
- Rechnung, keine Messung: Damit 3,9 V am Eingang nicht reichen, müssten bei ≈ 180 mA mehr als 0,6 V über dem Regler abfallen. Der Chip ist laut Datenblatt (Tab. 4-2) ab 3,0 V spezifiziert und dürfte erst unterhalb davon aussteigen. Dafür müsste der Abfall sogar über 0,9 V betragen, also ≈ 5 Ω. Für einen 800-mA-LDO ist das ungewöhnlich hoch. Genauso gut kommen der Zuleitungswiderstand oder ein zu kleiner Kondensator am Ausgang beim Lastsprung in Frage.
- Abgrenzung: Spannung am 5V-Pin und am 3V3-Pin messen, testweise einen Elko an den 5V-Pin bzw. an 3V3 setzen, oder den Regler gegen den RT9080 tauschen und die Reihe wiederholen.

Multimeter-Messung (Fluke 179, 2026-11-08), PPK2 auf 3,7 V, ohne KSQ:

| Messpunkt | Spannung |
|---|---|
| PPK2-Ausgang, unbelastet | 3,70 V |
| 5V-Pin, Gerät läuft bzw. rebootet | ≈ 3,6 V (beim Reboot ≈ 0,1 V tiefer) |
| 3V3-Pin | 3,3 V konstant |

Auswertung (Rechnung, keine Messung):

- Zwischen PPK2 und 5V-Pin gehen schon bei Grundlast ≈ 0,1 V verloren. Bei ≈ 15–35 mA Grundlast (S10-Aufnahmen) ergibt das **≈ 3–7 Ω** im Zuleitungspfad (Kabel, Steckverbinder, oder Innenwiderstand des PPK2-Ausgangs).
- Bei der TX-Spitze von ≈ 180 mA fallen über diesen Widerstand **≈ 0,5–1,2 V** ab. Aus 3,7 V werden am 5V-Pin kurzzeitig ≈ 2,5–3,2 V. Das passt zur Grenze von 4,0 V: Dort bleiben ≈ 2,8–3,5 V.
- Der ME6217 regelt bei 3,6 V Eingang und Grundlast sauber auf 3,3 V. Hauptursache ist damit wahrscheinlich der Einbruch **vor** dem Regler. Das ist noch nicht durch eine Messung bestätigt (siehe Elko-Test).
- Nachmessung direkt an den PPK2-Klemmen (eingestellt 3,710 V): unbelastet 3,708 V, belastet 3,685 V. Der PPK2 verliert also ≈ 23 mV, das sind bei ≈ 20–30 mA Grundlast ≈ 1 Ω (Rechnung; ob sich das bei 180 mA linear fortsetzt, ist [UNBELEGT]). Der größere Teil des Verlusts bis zum 5V-Pin (≈ 3,685 → ≈ 3,6 V) liegt damit in Kabeln und Steckverbindungen, rechnerisch ≈ 3–4 Ω.
- **Elko-Test (2026-11-08): ein Elko am 5V-Pin wie auch an 3V3 ändert nichts.** Ein reiner Spannungseinbruch auf der Zuleitung während der kurzen TX-Spitze ist damit unwahrscheinlich. Werte und Anschlussart der Elkos sind noch nachzutragen. Die Zuleitungsrechnung oben bleibt richtig, erklärt den Reset aber offenbar nicht allein.
- **`rst:0x1` ist beim ESP32-H2 nicht eindeutig:** `RESET_REASON_CHIP_POWER_ON` und `RESET_REASON_CHIP_BROWN_OUT` haben beide den Wert `0x01` (IDF 5.5.4, `components/soc/esp32h2/include/soc/reset_reasons.h`). Der Reset kann also ein echter Einschalt-Reset oder ein Brownout auf Chip-Ebene sein. Die Brownout-Schwelle Level 0 (aktuelle Einstellung) liegt beim ESP32-H2 bei ≈ 2,57 V, ein Schätzwert laut `components/esp_hw_support/power_supply/port/esp32h2/Kconfig.power`.
- Offen, als Nächstes zu prüfen: Sendeleistung (DEBUG-Log `TX power … dBm` nach dem Join bei 4,0 V), Test ohne UART-Adapter, 3V3 und 5V-Pin mit dem Oszilloskop.
- Ein Multimeter mittelt über viele Millisekunden. Einbrüche von ≈ 0,1 ms Dauer zeigt es nicht an, auch nicht im MIN/MAX-Modus. Die „konstanten 3,3 V“ schließen einen kurzen Einbruch deshalb nicht aus.

1. **M1, Source Meter 3,5 V** (≈ Akkuspannung), Gerät komplett inklusive LED-Pfad. Kaltstart aufzeichnen.
   - Schleife bleibt aus → Ursache liegt in Akku, Schutzplatine oder Verkabelung → weiter mit Schritt 3.
   - Schleife tritt auf → weiter mit Schritt 2.
2. **M2**: LED-Pfad bzw. Boost abklemmen, sonst wie Schritt 1. Hört die Schleife auf, ist der LED-Pfad die Ursache.
3. **Ampere Meter mit echtem Akku inklusive Schutzplatine**: Akku-Ausgang → PPK2 `VIN`, PPK2 `VOUT` → Gerät. Zeigt den Strom bis zum Abbruch unter realem Innenwiderstand.

Notieren: I_max, Zeitpunkt des Abbruchs nach dem Einschalten, Form der Spitze (kurzer Puls oder Plateau). Das UART-Log (nur GPIO 24 → RX und GND) darf dabei angeschlossen bleiben. Für Ruhestrom-Messungen wird es entfernt (siehe oben).

## Auswertung und Laufzeitrechnung

Mittlerer Tagesstrom aus den Messwerten:

```
I_mittel = (t_an · I_an + t_aus · I_aus + n_Schaltvorgänge · Q_Schalt) / 24 h
Laufzeit = Akkukapazität (mAh) / I_mittel (mA)
```

Beispiel mit den Planwerten aus [hardware.md](hardware.md) (3.000 mAh, 6 h/Tag an, I_an ≈ 150 mA, I_aus ≈ 0,1 mA):

```
pro Tag:  6 h · 150 mA = 900 mAh   +   18 h · 0,1 mA = 1,8 mAh
Laufzeit ≈ 3.000 / 902 ≈ 3,3 Tage
```

Daraus folgt: Solange das Licht täglich mehrere Stunden brennt, bestimmen **LED-Pfad und Leuchtdauer** die Laufzeit. Der Standby-Strom wird erst bei kurzen Leuchtzeiten relevant. Er muss aber trotzdem im µA-Bereich liegen, sonst entlädt sich der Akku in Ruhephasen: 5 mA Dauerstrom leeren 3.000 mAh in 25 Tagen, 0,1 mA erst in ~3,4 Jahren.

## Messprotokoll

Messdateien ablegen unter `docs/measurements/` mit dem Namensschema `JJJJ-MM-TT_<Szenario>_<Aufbau>_<Spannung>.ppk2`, z. B. `2026-11-10_S3_M2_3V7.ppk2`.

| Datum | Git-Hash | Hardware-Stand | Szenario | Aufbau | U | I_mittel | I_max | Bemerkung |
|---|---|---|---|---|---|---|---|---|
| 2026-11-08 | ff2855d-dirty (DEBUG) | Original-Board + LED-Pfad | S10 Schritt 1 | M1 | 3,5 V | ≈ 15–20 mA Grundlast | ≈ 150 mA | Reboot-Schleife auch am Source Meter, Periode ≈ 0,6 s. Pro Zyklus Spitze ≈ 100 mA, kurz darauf ≈ 150 mA, danach Abfall auf 0 mA (Reset). Akku als Ursache damit ausgeschlossen. ADC meldet 3286–3292 mV bei eingestellten 3,5 V (Kalibrierung offen) |
| 2026-11-08 | ff2855d-dirty (DEBUG) | Original-Board, ohne LED-Pfad | S10 Schritt 2 | M2 | 3,5 V | ≈ 15–25 mA Grundlast | ≈ 180–190 mA | Schleife bleibt. Spitze ≈ 0,1–0,15 ms, danach 0 mA (Reset). LED-Pfad als Ursache ausgeschlossen |
| 2026-11-08 | ff2855d-dirty (DEBUG) | Original-Board, hochwertiges Steckbrett | S10 Lösung | M2 | 3,7 V | | | Startet einwandfrei. Ursache war das billige Steckbrett (Kontaktwiderstände) |
| 2026-11-08 | ff2855d-dirty (DEBUG) | Original-Board (ME6217), hochwertiges Steckbrett | Spannungsreihe (Vorgriff S7) | M2 | 4,2 → 2,7 V | | | Stabil bis 3,00 V; ab 2,9 V Reboot-Schleife; unter 2,74 V kein Start. Einordnung: [hardware.md](hardware.md#esp-regler) |
| 2026-11-09 | DEBUG-Stufe 3 (uncommitted) | Original-Board (ME6217, WS2812 bestückt), ohne LED-Pfad, Logic-Port an GPIO 2–5 | S3 (erste Messung) | M2 | 3,7 V (PPK2) | 11,41 mA über 88,6 s | 0,74 A beim Einschalten, danach ≈ 205 mA alle ≈ 2,0 s (Keepalive) | Grundlast konstant ≈ 11 mA, **kein Light Sleep erkennbar**. Digitalkanäle D0–D3 ohne Pegelwechsel. Ursache offen, Diagnose läuft |
| 2026-11-09 | DEBUG-Stufe 2 (uncommitted, Akku emuliert, kein ADC) | wie oben, UART-Adapter angeschlossen | S3 | M2 | 3,7 V (PPK2) | **1,08 mA** über 36,8 s | ≈ 200 mA | **Light Sleep funktioniert:** `esp_pm_dump_locks()` meldet 98 % SLEEP, ≈ 16 Light Sleeps pro 30 s, 1 Ablehnung seit dem Start. Poll alle ≈ 2 s: ≈ 36 ms, ≈ 367 µC, TX-Plateau ≈ 195 mA für ≈ 1 ms. Dazu ≈ alle 30 s ≈ 110 ms bei ≈ 7 mA, vermutlich die Ausgabe des PM-Dumps über die UART (Zuordnung nicht belegt). **Ruhestrom zwischen den Polls 847 µA** (Auswahl 1,6 s; dabei kleine periodische Spitzen bis 4,4 mA, Abstand ≈ 30 ms, Ursache offen). Einordnung: [hardware.md](hardware.md#board-umbau-esp32-h2-zero). Licht aus (StartUpOnOff → OFF) |
| 2026-11-09 | DEBUG-Stufe 2 + `ZL_DEBUG_BATTERY_ADC` (Testbuild `build_l2adc`) | wie oben | S3 | M2 | 3,5 V (PPK2; ADC-Log 3395 mV, erwartet am Pin ≈ 3380 mV) | **1,27 mA** | | Ruhephase **852 µA**. Dump: 98 % SLEEP, Schlaf wie ohne ADC. **Die Akkumessung blockiert den Light Sleep nicht.** Parent diesmal der Coordinator (LQI 46) statt eines Routers (LQI 35); ob das den höheren Mittelwert erklärt, ist offen |
| 2026-11-09 | wie oben (`build_l2adc`) | wie oben, **UART-Adapter abgezogen** | S3 | M2 | 3,5 V (PPK2) | | | Ruhephase **863 µA**, mit Adapter 852 µA. **Der UART-Adapter trägt nicht messbar zum Ruhestrom bei** |
| 2026-11-09 | wie oben (`build_l2adc`) | wie oben, KSQ angeschlossen, **keine LED-Kette** | S3 mit **Licht an** (per HA) | M2 | 3,5 V (PPK2) | | 4,43 mA (in der Auswahl) | Zwischen den Polls **1,14 mA** (Auswahl 1,4 s), mit Licht aus 0,85–0,86 mA. Das Gerät schläft also auch bei eingeschaltetem Licht, **eingeschaltetes Licht blockiert den Light Sleep nicht**. (Nachtrag: DEBUG-Stufe 3 **ohne Logic-Kabel** schläft ebenfalls, Ruhephase 866 µA. Die 11 mA der ersten Stufe-3-Messung hängen also am Anschluss des Logic-Ports, siehe [PPK2-Marker](setup-build.md#ppk2-marker-level-3).) Mehrstrom ≈ 0,28 mA. Ich glaube es könnte so sein: LEDC mit `KEEP_ALIVE` hält RC_FAST und die Peripherie-Domäne im Sleep an (known-issues 18/19), dazu kommt der Strom in den CE-Eingang der KSQ. Nicht getrennt gemessen. Periodische Spitzen bis ≈ 4,4 mA etwa alle 27–30 ms wie bei Licht aus |
| 2026-11-09 | DEBUG-Stufe 3 (`build_l3`) | Original-Board, Logic-Kabel vor dem Einschalten angesteckt, ohne UART-Adapter | S3, ein Poll im Detail | M2 | 3,5 V (PPK2) | Ruhephase 864 µA | 193 mA | **Marker funktionieren.** D0 ist ≈ 8 ms High, die CPU ist also so lange wach. Der Strom steigt aber schon ≈ 2,5 ms vor D0 an und fällt erst ≈ 4,5 ms nach D0 ab. Ich glaube es könnte so sein: Das sind das Aufwachen bzw. das Einschlafen der Hardware vor bzw. nach den Callbacks (Retention, Flash), nicht gemessen getrennt. Gesamte Poll-Aktivität ≈ 15 ms, darin TX ≈ 0,9 ms bei ≈ 190 mA. Ladung im 32-ms-Fenster 360 µC; abzüglich Ruhestrom (32 ms × 0,86 mA ≈ 28 µC) ≈ 330 µC pro Poll. Rechnung: bei 2 s Keepalive ≈ 0,17 mA Mittelwert durch die Polls. D1–D3 im Ruhebetrieb ohne Pegelwechsel (erwartet) |
| 2026-11-09 | DEBUG-Stufe 3 (`build_l3`, D1 noch als Puls) | wie oben | S4 Einschalten per HA (erste Aufnahme) | M2 | 3,5 V (PPK2) | 11,7 mA im 300-ms-Fenster | 199 mA | Ablauf laut D0/D2, Zeiten aus dem Bild abgelesen (≈ ±1 ms): Wachphase ab ≈ 486 ms mit zwei TX (≈ 499 und ≈ 505 ms), CPU wach bis ≈ 548 ms. Weitere kurze Wachphase mit TX bei ≈ 557 ms. Ab ≈ 638 ms erneut wach, **D2 (Rampe) ab ≈ 677 ms**, danach weitere Wachphase mit zwei TX (≈ 705 und ≈ 710 ms). Während der Rampe schläft die CPU zwischendurch (D0 Low, LEDC-Hardware-Fade). Von der Wachphase mit dem Befehl bis zum Rampenstart ≈ 190 ms. Zuordnung der einzelnen TX (Data Request, ACK, Report, Default Response) ist eine Vermutung. D1 zeigte keinen Puls: Die Handler laufen nur wenige µs, D1 ist seitdem ein Umschaltsignal. Zeitpunkt des Klicks in HA nicht erfasst |
| 2026-11-09 | DEBUG-Stufe 3 (`build_l3`, D1 als Umschaltsignal; ob die Firmware vom 08:33-Build stammt, ist unbestätigt) | wie oben | S4, viermal geschaltet per HA | M2 | 3,5 V (PPK2) | 1,62 mA über 25,9 s | 200 mA | Pro Schaltvorgang drei Wachphasen mit TX innerhalb von ≈ 0,2 s. Rampenstart (D2) ≈ 0,18 s nach Beginn der ersten Wachphase. **D2 bleibt ≈ 2,0 s High**; bei `LIGHT_RAMP_MS` = 1000 und 0 ↔ 100 % erwartet: ≈ 1,0 s plus 10 ms Boost-Settle (Ausschalten: plus ≈ 3 ms Stop-Latch), Ursache offen. **D1 zeigt keinen Pegelwechsel**, obwohl jeder Schaltvorgang über `attribute_handler_callback` läuft. Zwischen den Polls kleine Spitzen ≈ 25 mA ohne D0-Wechsel, Ursache offen |
| 2026-11-09 | DEBUG-Stufe 3 (`build_l3`, ELF-SHA256 `fca63aa7f`, D1 als Umschaltsignal) | wie oben, **D3 an GPIO 10 (PWM)** statt Akkumessung | S4 | M2 | 3,5 V (PPK2) | 2,15 mA über 8,2 s | 197 mA | **D1 funktioniert:** pro Schaltbefehl 3 Flanken (3 ZCL-Ereignisse) innerhalb von ≈ 0,1 s. Die Rampe (D2) beginnt ≈ 20 ms nach dem letzten dieser Ereignisse und ≈ 0,13 s nach dem ersten. D2 bleibt wieder ≈ 2,0 s High. Auf D3 erscheinen während der Rampe erst nach ≈ 1 s sichtbare PWM-Pulse. In einem 75-ms-Ausschnitt etwa in der Rampenmitte ein gleichmäßiges Rechteck mit grob 50 % Tastgrad, Periode aus dem Bild grob 1,3 ms. Abgezählt, ungenau: Frequenz damit eher ≈ 750 Hz als 1000 Hz, **[UNBELEGT]**, genaue Messung offen. Nach dem Rampenende nur noch vereinzelte Pulse. Ob der Tastgrad dann wirklich klein ist oder ob das die Darstellung eines fast durchgehenden High-Pegels ist, ist aus den Bildern nicht zu entscheiden. Strom zwischen den Polls bei eingeschaltetem Licht ≈ 1,1–1,2 mA mit wiederkehrendem Muster: Abfall auf ≈ 0,1 mA für 4–8 ms, dann Sprung auf ≈ 4,2 mA mit abklingender Flanke, Ursache offen |
| 2026-11-09 | nicht notiert (vermutlich wie zuvor) | Original-Board, **WS2812 ausgelötet**, ME6217 | S3 | M2 | nicht notiert | Ruhephase **373 µA** | | Vorher 863–866 µA. **WS2812-Anteil ≈ 490 µA** (Differenz, gilt bei gleicher Firmware und Spannung). Rest ≈ 190 µA über den Typwerten (ME6217 + ESP Light Sleep ≈ 185 µA), Kandidaten in [hardware.md](hardware.md#board-umbau-esp32-h2-zero). Fenster zwischen zwei Polls (1,701 s): Mittel **374,7 µA**, Max 498 µA, 637 µC. **Keine periodischen 4-mA-Spitzen mehr**, das Signal schwankt nur zwischen ≈ 250 und 480 µA. Der Grundstrom ist also wirklich ≈ 375 µA |
| 2026-11-09 | DEBUG-Stufe 3 + `ZL_DEBUG_DIAG_NO_LEDC` (`build_l3diag`, kein LEDC, GPIO 10/11 Low) | wie oben (WS2812 ausgelötet, ME6217) | S3 | M2 | nicht notiert | Ruhephase **127,3 µA** (Fenster 1,088 s, Max 188 µA, 138,6 µC) | | Gegenüber 374,7 µA mit LEDC **≈ 248 µA weniger** (Rechnung). Die LEDC-Konfiguration mit `KEEP_ALIVE` kostet den Ruhestrom auch bei Licht aus. Mechanismen siehe [known-issues](known-issues.md) 18. 127 µA liegen unter der Summe der Typwerte (≈ 185 µA) |
| 2026-11-09 | DEBUG-Stufe 3 (`build_l3`, LEDC `NO_ALIVE_ALLOW_PD`, PM-Lock bei Licht an, Taster per EXT1) | WS2812 ausgelötet, ME6217, KSQ angeschlossen, **keine LED-Kette** | S3 Licht aus | M2 | 3,5 V (PPK2) | Ruhephase **126,9 µA** (Fenster 1,080 s, Max 497 µA, 137,2 µC) | ≈ 210 mA (Polls) | **Ziel erreicht**, gleicher Wert wie ohne LEDC (127,3 µA). Neues Muster in der Ruhephase: Sägezahn mit Periode ≈ 120 ms (aus dem Bild abgelesen). Abfall auf ≈ 15 µA, langsamer Anstieg auf ≈ 50 µA über ≈ 20 ms, dann Sprung auf ≈ 500 µA mit Abklingen auf ≈ 150 µA. Ursache offen. Marker D0–D3 im Sleep ohne Pegelwechsel |
| 2026-11-09 | wie oben | wie oben | S4 Übergang aus → an, danach Licht an | M2 | 3,5 V (PPK2) | **10,27 mA** über 1,688 s (Licht an, kein Sleep) | 19,6 mA ohne Polls, ≈ 215 mA mit TX | Ab dem Einschalten konstant ≈ 10 mA, die CPU schläft wie vorgesehen nicht (PM-Lock `light_on`). D2 fällt nach dem Rampenende auf Low. Vergleich: mit `KEEP_ALIVE` und Licht an waren es 1,14 mA zwischen den Polls. **Licht an kostet damit ≈ 9,1 mA mehr** (Rechnung). Einordnung: Datenblatt ESP32-H2 Tab. 4-9 (Modem-sleep, 3,3 V, 25 °C), 96 MHz, CPU idle: 6 mA mit abgeschalteten bzw. 13 mA mit eingeschalteten Peripherietakten |
| 2026-11-09 | DEBUG-Stufe 3 (`build_l3`, Park-Lösung: aus und voll statisch mit Sleep) | wie oben | S4 an → aus → an | M2 | 3,5 V (PPK2) | 6,49 mA über 10,06 s | 209 mA | Licht an ≈ 10 mA, D0 bleibt High, **der Chip schläft bei Licht an nicht**. Licht aus: Sleep wie erwartet (D0 Low). D2 (Rampe) jeweils ≈ 1 s. **Taster weckt aus dem Sleep und schaltet korrekt** (Nutzer). Ich glaube es könnte so sein: Die gespeicherte Helligkeit (CurrentLevel, persistent) steht nicht auf 100 %, z. B. noch von der 50-%-Messung. Dann ist „an“ ein gedimmter Level und hält den PM-Lock. Prüfen mit Helligkeit 100 % in HA. **Bestätigt** (nächste Zeile) |
| 2026-11-09 | wie oben | wie oben | S3 Licht an, **Helligkeit 100 %** in HA | M2 | 3,5 V (PPK2) | Ruhephase **413,3 µA** (Fenster 0,44 s, Max 512 µA, 181,8 µC) | | **Der Chip schläft bei voller Helligkeit.** Gegenüber Licht aus (126,9 µA) ≈ 286 µA mehr (Rechnung). Das passt zum Eigenverbrauch des CN5711 bei CE High (I_VCC1 200–320 µA, Datenblatt S. 4), nicht getrennt gemessen. Vorher mit Licht an: 10,27 mA (ohne Sleep) bzw. 1,14 mA (`KEEP_ALIVE`) |
| | | Original-Board | S3 | M2 | 3,7 V | | | Ist-Ausgangswert, Zigbee-Sleep aus |
| | | nach Umbau | S3 | M2 | 3,7 V | | | Soll ≈ 90 µA + Polls |
