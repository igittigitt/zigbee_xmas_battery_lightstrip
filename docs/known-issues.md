# Bekannte Probleme & TODOs

Stand: 2026-11-07, nach der Umstellung auf esp-zigbee-lib 2.0.5 / ESP-IDF 5.5.4. Punkte grob nach Priorität sortiert. Erledigte Punkte stehen unten unter „Erledigt“.

## Zigbee / Power Management

1. **Hardware-Test teilweise erledigt.** Am 2026-11-07 ist der **Join an Z2M bestätigt** (DEBUG-Build, ESP32-H2-Zero am USB, Z-Stack 3.x.0 auf SLZB-06P10). Log: `Joined network: PAN ID 0x7285 …, channel 11, short address 0xbaf2`, nach 23 Fehlversuchen mit Status `0x03` = `EZB_BDB_STATUS_NO_NETWORK`, solange „Permit join“ aus war. Ein Steering-Versuch dauerte etwa 2,3 s plus 1 s Pause. Noch nicht geprüft: Interview/Exposes in Z2M, Schalten aus HA, Rejoin nach Neustart, echter Light Sleep (ohne USB), Reaktionszeit.
1a. **Flash-Größe:** Der Boot meldet `Detected size(4096k) larger than the size in the binary image header(2048k)`. Der Chip auf dem Board hat laut Erkennung 4 MB, `sdkconfig` steht auf dem Default 2 MB. Unkritisch, weil alle Partitionen unter 1 MB liegen. Zum Beseitigen der Warnung `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` in `sdkconfig.defaults` setzen.
1b. **RMT-Auflösung (DEBUG-Backend):** `rmt: channel resolution loss, real=10666666` statt 10 MHz (`LIGHT_WS2812_RMT_RESOLUTION_HZ`). Die WS2812-Bitzeiten sind damit rund 6 % kürzer als in `light_config.h` angegeben (z. B. T0H 0,28 µs statt 0,3 µs). Ich glaube es könnte so sein: Die Quelltaktrate ist 32 MHz, und 32/3 ergibt die gemeldeten 10,67 MHz. Eine Auflösung, die 32 MHz glatt teilt (z. B. 8 MHz), vermeidet das; die Tick-Werte müssen dann neu berechnet werden.
1c. **Rejoin nach Neustart scheiterte (2026-11-07).** Nach dem Flashen einer neuen Firmware ohne `erase-flash` (gleiche Partitionstabelle, Netzdaten vorhanden) scheiterte jede BDB-Initialisierung mit `BDB Device Reboot failed (status 0x03)`. Der Debug-Modus des Stacks (`CONFIG_ZB_DEBUG_MODE=y`) zeigte `[zdo_app_join.c] Comm failed in Rejoin (0x2c3)` und `… in TcRejoin (0x2c3)`.
   - `0x2c3` = Kategorie NWK (`ezbee/error.h`), Code `0xC3`. Laut Zigbee r22 ist das NOT_PERMITTED, „NLME-JOIN.request has been disallowed“ (Microchip `APS_Status_t`, ns-3 `zigbee-nwk.h`).
   - Auch „Permit join“ in Z2M half nicht.
   - Ausgeschlossen: das Setzen von `OnOff` vor der BDB-Initialisierung. Der Fehler blieb, nachdem es dahinter verschoben war.
   - **Rückwärtstest (Baseline ≈ erste funktionierende Firmware):** Join klappt. Rejoin nach RESET scheitert 4× (je ~5 s, Nachbartabelle leer: `0 neighbor(s)`) und klappt beim 5. Versuch, 35,7 s nach dem Start. Parent war der TI-Router `0xa84f` (`0x00124b0030cc2e6c`), nicht der Coordinator. Der Erst-Beitritt zeigte dasselbe Muster (Erfolg erst nach 46 s bzw. 83 s).
   - Ich glaube es könnte so sein: In den ersten ~30 s nach dem Start empfängt das Gerät beim Scan keine Beacons. Ursache (ESP oder TI-Router) unbekannt, ohne Sniffer nicht zu klären. Ein Anwender meldet dasselbe Symptom in esp-zigbee-sdk [#894](https://github.com/espressif/esp-zigbee-sdk/issues/894) (offen). Praktisch fängt der Rejoin-Backoff das ab.
   - Mit den späteren Änderungen klappte **nie** ein Rejoin. `light_sleep_enable = false` führte zu NWK `0xC3`. Die eigene Sperre `ESP_PM_NO_LIGHT_SLEEP` zusammen mit den Debug-Bibliotheken führte zu MAC `0xEA` (NO_BEACON). Der Rückwärtstest läuft, temporäre Schalter `ZB_BASELINE_TEST`/`ZB_TEST_NO_AWAKE_LOCK` in `zigbee_config.h`.
   - Espressif empfiehlt bei `EZB_NWK_NETWORK_STATUS_PARENT_LINK_FAILURE` einen „Leave mit Rejoin“ ([#837](https://github.com/espressif/esp-zigbee-sdk/issues/837), [#894](https://github.com/espressif/esp-zigbee-sdk/issues/894)). Noch nicht umgesetzt.
2. **Reaktionszeit Z4 ungeklärt.** `ZB_KEEP_ALIVE_MS` = 2000. Ob jeder Keepalive Befehle beim Parent abholt (MAC Data Request) oder nur ein End Device Timeout Request ist, ist nicht dokumentiert. Reaktionszeit und Ruhestrom messen und den Wert anpassen, siehe [power-management.md](power-management.md#reaktionszeit-ziel-z4-und-keepalive).
3. **Reaktion auf `PARENT_LOST` ist eine Annahme.** Rejoin per BDB INITIALIZATION mit Backoff (1 s … 5 min). Dass Espressif das so vorsieht, ist nicht dokumentiert, siehe [zigbee.md](zigbee.md#commissioning). Testen, z. B. Coordinator für > 64 min abschalten.
4. **Pairing-Fenster ohne Anzeige und ohne Taster.** Nach 3 min ohne Netz sucht das Gerät nicht weiter. Bis Z5/Z10 umgesetzt sind, hilft nur ein Neustart (Reset-Taste oder Strom aus und wieder an). `zigbee_device_start_pairing()` und `zigbee_device_factory_reset()` sind als Einstieg für den Taster vorhanden.
5. **Lokales Schalten meldet sich erst mit Reporting.** `on_off_cluster_set_state()` setzt das Attribut. Ob Z2M den Wert per Reporting bekommt, hängt von dessen Konfiguration beim Interview ab (Z2). Prüfen.
6. **`esp_timer`-Callback wartet auf den Zigbee-Lock** (`retry_timer_cb` in `zigbee_device.c`, Muster wie `alarm_timer.c` im SDK-Beispiel). Solange der Stack den Lock hält, blockiert er den esp_timer-Task. Ich glaube es könnte so sein: unkritisch, weil der Stack den Lock nur kurz hält. Alternative wäre `esp_zigbee_task_queue_post()`; deren Nutzung aus fremden Tasks ist aber nicht dokumentiert.
7. **Schlafstatistik im DEBUG-Build entfallen.** Sie hing am 1.x-Signal `CAN_SLEEP`. Möglicher Ersatz: `esp_pm_light_sleep_register_cbs()`, siehe [power-management.md](power-management.md#sleep-simulation-im-debug-build).
8. **Batteriewerte fehlen** (Z7): `BatteryVoltage`/`BatteryPercentageRemaining` stehen auf `0xFF` (unbekannt), im DEBUG-Build steht der Ladestand auf dem emulierten Wert. Es fehlen die ADC-Messung und eine `power_config_cluster_update()`-Funktion. Der Wert wird nur beim Start gesetzt, nicht aktiv gemeldet. Ob Z2M ihn beim Interview bzw. „Reconfigure“ selbst liest oder erst nach einer Änderung per Report bekommt, ist zu prüfen.
9. **Identify-Erkennung ungeprüft** (Z11): Angenommen ist, dass ein Identify-Befehl `SET_ATTR_VALUE` für `IdentifyTime` auslöst und TriggerEffect `EZB_ZCL_CORE_IDENTIFY_EFFECT_CB_ID`. Beides ist nicht dokumentiert, siehe [zigbee.md](zigbee.md#eingehende-aktionen).
10. **Persistenz von `OnOff`/`StartUpOnOff` am 2026-11-07 bestätigt** (Z6): `power_on_behavior` = `previous` aus Z2M kommt an (`StartUpOnOff set to 0xff`), und nach RESET mit eingeschaltetem Licht geht die LED wieder an. Ursprüngliche Notiz: Das gilt für `EZB_ZCL_ATTR_ACCESS_PERSISTENT` an Attributen der ZHA-Vorlage und für das Lesen der Werte bei `SKIP_STARTUP`, siehe [zigbee.md](zigbee.md#einschaltverhalten-z6). Mit `power_on_behavior` = `previous` testen: einschalten, Strom aus und wieder an. Jede Schaltänderung schreibt dann in den NVS. Ich glaube es könnte so sein: unkritisch für den Flash, weil NVS die Schreibvorgänge verteilt und nur wenige Schaltvorgänge pro Tag anfallen. Belegt ist das nicht.
11. **Licht nach Neustart erst nach dem Stack-Start:** `StartUpOnOff` wird erst bei `SKIP_STARTUP` ausgewertet, etwa 0,5 s nach dem Boot (Log: `Stack started` bei ~491 ms).

## Lichtmodul / DEBUG

15. **Sleep-Simulation (DEBUG) bildet die GPIO-Umschaltung nicht nach.** Pads mit `SleepSel=1` schaltet nur die Hardware beim echten Sleep um; dafür gibt es keine öffentliche API. Ersatz: GPIO-Bericht im Log, siehe [power-management.md](power-management.md#sleep-simulation-im-debug-build). (Das frühere Problem „USB-Log bricht im Light Sleep ab“ ist durch `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` per DEBUG behoben.)
16. **WS2812-Datenleitung im Light Sleep.** `light_driver_ws2812.c` ruft `gpio_sleep_sel_dis(GPIO 8)`. Mit `CONFIG_PM_SLP_DISABLE_GPIO=y` würden die Pins im Sleep sonst freigegeben, und R1 (10 kΩ) zöge DI auf High. Ich glaube es könnte so sein: So bleibt der RMT-Ruhepegel (Low) erhalten und die LED zeigt keine Fehlfarben. Am Prototyp beobachten.
17. **PWM-Backend ungetestet.** Es kompiliert, aber KSQ, Boost und Pins sind [OFFEN]/[VORSCHLAG]. PWM-Frequenz (1 kHz) und Boost-Settle-Zeit (10 ms) in `light_config.h` sind Platzhalter.
18. **RC_FAST im Sleep auch bei LED aus?** Mit `LEDC_SLEEP_MODE_KEEP_ALIVE` hält der LEDC-Treiber die Taktquelle im Sleep an (`ledc_timer_clk_src_set_xpd_in_sleep`, `esp_driver_ledc/src/ledc.c`). Ob das auch bei angehaltenem Timer (`ledc_timer_pause`) Ruhestrom kostet, ist [UNBELEGT]. Mit dem PPK2 messen; ggf. den Kanal bei LED aus mit anderem `sleep_mode` neu konfigurieren.
19. **LEDC `KEEP_ALIVE` verhindert das Abschalten der Peripherie-Domäne.** `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP` ist gesetzt. Der LEDC-Treiber lässt die Domäne bei `KEEP_ALIVE` aber eingeschaltet (IDF 5.5.4 `ledc.c`, Z. 435–449, 921–931). Ich glaube es könnte so sein: Das gilt auch bei LED aus, solange LEDC konfiguriert ist. Zusammen mit Punkt 18 messen.

## Repository / Umgebung

12. [README.md](../README.md) enthält noch Text und **1.x-API-Beispiele** aus dem Espressif-Beispiel (`esp_zb_*`, `HA_on_off_switch`, leere Abschnitte) sowie Projektstruktur-Angaben (`zigbee_node/`), die nicht zum Repo passen. Die gültige Doku steht in `docs/`.
13. `main/_OLD_esp_zb_light.c` ist ein Überbleibsel des Original-Beispiels (1.x-API) und nicht im Build. Löschen, sobald es nicht mehr als Referenz gebraucht wird.
14. `.gitignore` ignoriert `.vscode/`. Das Verzeichnis enthält aber projektrelevante Einstellungen mit lokalen Pfaden. **`idf.currentSetup` zeigt noch auf ESP-IDF 5.5.1.** Builds über die VS-Code-Extension scheitern daher an `idf: ~5.5.4` in `main/idf_component.yml`. Auf `C:\Users\muogeisen\esp\v5.5.4\esp-idf` umstellen.
20. [setup-build.md](setup-build.md) verweist für den Produktiv-Testbuild auf `release.defaults`. **Die Datei existiert nicht.** Sie müsste `CONFIG_ZL_DEBUG=n` enthalten.
21. **Kein Code-Size-Feintuning für mbedTLS.** Die SDK-Beispiele binden `examples/utils/example_common/sdkconfig.mbedtls_minimal` ein und schalten damit ungenutzte mbedTLS-Teile ab. Nicht übernommen, weil die Partition reicht: 586 KB von 940 KB belegt (Build 2026-11-07).

## Erledigt (2026-11-07)

- Onboard-LED zeigte statt Warmweiß Gelbgrün → Byte-Reihenfolge R,G,B (`LIGHT_WS2812_ORDER_RGB`).
- Identify ohne sichtbare Wirkung → Blinken 2 s / 5 Hz (Z11).
- `power_on_behavior` ohne Wirkung (Attribut `StartUpOnOff` fehlte) → ergänzt und ausgewertet (Z6).
- Akkustand im DEBUG-Build „N/A“ → emuliert (`CONFIG_ZL_DEBUG_BATTERY_PERCENT`).

- Build ging nicht → mit esp-zigbee-lib 2.0.5 / IDF 5.5.4 fehlerfrei, ohne Warnungen.
- Eigene Partitionstabelle wurde nicht verwendet → `CONFIG_PARTITION_TABLE_CUSTOM=y`. Die Tabelle ist an 2.x angepasst (Zigbee-Daten in `nvs`).
- `CONFIG_ZB_ZCZR` in `sdkconfig.defaults` → `CONFIG_ZB_ZED=y`.
- Zigbee-API aus fremdem Task ohne Lock → periodischer Timer entfernt; öffentliche Funktionen holen den Lock.
- Zigbee-Sleep unvollständig → 2.x-Sleep über `esp_pm` + Kconfig des Beispiels + `rx_on_when_idle = false`.
- `DEVICE_REBOOT` mit Fehler ohne Wiederholung → Rejoin mit Backoff.
- Endloses Network Steering → Pairing-Fenster 3 min (Z10).
- Device ID Temperatursensor / Model `ESP32-H2.Sensor` → On/Off Light, `zigbee-light` / `ZL-LightChain`.
- Simulierter Temperatur-Cluster → entfernt.
- Feste Batteriewerte 3,3 V / 100 % → `0xFF` (unbekannt) bis zur echten Messung.
- Ungenutzte Funktionen `power_management_light_sleep()`/`_deep_sleep()` und `stdlib.h`-Problem in `temperature_cluster.c` → entfernt.
