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
- *Status: noch nicht implementiert.*

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
| | | Original-Board | S3 | M2 | 3,7 V | | | Ist-Ausgangswert, Zigbee-Sleep aus |
| | | nach Umbau | S3 | M2 | 3,7 V | | | Soll ≈ 90 µA + Polls |
