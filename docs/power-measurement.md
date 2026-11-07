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

Pro Szenario mit dem Power Profiler aufzeichnen, das Fenster markieren und **Mittelwert, Maximum und Ladung (µC bzw. mAh)** ablesen. Die Aufnahme als `.ppk2` speichern.

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
| | | Original-Board | S3 | M2 | 3,7 V | | | Ist-Ausgangswert, Zigbee-Sleep aus |
| | | nach Umbau | S3 | M2 | 3,7 V | | | Soll ≈ 90 µA + Polls |
