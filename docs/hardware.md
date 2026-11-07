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
- **Arbeitsbereich Eingangsspannung (5V-Pin):** ca. 3,05 V bis **5,5 V** (Obergrenze RT9080; 6,5 V ist nur Absolute Maximum).
- **Restkapazität unter 3,3 V** – kein Datenblatt nennt sie für die Ruhespannung. Verfügbare Anhaltspunkte, die sich nicht decken:
  - Panasonic NCR18650B, Entladekurve 0,2 C, 25 °C (**unter Last**, Werte aus der Kurve abgelesen): ca. 6–8 % der Kapazität (bis 2,5 V) liegen unter 3,3 V, ca. 1–2 % unter 3,0 V. Quelle: Datenblatt, gehostet bei [batteryjunction](https://files.batteryjunction.com/frontend/files/Panasonic-NCR18650B.pdf) und TI E2E (zwei übereinstimmende Kopien, nicht vom Hersteller selbst).
  - Beispiel-OCV-Tabelle von TI ([SLUAAR3](https://www.ti.com/lit/SLUAAR3), Fig. 2-1, generische Zelle): 3,38 V ≙ 20 %, 3,20 V ≙ 10 % → 3,3 V Ruhespannung ≈ 15 % Ladestand.
  - Ich glaube es könnte so sein: Für unsere konkrete Zelle liegt der Wert irgendwo zwischen diesen Angaben; er hängt von Zellchemie, Temperatur und Alterung ab. **Klärung per Messung** (Entladung der eigenen Zelle) sinnvoll, bevor die Schwelle Z8 festgelegt wird.
- Entladeschlussspannung laut Datenblättern: Samsung INR18650-35E 2,65 V; Samsung 30Q, LG MJ1, Panasonic NCR18650B 2,5 V. 3,0 V nennt keines davon – das ist eine konservative eigene Wahl.
- Ob ein Buck-Boost die Restkapazität sinnvoll nutzbar machen würde, hängt von dieser Restkapazität und dem Ruhestrom des Wandlers ab – [OFFEN]. Abschaltschwelle Z8 vorläufig ca. 3,2–3,3 V Ruhespannung (Hysterese, Messung bei LED aus), Festlegung nach Klärung.
- ⚠️ **5V-Pin = VBUS** (direkt verbunden, siehe Schaltplan). Bei gestecktem USB und angeschlossenem Akku speist USB mit 5 V in den Akku zurück. Vor dem Flashen Akku abklemmen oder eine Schottky-/Ideal-Diode in die Akkuleitung setzen (kostet ~0,2–0,3 V Reserve).
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

**Software-Regeln:** Messen bei LED aus (bzw. einige Sekunden nach Ausschalten), Mittelung über mehrere Samples, OCV-Lookup-Tabelle für %, Unterspannungsentscheidung erst nach mehreren Messungen in Folge, mit Hysterese. Messintervall 10–30 min, gebündelt mit ohnehin nötigen Wakeups, zusätzlich bei jedem LED-Schaltvorgang.

## GPIO-Belegung

Pinout ESP32-H2-Zero: linke Stiftleiste 5V, GND, 3V3, GPIO 0–5 (GPIO 1–5 = ADC1_CH0–CH4); rechte Stiftleiste GPIO 24 (TX), 23 (RX), 25, 22, 14, 13, 12, 11, 10; Pads auf der Unterseite GPIO 27/26 (USB), 9 (BOOT), 8.

Strapping-Pins laut Datenblatt: **nur GPIO 8, 9 und 25** – meiden. GPIO 13/14 sind die Anschlüsse für einen optionalen externen 32-kHz-Quarz (genauerer Sleep-Takt) und werden deshalb freigehalten. GPIO 8 hat auch nach Auslöten der WS2812 den Pull-up R1 (10 kΩ nach 3V3, siehe Schaltplan) – nicht auf Low treiben. Datenblatt Tab. 2-4: GPIO 9 hat beim Reset einen internen schwachen Pull-up (Bitwert 1), der BOOT-Taster zieht ihn auf Low; zusätzlich ist R6 (10 kΩ) extern bestückt.

| GPIO | Funktion | Status |
|---|---|---|
| 9 | Taster (BOOT), kurz = Toggle, lang = Pairing | [ZIEL] |
| 10 | PWM → KSQ (LEDC), nur Produktiv-Build – `LIGHT_PWM_GPIO` | [VORSCHLAG], im Code hinterlegt |
| 11 | EN Boost-Wandler / Lastschalter, nur Produktiv-Build – `LIGHT_PWM_EN_GPIO` | [VORSCHLAG], im Code hinterlegt |
| 1 (ADC1_CH0) | Akkuspannung über Spannungsteiler 1 MΩ/1 MΩ + 100 nF | [VORSCHLAG] |
| 8 | **Nur DEBUG-Build:** Daten der Onboard-WS2812 (RMT), simuliert die Lichterkette – `LIGHT_WS2812_GPIO` in [light_config.h](../main/light/light_config.h). Im Produktiv-Build ungenutzt | [IST] |

## Grobe Laufzeitabschätzung (LED an)

**Rechnung, keine Messung**, mit den Annahmen aus dem Abschnitt Wirkungsgrad. Energie = Nennkapazität × 3,7 V (Nennspannung als Annahme; die real entnehmbare Energie hängt von Zelle, Last und Abschaltschwelle ab). LED-Leistung = 100 mA × 3 V = 0,3 W.

| LED-Pfad | Aufnahme aus Akku | Leuchtdauer 3.000 mAh (11,1 Wh) | 5.000 mAh (18,5 Wh) |
|---|---|---|---|
| Boost 5 V + lineare KSQ (≈ 54 %) | 0,3 / 0,54 ≈ 0,56 W | ≈ 20 h | ≈ 33 h |
| Boost 3,7 V + lineare KSQ (≈ 73 %) | 0,3 / 0,73 ≈ 0,41 W | ≈ 27 h | ≈ 45 h |

Maßgeblich sind erst die Messwerte (siehe [power-measurement.md](power-measurement.md)). Im ausgeschalteten Zustand bestimmen Ruhestrom von Regler und abgeschaltetem Boost sowie ESP-Sleep + Zigbee-Polls die Laufzeit.

## Offene Punkte

- [OFFEN] LED-Kette: Anzahl LEDs, Verschaltung (parallel?), eingebaute Vorwiderstände, ursprüngliche Versorgung (USB 5 V / 3×AA)?
- [OFFEN] Boost-Modul: Typ, EN-Pin vorhanden, Leerlaufstrom, Ausgangsspannung einstellbar?
- [OFFEN] KSQ-Shield: Typ, linear oder getaktet, Dimm-/EN-Eingang (Logikpegel, max. PWM-Frequenz), Dropout.
- [OFFEN] Rückspeiseschutz USB ↔ Akku am 5V-Pin: Akku beim Flashen abklemmen oder Diode?
- [OFFEN] Ruhestrom nach Umbau messen (Soll ≈ 90 µA im Light Sleep) – Aufbau M2 in [power-measurement.md](power-measurement.md).
- [OFFEN] 18650 mit Schutzschaltung? Ladeelektronik (z. B. TP4056)?
