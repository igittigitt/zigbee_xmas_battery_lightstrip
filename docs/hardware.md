# Hardware

## Stückliste

| Komponente | Details |
|---|---|
| MCU | **Waveshare ESP32-H2-Zero** (ESP32-H2FH4S, 4 MB Flash, Keramikantenne, nativer USB), IEEE 802.15.4 / Zigbee nativ. Umbau nötig, siehe [Board-Umbau](#board-umbau-esp32-h2-zero) |
| Akku | Li-Ion 18650, 1S, 3.000 – 5.000 mAh (4,2 V voll, ~3,7 V nominal) |
| ESP-Versorgung | Low-Iq-LDO auf dem Board (RT9080-33 statt ME6217), Akku direkt am 5V-Pin – siehe [ESP-Regler](#esp-regler). Bisherige Alternative: Buck-Boost 2,0 – 5,0 V → 3,3 V am 3V3-Pin |
| LED-Versorgung | Boost-Wandler auf 5 V (Akkuspannung reicht für LED-Flussspannung + KSQ-Reserve nicht über die ganze Entladekurve) |
| LED-Treiber | KSQ-Shield (Konstantstromquelle), ca. 100 mA |
| Taster | BOOT-Taste des Boards (GPIO 9) |

## Versorgungskonzept

Zwei getrennte Pfade, damit LED-Lastsprünge die ESP-Versorgung nicht stören:

```
                 ┌──► 5V-Pin ──► LDO RT9080-33 (onboard) ──► ESP32-H2
                 │    (Akku direkt, kein Buck-Boost)
18650 1S ────────┤
(3,0 – 4,2 V)    │    Lastschalter / EN
                 └──► Boost 5 V ──► KSQ ~100 mA ──► LED-Kette
                          ▲            ▲
                    GPIO (EN)    GPIO (PWM, LEDC)
```

### Anforderungen

- **Boost-Wandler bei LED aus vollständig abschalten** (EN-Pin oder MOSFET-Lastschalter per GPIO). Ich glaube es könnte so sein: Fertige Boost-Module haben ohne Last einen Eigenverbrauch, der deutlich über dem Sleep-Strom des ESP (85 µA) liegt – [UNBELEGT], konkreter Wert erst mit Typ/Datenblatt des Moduls bzw. per Messung.
- **PWM** an den Dimm-/Enable-Eingang der KSQ für Soft-Start/-Stop (Ziel Z3) und Pairing-Blinken (Z10).
- Einschaltreihenfolge: Boost EN → kurze Stabilisierung → PWM-Rampe hoch. Ausschalten umgekehrt.

### Messung LED-Kette (2026-11-08)

Vom Nutzer gemessen, am **Original-Timer mit 3× AA**: Spannung an der Kette **ca. 2,9 V**, Strom **100 mA**.

- Daraus (Rechnung): LED-Leistung ≈ 2,9 V × 0,1 A ≈ 0,29 W, passt zur bisherigen Annahme 0,3 W.
- Noch offen: Wurde die Spannung direkt an den Anschlüssen der Kette gemessen? Wie hoch war dabei die Batteriespannung? Davon hängt ab, ob der Timer den Strom regelt (KSQ) oder nur einen Vorwiderstand hat – „genau 100 mA“ deutet auf Regelung hin, belegt ist das nicht.
- **Widerspruch zur Praxis:** Laut Nutzer halten die 3× AA im Alltag (6 h/Tag) **1–2 Wochen**. Mit konstant 100 mA ergibt die Rechnung aus den Datenblättern nur ≈ 4 Tage (siehe [Laufzeitabschätzung](#grobe-laufzeitabschätzung-led-an)).
- **Hypothese:** Ich glaube es könnte so sein: Der Timer begrenzt den Strom nur mit einem Vorwiderstand und regelt nicht. Dann sinkt der Strom mit der Batteriespannung, z. B. mit angenommenen ~16 Ω (aus (4,5 V − 2,9 V) / 0,1 A): 3,9 V → ≈ 60 mA, 3,6 V → ≈ 45 mA, 3,3 V → ≈ 25 mA (Rechnung; die sinkende Flussspannung bei kleinerem Strom ist nicht berücksichtigt). Ein mittlerer Strom von grob 40–50 mA ergäbe mit ≈ 2.500 mAh ≈ 50–60 h, also 1,5–2 Wochen bei 6 h/Tag – passend zur Beobachtung. Die Kette wird dabei allmählich dunkler. Weitere mögliche Gründe: Taktung (PWM) durch den Timer, Erholung der Zellen in den Pausen, Spannungsabfall am Shunt des Multimeters bei der Strommessung. Alles [UNBELEGT].
- **Prüfung:** Szenario S9 / Aufbau M4 in [power-measurement.md](power-measurement.md#m4--original-timer-statt-3-aa-referenz-s9) (Strom über der Spannung am Timer), alternativ Blick ins Timer-Gehäuse (ein einzelner Widerstand in Reihe zur Kette).
- **Folge, falls bestätigt:** Die Kette ist im Alltag offenbar mit deutlich weniger als 100 mA hell genug. Dimmen per PWM (Z13) auf z. B. 40–60 % würde den Verbrauch etwa halbieren (Annahme: Strom linear zum Tastgrad) – bei gleichbleibender Helligkeit bis zum Abschalten. Rechnung: 18650 mit 3.000 mAh und 50 mA ≈ 60 h ≈ 10 Tage bei 6 h/Tag.
- Ich glaube es könnte so sein: Die Kette ist parallel geschaltet (eine Flussspannung von ~2,9 V entspricht einer einzelnen weißen LED). Anzahl der LEDs und eventuelle Vorwiderstände in der Kette sind [UNBELEGT].

### Wirkungsgrad des LED-Pfads

Eine lineare KSQ verheizt die Differenz zwischen Versorgung und LED-Flussspannung:

**Rechnung, keine Messung.** Annahmen (alle [UNBELEGT], da Typen von LED, KSQ und Boost noch unbekannt): KSQ arbeitet linear, LED-Flussspannung V_F ≈ 3 V, Boost-Wirkungsgrad η_Boost ≈ 90 %.

Wirkungsgrad einer linearen KSQ = V_F / V_Versorgung; gesamt = η_Boost × V_F / V_Versorgung.

| Variante | Rechnung | Ergebnis (unter obigen Annahmen) |
|---|---|---|
| Boost 5 V + lineare KSQ | 0,9 × 3 / 5 | ≈ 54 % |
| Boost auf 3,7 V + lineare KSQ (falls einstellbar und KSQ-Dropout ≤ 0,7 V) | 0,9 × 3 / 3,7 | ≈ 73 % |
| Boost-LED-Treiber mit Konstantstrom | Wirkungsgrad aus Datenblatt des konkreten ICs | [UNBELEGT] |

Ob die KSQ linear oder getaktet arbeitet, entscheidet über die gesamte Rechnung – erst mit dem Typ belastbar.

#### Variante ohne Boost: lineare KSQ direkt am Akku

Mit der gemessenen Flussspannung von 2,9 V liegt die Akkuspannung im ganzen Nutzbereich **über** der LED-Spannung. Ein Boost ist dann nicht nötig, sofern die KSQ mit wenig Spannungsreserve auskommt. **Rechnung, keine Messung:**

| Akkuspannung | Reserve für die KSQ (U_Akku − 2,9 V) | Wirkungsgrad 2,9 V / U_Akku |
|---|---|---|
| 4,2 V (voll) | 1,3 V | ≈ 69 % |
| 3,7 V (nominal) | 0,8 V | ≈ 78 % |
| 3,3 V (Abschaltschwelle Z8) | 0,4 V | ≈ 88 % |

- **Bedingung:** Dropout der KSQ bei 100 mA ≤ ca. 0,4 V, damit die vollen 100 mA bis zur Abschaltschwelle Z8 fließen. Unterhalb der Reserve regelt die KSQ nicht mehr, die Kette wird dann allmählich dunkler. Dropout des vorhandenen KSQ-Shields: [OFFEN].
- **Akkustrom = LED-Strom** (lineare Regelung, plus Eigenverbrauch der KSQ): ≈ 100 mA. Zum Vergleich Boost 5 V + lineare KSQ: 5 V × 0,1 A / 0,9 / 3,7 V ≈ 150 mA (Rechnung mit η_Boost 90 % und 3,7 V). Also rund ein Drittel weniger Strom, ein Bauteil weniger und kein Leerlaufstrom des Boost im Standby.
- Die Flussspannung steigt mit dem Strom und sinkt mit der Temperatur; 2,9 V gilt für die gemessenen 100 mA bei Raumtemperatur. Wie stark sie schwankt, ist [UNBELEGT].
- Weiterhin nötig: Abschalten der KSQ bei LED aus (EN-Eingang oder Lastschalter), PWM für Rampen und Dimmen (Z3, Z13). Abschaltstrom der vorhandenen KSQ siehe [KSQ-Modul](#ksq-modul-ld06ajsa).

### KSQ-Modul LD06AJSA

Label auf dem Modul laut Nutzer „L006AJSA“; im Handel heißt es **LD06AJSA** (Händlerangaben: 2,8–6 V, 30–1500 mA, PWM < 2 kHz, Überstrom 1,9 A typ.). Händlerseiten (manuals.plus, Elecbee) waren für automatische Abrufe gesperrt; Angaben dort nur aus Suchergebnissen.

- **Treiber-IC:** **CN5711** (Consonance). Aufschrift auf dem IC vom Nutzer geprüft (2026-11-08). Die Händlerwerte stimmen mit dem Datenblatt überein (2,8–6 V, bis 1,5 A, Überstrom 1,9 A typ., PWM an CE < 2 kHz). Datenblatt: [datasheets/CN5711_datasheet.pdf](../datasheets/CN5711_datasheet.pdf) (Consonance REV 1.1, Kopie von mikrocontroller.net).
- **Linearregler**, kein Schaltregler: „current regulation“, Verlust P = (VCC − V_LED) × I_LED (Datenblatt S. 1, S. 5). Dropout 30 mV bei 100 mA, 370 mV bei 1,5 A (S. 4, V_DROP) – erfüllt die Bedingung der [Variante ohne Boost](#variante-ohne-boost-lineare-ksq-direkt-am-akku).
- **CE-Eingang** (Pin 8, „Chip Enable“, S. 3): High = an, Low = aus. Schwellen V_CEL ≤ 0,6 V, V_CEH ≥ 2,3 V, Eingangsstrom ±1 µA (S. 4). 3,3-V-Logik des ESP32-H2 reicht. PWM direkt an CE, f < 2 kHz (S. 6, Method 1) – unsere 1 kHz passen.
- **Abschaltstrom** bei CE = 0 V: ≤ 1 µA (S. 4, I_VCC2). Ruhestrom eingeschaltet 200–320 µA (I_VCC1).
- **Offener CE leuchtet voll** (Beobachtung des Nutzers, 2026-11-08). Der IC selbst hat laut Datenblatt keinen nennenswerten Pull-up (±1 µA). Ich glaube es könnte so sein: Das Modul zieht CE über einen Widerstand an VCC (Datenblatt-Applikation: CE direkt an VCC, S. 2). Wert [UNBELEGT].
- **CE an GPIO 10 (PWM) blieb nach „aus“ an, an GPIO 11 (einfacher Ausgang) nicht** (Nutzer, 2026-11-08). Ursache vermutlich in der Firmware: LEDC-Timer wurde direkt nach `ledc_stop()` pausiert, bevor der Ruhepegel übernommen war. Korrektur: `LIGHT_PWM_STOP_LATCH_MS` in `light_config.h` (2 PWM-Perioden warten vor `ledc_timer_pause()`); **am Gerät bestätigt** (2026-11-08). Der Mechanismus (Übernahme erst beim Timer-Überlauf) ist weiterhin nicht durch Doku belegt.
- **Befund 2026-11-09 (Nutzer):** Auf der Modulplatine ist **kein Pull-up an CE erkennbar**, nur das Poti und ein Widerstand dazu. Ich glaube es könnte so sein: Der offene CE-Eingang (CMOS, ±1 µA) floatet und wird als High erkannt. Die folgenden zwei Punkte zum Pull-up sind damit vermutlich gegenstandslos.
- **Fail-safe (Z8):** Pull-down **100 kΩ von CE nach GND**. Dann ist die KSQ aus, wenn der ESP aus, im Reset oder im Deep Sleep ist.
  - Rechnung mit Datenblattwerten: höchstens 1 µA × 100 kΩ = 0,1 V < V_CEL 0,6 V.
  - Bei CE High fließen 3,3 V / 100 kΩ = 33 µA.
  - Am Gerät noch nicht umgesetzt.
- **Folge für den Akku:** Solange der ESP CE auf Low hält, fließt durch diesen Pull-up dauerhaft VCC / R_pu – auch im Light Sleep. Beispiel (Rechnung, R_pu angenommen): 4,2 V / 10 kΩ = 420 µA. Messen: Strom von CE nach GND bei eingeschaltetem Modul (Multimeter im µA-Bereich).
- **Pull-up am ESP-Pin:** VCC (Akku bis 4,2 V) liegt über 3,3 V. Über den Pull-up fließt dann Strom in GPIO 10, begrenzt durch R_pu. [UNBELEGT], ob unbedenklich – sicherer ist, den Pull-up zu entfernen.

### ESP-Regler

**Entscheidung (Vorschlag):** Den Onboard-LDO des ESP32-H2-Zero (U1, ME6217C33M5G) gegen einen pinkompatiblen **Low-Iq-LDO** tauschen und den Akku direkt an den **5V-Pin** (= VIN des LDO) legen. Der Buck-Boost entfällt für den ESP-Pfad.

Begründung: Der ESP32-H2 läuft ab 3,0 V, und unterhalb ~3,3 V Akkuspannung greift ohnehin der Tiefentladeschutz (Z8). Ein Buck-Boost ist dafür nicht nötig; ein LDO mit µA-Ruhestrom ist im Standby sparsamer.

| Regler | Max. Strom | Ruhestrom (typ.) | Bemerkung |
|---|---|---|---|
| ME6217C33M5G (Original) | 800 mA | 100 µA (max. 130 µA) | EN fest an VIN, mehr als der ESP im Light Sleep |
| **Richtek RT9080-33GJ5** | 600 mA (Strombegrenzung ≥ 610 mA) | 2 µA (max. 4 µA) | **Gewählt**, Daten geprüft, siehe unten |
| TI TPS7A2033PDBVR | 300 mA | ≈ 6,5 µA | unter der Datenblatt-Empfehlung von 0,35 A (s. u.) – nicht verwenden |
| Torex XC6220B331MR | 1 A | 8 µA nur im Power-Save-Modus (I_OUT = 0,1 mA); im High-Speed-Modus (I_OUT = 10 mA) 50 µA typ. / 108 µA max., automatische Umschaltung | Alternative. B-Typ ohne CE-Pull-down → CE darf nicht offen sein. Quelle: Torex ETR03041-016b, S. 6 (Kopie bei chipdip.ru, torexsemi.com nicht erreichbar) |

- Pinbelegung ME6217C (SOT23-5) laut [Datenblatt](../datasheets/ME6217_datasheet.pdf) V02, S. 2: 1 VIN, 2 VSS, 3 CE, **4 NC**, 5 VOUT. Das Schaltplansymbol von Waveshare beschriftet Pin 4 als „BP“, markiert ihn aber als **unbeschaltet** (×). CE (Pin 3) liegt laut Schaltplan an VCC_5V.
- **RT9080-33GJ5** laut [Datenblatt](../datasheets/RT9080_dataseeht.pdf) (DS9080-09, Dez. 2024): TSOT-23-5 (flachere Bauform, gleicher Footprint), 1 VIN, 2 GND, 3 EN, 4 NC, 5 VOUT → **pinkompatibel**. Pin 4 ist intern nicht verbunden, ein Kondensator oder GND daran ist laut Datenblatt unschädlich.
  - ⚠️ **Nicht die Variante RT9080N bestellen** – dort ist Pin 4 der Sense-Eingang (SNS), der auf dem Board unbeschaltet bliebe (Pin 4 ist laut Schaltplan offen).

  **Pinvergleich (beide Datenblätter geprüft):**

  | Pin | ME6217C33M5G (SOT-23-5) | RT9080-33GJ5 (TSOT-23-5) |
  |---|---|---|
  | 1 | VIN | VIN |
  | 2 | VSS (GND) | GND |
  | 3 | CE (EN) | EN |
  | 4 | NC | NC |
  | 5 | VOUT | VOUT |

  Hinweise zum Tausch:
  - **EN-Schwelle:** ME6217 ein ab 1,5 V, RT9080 ab 0,9 V. EN liegt auf dem Board an VCC_5V → beide schalten sicher ein.
  - **Bauhöhe:** TSOT-23-5 ist flacher (≈ 1,0 mm statt ≈ 1,45 mm), Pinabstand und Pads sind identisch – lötet auf denselben Footprint.
  - **Pin 4:** bei beiden NC; die Bezeichnung „BP“ im Waveshare-Symbol ist ohne Bedeutung.
  - Eingang 1,2 – 5,5 V (Absolute Max. 6,5 V), 600 mA ab V_in ≥ 2,3 V, Ruhestrom 2 µA typ. / 4 µA max., abgeschaltet 0,1 µA.
  - EN: ein > 0,9 V, aus < 0,4 V, Eingangsstrom ≤ 0,1 µA – EN fest an VIN wie auf dem Board ist zulässig.
  - Ausgangskondensator ≥ 1 µF effektiv (Board: 10 µF) ✓.
  - Kein Unterspannungs-Lockout dokumentiert; Übertemperatur-Abschaltung bei 150 °C. Verlustleistung im ungünstigsten Fall (4,2 V → 3,3 V, 153 mA TX-Spitze) ≈ 0,14 W → bei 189 °C/W kurzzeitig ≈ +26 K, unkritisch.
  - Kein Rückstromschutz: Absolute Max. VOUT − VIN ≤ +0,3 V (wie beim ME6217). Unkritisch, solange nur über den 5V-Pin versorgt wird.
- Strombedarf ESP32-H2: 802.15.4-TX 38 mA (0 dBm) bis 153 mA (20 dBm). Das Datenblatt (Tab. 4-2) fordert aber eine Versorgung, die **≥ 0,35 A** liefern kann → Regler mit ≥ 500 mA wählen.
- Versorgungsbereich ESP32-H2 laut Datenblatt (Tab. 4-2): **3,0 – 3,6 V**.
- **Dropout-Betrieb:** Belegt ist laut RT9080-Datenblatt (DS9080-09, EC-Tabelle S. 6) nur: Dropout 0,31 V typ. / 0,53 V max. bei 600 mA, V_OUT ≥ 3 V, definiert bei V_OUT = 98 % des Nennwerts; Pass-Element ist ein P-MOSFET (§17.2). **Wie sich der Ausgang verhält, wenn V_IN unter den Sollwert fällt (ob er V_IN durchreicht), sagt das Datenblatt nicht** [UNBELEGT].
  - Ich glaube es könnte so sein: Der P-MOSFET schaltet dann voll durch und V_ESP ≈ V_Akku − I × R_on. Mit R_on aus dem Dropout gerechnet (0,31 V / 0,6 A ≈ 0,52 Ω typ., 0,53 V / 0,6 A ≈ 0,88 Ω max.) wären das ≈ 0 mV im Sleep, 14–24 mV bei RX (27 mA), 20–66 mV bei TX 0–9 dBm (38–75 mA), 80–135 mV bei TX 20 dBm (153 mA); der ESP bliebe bis ca. 3,05–3,15 V Akkuspannung im spezifizierten Bereich. **Per Messung prüfen** (Source Meter, Spannung schrittweise senken, V_3V3 messen).
  - **Messung mit dem Original-LDO ME6217 (2026-11-08)**, PPK2 Source Meter am 5V-Pin, ohne LED-Pfad, DEBUG-Build: **bis 3,00 V stabil, ab 2,9 V Reboot-Schleife, unter 2,74 V kein Start mehr.** Für den RT9080 steht die Messung noch aus.
    Achtung: Das sind die **am PPK2 eingestellten** Werte. Am 5V-Pin lagen laut späterer Messung (siehe [Kalibrierung](#kalibrierung)) ≈ 0,11–0,13 V weniger an. Bei 3,00 V Einstellung zeigte der Fluke am Pin **2,89 V** (gemessen). Die stabile Grenze am 5V-Pin liegt also bei ≈ 2,89 V.
  - Einordnung: 3,0 V ist die untere Grenze der Versorgungsspannung laut ESP32-H2-Datenblatt (Tab. 4-2). Ich glaube es könnte so sein: Unterhalb davon reicht der LDO die Eingangsspannung nur noch abzüglich seines Spannungsabfalls durch, und bei den Funk-Spitzen (≈ 100–180 mA) fällt die Chipspannung unter die Brownout-Schwelle (Level 0 ≈ 2,57 V, Schätzwert laut IDF-Kconfig). Unter 2,74 V reicht es schon für den Start nicht mehr.
  - Folge für Z8: Die geplante Abschaltschwelle von 3,2–3,3 V liegt 0,2–0,3 V über der gemessenen Grenze von 3,0 V. Mit dem echten Akku kommen noch Innenwiderstand und Schutzschaltung dazu, die bei den Spitzen zusätzlich Spannung kosten. Die Reserve deshalb mit dem echten Akku (Ampere Meter, S7) bestätigen.
- **Arbeitsbereich Eingangsspannung (5V-Pin):** ca. 3,05 V bis **5,5 V** (Obergrenze RT9080; 6,5 V ist nur Absolute Maximum).
- **Restkapazität unter 3,3 V** – kein Datenblatt nennt sie für die Ruhespannung. Verfügbare Anhaltspunkte, die sich nicht decken:
  - Panasonic NCR18650B, Entladekurve 0,2 C, 25 °C (**unter Last**, Werte aus der Kurve abgelesen): ca. 6–8 % der Kapazität (bis 2,5 V) liegen unter 3,3 V, ca. 1–2 % unter 3,0 V. Quelle: Datenblatt, gehostet bei [batteryjunction](https://files.batteryjunction.com/frontend/files/Panasonic-NCR18650B.pdf) und TI E2E (zwei übereinstimmende Kopien, nicht vom Hersteller selbst).
  - Beispiel-OCV-Tabelle von TI ([SLUAAR3](https://www.ti.com/lit/SLUAAR3), Fig. 2-1, generische Zelle): 3,38 V ≙ 20 %, 3,20 V ≙ 10 % → 3,3 V Ruhespannung ≈ 15 % Ladestand.
  - Ich glaube es könnte so sein: Für unsere konkrete Zelle liegt der Wert irgendwo zwischen diesen Angaben; er hängt von Zellchemie, Temperatur und Alterung ab. **Klärung per Messung** (Entladung der eigenen Zelle) sinnvoll, bevor die Schwelle Z8 festgelegt wird.
- Entladeschlussspannung laut Datenblättern: Samsung INR18650-35E 2,65 V; Samsung 30Q, LG MJ1, Panasonic NCR18650B 2,5 V. 3,0 V nennt keines davon – das ist eine konservative eigene Wahl.
- Ob ein Buck-Boost die Restkapazität sinnvoll nutzbar machen würde, hängt von dieser Restkapazität und dem Ruhestrom des Wandlers ab – [OFFEN]. Abschaltschwelle Z8 vorläufig ca. 3,2–3,3 V Ruhespannung (Hysterese, Messung bei LED aus), Festlegung nach Klärung.
- ⚠️ **5V-Pin = VBUS** (direkt verbunden, siehe Schaltplan). Bei gestecktem USB und angeschlossenem Akku speist USB mit 5 V in den Akku zurück. Vor dem Flashen Akku abklemmen oder eine Schottky-/Ideal-Diode in die Akkuleitung setzen (kostet ~0,2–0,3 V Reserve).
- **Versorgungspfad niederohmig verdrahten:** Der ESP32-H2 zieht beim Funk-Start kurze Spitzen von ≈ 100–180 mA (PPK2-Messung S10, [power-measurement.md](power-measurement.md#ablauf-s10-reboot-schleife)). Über ein billiges Steckbrett führte das unterhalb von 4,0 V zu einer Reboot-Schleife. Akku und 5V-Pin deshalb kurz und gelötet verbinden.
- Nicht über den 3V3-Pin einspeisen, solange der ME6217 bestückt ist: Das Datenblatt verbietet VOUT > VIN + 0,3 V (Rückstrom über die parasitäre Diode des P-MOSFET).

## Prototyp (DEBUG-Build)

Für die ersten Funktionstests läuft das unveränderte ESP32-H2-Zero am USB-Port (Versorgung, Flashen, Log). Die Onboard-WS2812 (GPIO 8) ersetzt die Lichterkette, siehe [setup-build.md](setup-build.md#debug-schalter). Kein Akku anschließen, solange USB steckt (5V-Pin = VBUS, s. o.). Der Board-Umbau unten betrifft erst das Produktivgerät.

## Board-Umbau ESP32-H2-Zero

Quellen: [Schaltplan](../datasheets/ESP32-H2-Zero-Schematic.pdf), [ESP32-H2-Datenblatt](../datasheets/Esp32-h2_datasheet_en.pdf), [ME6217-Datenblatt](../datasheets/ME6217_datasheet.pdf).

| Bauteil | Befund laut Schaltplan | Maßnahme |
|---|---|---|
| LED1 (WS2812B-0807) | VDD direkt an 3V3, DI an GPIO 8, **R1 10 kΩ Pull-up von GPIO 8 nach 3V3**. (C18 1 µF gehört zur Reset-Beschaltung CHIP_PU, nicht zur LED.) Ruhestrom bei „aus“: für die 0807-Bauform kein Hersteller-Datenblatt gefunden [UNBELEGT]. Andere Varianten laut WorldSemi: WS2812B-V5/W 0,6 mA, WS2812B-2020-V6 ≤ 1 µA. Ein Datenblatt eines Drittanbieters (XL-0807RGBC-WS2812B, bei Waveshare gehostet) nennt 2,5 mA bei 4,5 V – nicht vom Hersteller, Textlayout unklar | **Auslöten** (zerstörend ist ok; Taster und USB-C-Buchse mit Kapton schützen) |
| U1 (ME6217C33M5G) | VIN = VCC_5V = VBUS (direkt, keine Diode), VOUT = 3V3, CE fest an VIN (nicht herausgeführt) | **Tauschen** gegen Low-Iq-LDO (siehe oben) |
| Power-LED | nicht vorhanden | – |
| USB-UART-Wandler | nicht vorhanden, USB D+/D− direkt am ESP32-H2 (der auf der Waveshare-Seite verlinkte CH343-Treiber ist nicht nötig) | – |
| R6 10 kΩ (BOOT/GPIO 9 → 3V3), R7 10 kΩ + C18 1 µF (Reset/CHIP_PU) | Strom nur bei gedrücktem Taster (3,3 V / 10 kΩ = 330 µA) | – |
| R1 10 kΩ (GPIO 8 → 3V3) | bleibt nach Auslöten der WS2812 bestückt | GPIO 8 **nicht auf Low treiben** (sonst 330 µA Dauerstrom); als Eingang lassen oder R1 ebenfalls entfernen |

Erwarteter Ruhestrom im Light Sleep (Typwerte, **durch Messung zu bestätigen**). Die 85 µA gelten laut ESP32-H2-Datenblatt (Tab. 4-10) für: CPU und Funk abgeschaltet, Peripherietakte aus, **alle GPIOs hochohmig** – jede aktive Peripherie (z. B. LEDC auf RC_FAST bei LED an) liegt darüber.

| Zustand | WS2812 | LDO | ESP32-H2 Light Sleep | Summe |
|---|---|---|---|---|
| Original | [UNBELEGT], s. o. (≤ 1 µA bis 2,5 mA je nach Quelle) | 100 µA (ME6217 typ.) | 85 µA | ≥ 185 µA + WS2812 – messen (Szenario S3/M2) |
| Nach Umbau | – | ≈ 2 µA | 85 µA | ≈ 90 µA |
| **Gemessen Original (2026-11-09)** | bestückt | ME6217 | DEBUG-Stufe 2, Light Sleep laut `esp_pm_dump_locks()` 98 % | **≈ 847 µA** zwischen den Polls (PPK2, 3,7 V, UART-Adapter angeschlossen) |
| **Gemessen Zwischenstand (2026-11-09)** | **ausgelötet** | ME6217 | Light Sleep | **≈ 373 µA** zwischen den Polls (PPK2) |

Die WS2812 kostete damit **≈ 490 µA** Ruhestrom. Das ist die Differenz von 863–866 µA (Ruhephase mit WS2812, ohne UART-Adapter) zu 373 µA. Sie gilt nur unter der Annahme, dass Firmware und Versorgungsspannung bei beiden Messungen gleich waren. Der UART-Adapter ist als Ursache ausgeschlossen: Ohne ihn lag der Ruhestrom bei 863 µA, mit ihm bei 852 µA (gleiche Firmware). Die Größenordnung passt zu den 0,6 mA, die WorldSemi für WS2812B-V5/W angibt. Für die verbaute Bauform 0807 gibt es weiterhin keinen Herstellerwert.

Nach dem Auslöten liegt der Ruhestrom noch ≈ 190 µA über der Summe der belegten Typwerte (ME6217 100 µA + ESP32-H2 85 µA ≈ 185 µA). Ungeklärt [OFFEN].

Ausgeschlossen sind:
- **Periodische Spitzen:** In einem 1,7-s-Fenster zwischen zwei Polls gibt es keine (Mittel 374,7 µA, Max 498 µA). Der Grundstrom selbst liegt also bei ≈ 375 µA.
- **KSQ-Modul:** Mit WS2812 lag der Ruhestrom mit angeschlossener KSQ (Licht aus) bei 0,85–0,86 mA, ohne sie bei 852–863 µA.

Verbleibende Kandidaten:
- **ME6217:** Ruhestrom bis 130 µA (max.) statt 100 µA typ. Der LDO-Tausch trennt diesen Anteil ab.
- **Firmware:** Die 85 µA aus dem Datenblatt gelten für alle GPIOs hochohmig und Peripherietakte aus. Flash- und Peripherie-Abschaltung sind in der `sdkconfig` aktiv (`CONFIG_ESP_SLEEP_POWER_DOWN_FLASH`, `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP`). **Bestätigt (2026-11-09):** Ohne LEDC sinkt der Ruhestrom auf **127 µA**. Die LEDC-Konfiguration mit `KEEP_ALIVE` kostet damit ≈ 248 µA, auch bei Licht aus ([known-issues](known-issues.md) 18). Mit 127 µA liegt das Board unter der Summe der Typwerte von ≈ 185 µA, der ME6217 zieht also vermutlich weniger als 100 µA. Das ist nicht getrennt gemessen. Abhilfe in der Firmware siehe [power-management.md](power-management.md#zusammenspiel-mit-dem-licht-modul).

Hinzu kommen Spannungsteiler der Akkumessung (~2 µA), Leckstrom des abgeschalteten LED-Pfads und die Zigbee-Polls.

## Akkumessung

**Entscheidung (Vorschlag):** Hochohmiger Spannungsteiler + ADC des ESP32-H2 mit eFuse-Kalibrierung (`esp_adc_cali`). Kein Strommess-Chip, keine Z-Diode.

```
Akku+ ──[ 1 MΩ 1% ]──┬──[ 1 MΩ 1% ]── GND
                     ├── ADC (GPIO 1 = ADC1_CH0)
                     └──[ 100 nF ]─── GND
```

- Teiler 1:1 → 4,2 V Akku = 2,1 V am ADC. Ruhestrom (Ohmsches Gesetz) 4,2 V / 2 MΩ = 2,1 µA dauerhaft.
- **ADC-Genauigkeit laut ESP32-H2-Datenblatt, Tab. 4-6** (nach Hardware- und Software-Kalibrierung; gilt für Chips ab Date Code 342023 bzw. D/C 2334):

  | Dämpfung | Messbereich | Gesamtfehler |
  |---|---|---|
  | ATTEN0 | 0–1000 mV | ±7 mV |
  | ATTEN1 | 0–1300 mV | ±8 mV |
  | ATTEN2 | 0–1900 mV | ±12 mV |
  | ATTEN3 | 0–3300 mV | ±23 mV |

  Bei 1:1-Teiler (max. 2,1 V) ist ATTEN3 nötig → ±23 mV am ADC = **±46 mV Akkuspannung**. Mit einem Teiler, der 4,2 V auf < 1,9 V abbildet (z. B. 1,3 MΩ / 1 MΩ → 4,2 V ⇒ 1,83 V; Rechnung), reicht ATTEN2 → ±12 mV am ADC = ±28 mV Akkuspannung. Hinzu kommt die Toleranz der Widerstände.
- Kondensator 100 nF: Das Datenblatt (Abschnitt 4.5) gibt die ADC-Werte für eine Messung **mit externem 100-nF-Kondensator am ADC-Pin** an; wir übernehmen das. Ich glaube es könnte so sein: Bei einer Quelle mit ~0,5 MΩ Innenwiderstand ist der Kondensator zusätzlich nötig, damit der Abtastkondensator des ADC schnell genug geladen wird – das ist Plausibilität, nicht belegt.
- Falls doch geschaltet: nur High-Side per P-MOSFET (Low-Side würde Akkuspannung > 3,3 V an den ADC-Pin legen).

**Begründung:** Die Spannung ist für den Tiefentladeschutz (Z8) die maßgebliche Messgröße. Für den Ladestand (%) ist sie nur eingeschränkt genau:
- Die Ruhespannungskurve ist im mittleren Bereich flach. In der TI-Beispieltabelle ([SLUAAR3](https://www.ti.com/lit/SLUAAR3), Fig. 2-1) entsprechen 20–80 % Ladestand 3,38–3,99 V, also im Mittel ca. 10 mV pro Prozent. Battery University ([BU-903](https://batteryuniversity.com/article/bu-903-how-to-measure-state-of-charge)) beschreibt die NMC-Kurve als sehr flach im Mittelteil.
- Folge mit unserem ADC-Fehler (±28–46 mV Akkuspannung, s. o.): allein dadurch ca. ±3–5 % Ladestand im Mittelbereich (Rechnung mit 10 mV/% aus der generischen TI-Tabelle). Zellstreuung, Temperatur, Alterung und Relaxation kommen hinzu – deren Größe ist [UNBELEGT].
- Die einzige gemessene Angabe, die gefunden wurde: TI SLUAAR3 berichtet für reine Spannungszuordnung **unter dynamischer Last** Spitzenfehler von ca. 17–20 %. Für Messung in Ruhe fand sich keine Zahl. Die frühere Angabe „±10 %“ war nicht belegt und ist gestrichen.
- Innenwiderstand neuer Zellen laut Datenblatt (AC 1 kHz, Maximalwert): Samsung 35E ≤ 35 mΩ, LG MJ1 ≤ 40 mΩ, Samsung 30Q ≤ 26 mΩ. Bei ~150 mA Last ergibt das ≤ 6 mV Spannungsabfall (Rechnung; DC-Widerstand bei Pulsen, niedrigem Ladestand, Kälte und Alterung liegt höher, Wert [UNBELEGT]).

| Alternative | Warum nicht (vorerst) |
|---|---|
| Fuel-Gauge-IC (MAX17048, I²C) | Zusatzbauteil. Datenblatt 19-6171 Rev 3 (Kopie bei chipsmall.com; analog.com nicht erreichbar, neuere Rev 7 ungeprüft): Hibernate 3 µA typ. / 5 µA max. (Titelseite derselben Revision nennt 4 µA), aktiv 23 µA typ. / 40 µA max., Spannungsfehler ±7,5 mV/Zelle bei 3,6 V und 25 °C, ±20 mV über den Bereich. **Eine Genauigkeit des Ladestands in % nennt das Datenblatt nicht** (nur Diagramme) – frühere Angabe „±3–5 %“ war unbelegt |
| Strommess-Chip (INA219/226) | Coulomb-Zählung braucht Kalibrierung und Bezug „voll“. Ruhestrom laut TI: INA219 0,7 mA typ. / 1 mA max. (Power-down 6 µA typ.), SBOS448G; INA226 330 µA typ. / 420 µA max. (Power-down 0,5 µA typ.), SBOS547C |
| Z-Diode | Ich glaube es könnte so sein: nur als Schwellwertgeber brauchbar, Toleranz und Temperaturgang zu groß für eine Messung – nicht belegt |

**Software-Regeln:** Messen bei LED aus (bzw. einige Sekunden nach Ausschalten), Mittelung über mehrere Samples, OCV-Lookup-Tabelle für %, Unterspannungsentscheidung erst nach mehreren Messungen in Folge, mit Hysterese. Messintervall 10–30 min, gebündelt mit ohnehin nötigen Wakeups, zusätzlich bei jedem LED-Schaltvorgang. Umgesetzt sind Mittelung, Messung nur in Ruhe für den Ladestand und 15 min Intervall; die Messung bei jedem Schaltvorgang sowie Mehrfachmessung/Hysterese für Z8 fehlen noch.

### Firmware-Umsetzung (Z7)

Umgesetzt am 2026-11-08 in [battery.c](../main/battery/battery.c) (Messung) und [power_config_cluster.c](../main/clusters/power_config_cluster.c) (Attribute). Alle Werte in [battery_config.h](../main/battery/battery_config.h).

```
beim Start (LED noch aus) und alle BATTERY_MEASURE_INTERVAL_MS = 15 min:
  ADC-Unit anlegen → 16 Samples, eFuse-kalibriert (Curve Fitting) → Mittelwert → Unit löschen
  × Teilerverhältnis (R_TOP + R_BOTTOM) / R_BOTTOM × BATTERY_CAL_PERMILLE / 1000  → Akkuspannung mV
  LED aus → Ladestand aus BATTERY_OCV_TABLE, geglättet (nur fallend, Anstieg erst ab +5 %)
  LED an  → nur Spannung, Ladestand bleibt (Spannung unter Last ist zu niedrig)
  → BatteryVoltage (100 mV) und BatteryPercentageRemaining (0,5 %)
```

- **ADC-Unit nur während der Messung:** Solange eine Oneshot-Unit existiert, hält der IDF-Treiber im Light Sleep die Power-Domänen TOP und MODEM eingeschaltet, wenn `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP` gesetzt ist (IDF 5.5.4 `components/esp_adc/adc_oneshot.c`, `adc_oneshot_new_unit()`/`adc_oneshot_del_unit()`). Deshalb wird sie für jede Messung angelegt und gelöscht.
- **Pad GPIO 1:** nach Reset Eingang aus, hochohmig, ohne Pull (Datenblatt, IO-MUX-Tabelle, Reset-Wert 0). Der Treiber schaltet es als analog, ohne Pulls (`gpio_config_as_analog()`). Der Teiler wird also nicht durch einen internen Pull belastet.
- **Messintervall:** Jede Messung weckt den Chip einmal. Die Messdauer (16 Samples) ist **[UNBELEGT]**, mit dem PPK2 messen (Ereignis alle 15 min im Aufbau M1).
- **Ladestand-Tabelle ist ein Platzhalter [UNBELEGT]:** linear 3300 mV = 0 % bis 4200 mV = 100 %. Die echte Kurve ist in der Mitte flach (s. o.). Ersetzen durch Punkte aus der Entladekurve der verwendeten Zelle (Datenblatt oder eigene Messung).
- **Auflösung in HA:** `BatteryVoltage` hat nur 100 mV Auflösung (ZCL). Intern rechnet die Firmware in mV.
- **Speicherung:** Im Gerät nichts dauerhaft, nur die aktuellen Werte im RAM; nach einem Neustart wird neu gemessen. Die Historie hält Home Assistant. Wie oft Z2M Reports bekommt, bestimmt die Reporting-Konfiguration aus dem Interview **[UNBELEGT]**, am Gerät prüfen.

### Kalibrierung

Fehler ohne Kalibrierung (ungünstigster Fall): ADC ±46 mV Akku (Datenblatt Tab. 4-6, ATTEN3, ×2 Teiler) + Teiler 2 × 1 % ≈ ±42 mV bei 4,2 V (Rechnung) + Leckstrom des Pads am 500-kΩ-Knoten **[UNBELEGT]**. Diese Fehler wirken im Wesentlichen als Faktor; daher **Einpunkt-Kalibrierung pro Gerät** über `BATTERY_CAL_PERMILLE`:

**Schritt-für-Schritt-Anleitung mit Rechenbeispiel: [README.md, „Akkumessung kalibrieren“](../README.md#akkumessung-kalibrieren).** Kurzfassung: Quelle am 5V-Pin (ohne USB), Log über UART-Adapter. Bei mehreren Spannungen U_Log mit dem Multimeter **direkt am 5V-Pin** vergleichen, `CAL_neu = CAL_alt × U_Multimeter / U_Log`, Ergebnisse mitteln.

Bezugswert ist die Spannung am Pin, nicht die Einstellung der Quelle. Die Leitungen verlieren 0,1 V und mehr (siehe unten).

**Ergebnis Prototyp (2026-11-08):** PPK2 als Source Meter am 5V-Pin, ohne USB, ohne LED-Pfad, Onboard-WS2812 aktiv (DEBUG). Fluke 179 direkt an 5V-Pin und GND des Boards. Der Fluke misst im 6-V-Bereich auf ±(0,09 % + 2 Digits) genau ([Fluke-Datenblatt 175/177/179](https://www.farnell.com/datasheets/2631819.pdf)), das sind ≈ ±5 mV.

| PPK2 eingestellt | Fluke am 5V-Pin | ADC-Log (CAL 1000) | ADC / Fluke | CAL = 1000 × Fluke / ADC |
|---|---|---|---|---|
| 3000 mV | 2890 mV | 2928 mV | 1,0131 | 987 |
| 3300 mV | 3170 mV | 3212 mV | 1,0132 | 987 |
| 3500 mV | 3380 mV | 3426 mV | 1,0136 | 987 |
| 4200 mV | 4070 mV | 4126 mV | 1,0138 | 986 |

- Der ADC misst über den ganzen Bereich **gleichmäßig ≈ 1,35 % zu hoch**. Ein einziger Faktor genügt also, eine Zweipunkt-Kalibrierung ist nicht nötig. **`BATTERY_CAL_PERMILLE` = 987.** Rechnerische Restabweichung danach ≤ 3 mV, das liegt innerhalb der Fluke-Genauigkeit.
- Eine frühere Einzelprüfung bei 3,7 V schien eine exakte Übereinstimmung zu zeigen. Ich glaube es könnte so sein: Dabei wurde der ADC-Wert mit der am PPK2 eingestellten Spannung verglichen statt mit der Spannung am 5V-Pin. Die liegt ≈ 0,12–0,13 V tiefer (siehe nächster Punkt), und das hebt den ADC-Fehler von +1,35 % (≈ 50 mV) nicht auf. Ungeklärt.
- Zwischen PPK2-Einstellung und 5V-Pin fehlen bei Grundlast **120–130 mV**. Davon entfallen ≈ 23 mV auf den PPK2-Ausgang selbst (Messung in [power-measurement.md](power-measurement.md#ablauf-s10-reboot-schleife)). Der Rest liegt in Kabeln bzw. Steckbrett, das sind rechnerisch einige Ohm. Für Messungen mit Spannungsangaben deshalb immer die Spannung am 5V-Pin angeben, nicht die PPK2-Einstellung.

## GPIO-Belegung

Pinout ESP32-H2-Zero: linke Stiftleiste 5V, GND, 3V3, GPIO 0–5 (GPIO 1–5 = ADC1_CH0–CH4); rechte Stiftleiste GPIO 24 (TX), 23 (RX), 25, 22, 14, 13, 12, 11, 10; Pads auf der Unterseite GPIO 27/26 (USB), 9 (BOOT), 8.

Strapping-Pins laut Datenblatt: **nur GPIO 8, 9 und 25** – meiden. GPIO 13/14 sind die Anschlüsse für einen optionalen externen 32-kHz-Quarz (genauerer Sleep-Takt) und werden deshalb freigehalten. GPIO 8 hat auch nach Auslöten der WS2812 den Pull-up R1 (10 kΩ nach 3V3, siehe Schaltplan) – nicht auf Low treiben. Datenblatt Tab. 2-4: GPIO 9 hat beim Reset einen internen schwachen Pull-up (Bitwert 1), der BOOT-Taster zieht ihn auf Low; zusätzlich ist R6 (10 kΩ) extern bestückt.

| GPIO | Funktion | Status |
|---|---|---|
| 9 | Taster (BOOT), kurz = Toggle, ≥ 5 s = Factory Reset + Pairing – `BUTTON_GPIO` in [button.h](../main/button.h). Eingang mit internem Pull-up, Light-Sleep-Wakeup auf Low | am Prototyp bestätigt (Level 1); Wecken aus Light Sleep ungetestet |
| 10 | PWM → CE der KSQ (LEDC), Produktiv- und DEBUG-Build – `LIGHT_PWM_GPIO` | [VORSCHLAG], im Code hinterlegt |
| 11 | EN Boost-Wandler / Lastschalter, nur Produktiv-Build – `LIGHT_PWM_EN_GPIO` | [VORSCHLAG], im Code hinterlegt |
| 1 (ADC1_CH0) | Akkuspannung über Spannungsteiler 1 MΩ/1 MΩ + 100 nF – `BATTERY_ADC_GPIO` in [battery_config.h](../main/battery/battery_config.h). Im DEBUG-Build nur mit `CONFIG_ZL_DEBUG_BATTERY_ADC` | [VORSCHLAG], im Code hinterlegt |
| 8 | **Nur DEBUG Level 1:** Daten der Onboard-WS2812 (RMT), zeigt den Zustand der Lichterkette – `LIGHT_WS2812_GPIO` in [light_config.h](../main/light/light_config.h). In Level 0 und 2 ungenutzt | [IST] |

## Grobe Laufzeitabschätzung (LED an)

**Rechnung, keine Messung**, mit den Annahmen aus dem Abschnitt Wirkungsgrad. Energie = Nennkapazität × 3,7 V (Nennspannung als Annahme; die real entnehmbare Energie hängt von Zelle, Last und Abschaltschwelle ab). LED-Leistung = 100 mA × 3 V = 0,3 W.

| LED-Pfad | Aufnahme aus Akku | Leuchtdauer 3.000 mAh (11,1 Wh) | 5.000 mAh (18,5 Wh) |
|---|---|---|---|
| Boost 5 V + lineare KSQ (≈ 54 %) | 0,3 / 0,54 ≈ 0,56 W | ≈ 20 h | ≈ 33 h |
| Boost 3,7 V + lineare KSQ (≈ 73 %) | 0,3 / 0,73 ≈ 0,41 W | ≈ 27 h | ≈ 45 h |
| Lineare KSQ direkt am Akku (ohne Boost) | ≈ 100 mA Akkustrom | 3.000 mAh / 100 mA ≈ 30 h | ≈ 50 h |

**Vergleich Original-Timer mit 3× AA (Alkaline), 100 mA, 6 h/Tag** – aus Datenblatt-Diagrammen abgelesen (Ablesegenauigkeit grob ±10 %), Rechnung:
- Energizer E91 ([Datenblatt](https://cdn.connectec.uk/Files/Products/Batteries/energizer_batteries/e91_aa_battery_datasheet.pdf), Form EBC-1102-R): 100 mA Dauerentladung bis 0,8 V ≈ 2.500 mAh; Kurve „Digital Audio 100 mA 1 hr/day“: 1,1 V nach ≈ 22–23 h, 1,0 V nach ≈ 24–25 h, 0,8 V nach ≈ 26 h.
- Duracell MN1500 ([Datenblatt](https://www.ece.ucsb.edu/~ilan/Classes/ECE2A_F2010/Labs/Datasheets/Duracell%20AA%20MN1500%20LR6.pdf), 6/08): 100 mA, 1 h/Tag ≈ 2,7 Ah.
- Drei Zellen in Reihe, die Kette braucht 2,9 V: Schon bei ca. 1,0–1,1 V pro Zelle (3,0–3,3 V gesamt) reicht die Spannung kaum noch für 100 mA. Die Kapazität bis 0,8 V ist also nicht nutzbar. Nutzbar sind etwa **22–25 h Leuchtdauer ≈ 4 Tage bei 6 h/Tag**. Bei 6 h am Stück statt 1 h/Tag eher weniger (weniger Erholung, [UNBELEGT] wie viel); Eigenverbrauch des Timers [UNBELEGT].
- Zum Vergleich (Rechnung oben, ohne Boost): 18650 mit 3.000 mAh ≈ 30 h ≈ 5 Tage, mit 5.000 mAh ≈ 50 h ≈ 8 Tage bei 6 h/Tag.

Die letzte Zeile der Tabelle rechnet mit Strom statt Leistung, weil bei linearer Regelung der Akkustrom gleich dem LED-Strom ist; Nennkapazität voll entnehmbar angenommen (bis zur Abschaltschwelle Z8 ist es weniger).

Maßgeblich sind erst die Messwerte (siehe [power-measurement.md](power-measurement.md)). Im ausgeschalteten Zustand bestimmen Ruhestrom von Regler und abgeschaltetem Boost sowie ESP-Sleep + Zigbee-Polls die Laufzeit.

## Offene Punkte

- [TEILWEISE] LED-Kette: ursprünglich 3× AA, gemessen 2,9 V / 100 mA (2026-11-08, siehe [Messung](#messung-led-kette-2026-11-08)). Offen: Messpunkt der Spannung, Batteriespannung dabei, Anzahl LEDs, Verschaltung, Vorwiderstände, regelt der Original-Timer?
- [OFFEN] Boost weglassen, KSQ direkt am Akku? Hängt am Dropout der KSQ, siehe [Variante ohne Boost](#variante-ohne-boost-lineare-ksq-direkt-am-akku).
- [OFFEN] Boost-Modul: Typ, EN-Pin vorhanden, Leerlaufstrom, Ausgangsspannung einstellbar?
- [OFFEN] KSQ: Typ LD06AJSA mit CN5711 (Aufschrift geprüft), siehe [KSQ-Modul](#ksq-modul-ld06ajsa). Offen: Pull-up an CE auf dem Modul (Wert, entfernen?), Pull-down an CE.
- [OFFEN] Rückspeiseschutz USB ↔ Akku am 5V-Pin: Akku beim Flashen abklemmen oder Diode?
- [OFFEN] Ruhestrom nach Umbau messen (Soll ≈ 90 µA im Light Sleep) – Aufbau M2 in [power-measurement.md](power-measurement.md).
- [OFFEN] 18650 mit Schutzschaltung? Ladeelektronik (z. B. TP4056)?
