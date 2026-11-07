# AGENTS.md

Hinweise für KI-Coding-Agents (Claude Code, Codex, Copilot …), die an diesem Repository arbeiten.

## Projekt in einem Satz

Firmware für eine batteriebetriebene Zigbee-LED-Lichterkette: **ESP32-H2**, **ESP-IDF 5.5.4**, **esp-zigbee-lib 2.0.5** (native 2.x-API `esp_zigbee_*`/`ezb_*`, kein ZBOSS), Rolle **Sleepy End Device**, Ziel **Zigbee2MQTT**. Sprache: C (C17), Build: CMake über `idf.py`. Versionsentscheidung und Programmiermodell: [docs/zigbee-framework.md](docs/zigbee-framework.md).

Ausführliche Doku: [docs/README.md](docs/README.md). **Verbindlicher Umfang (in/out of scope): [docs/goals.md](docs/goals.md)** – keine Features außerhalb davon ohne Rückfrage. Energieeffizienz hat oberste Priorität.

## Oberste Regel: Nichts erfinden

Hier kommt es auf Präzision an – nach den Angaben wird Hardware gekauft und umgebaut.

- Technische Aussagen (Bauteilwerte, Ströme, Spannungen, Pinbelegungen, API-Verhalten, Zigbee-/Z2M-Details) **nur mit belegbarer Quelle** schreiben. Primärquellen bevorzugen: Datenblätter in [datasheets/](datasheets/), Hersteller-Dokumentation (Espressif, Bauteilhersteller), Spezifikationen, Quellcode.
- **Nicht einer einzelnen Quelle vertrauen**, mehrere unabhängige Quellen abgleichen. Forenbeiträge nie ungeprüft übernehmen.
- In der Doku die Quelle angeben (Datenblatt + Tabelle/Seite oder URL).
- Rechnungen mit ihren Annahmen kennzeichnen; eine Rechnung ist kein Messwert.
- Wenn etwas unbekannt ist: das so sagen bzw. als **[UNBELEGT]** markieren.
- Vermutungen ausdrücklich so einleiten: **„Ich glaube es könnte so sein: …“**

## Befehle

```bash
idf.py set-target esp32h2      # nur nach frischem Checkout / fullclean
idf.py build                   # muss nach jeder Änderung fehlerfrei durchlaufen
idf.py -p COM5 flash monitor   # nur auf ausdrückliche Anweisung (braucht Hardware)
idf.py -p COM5 erase-flash     # NIE ohne Rückfrage – löscht Netzwerk-Pairing
```

Es gibt keine Unit-Tests. Verifikation = erfolgreicher `idf.py build` ohne neue Warnungen. Wenn ESP-IDF in der Umgebung nicht verfügbar ist, das klar sagen, statt einen Build-Erfolg zu behaupten. Benötigt wird ESP-IDF **5.5.4** (`idf: ~5.5.4` in `main/idf_component.yml`); lokal unter `C:\Users\muogeisen\esp\v5.5.4\esp-idf`, Aktivierung siehe [docs/setup-build.md](docs/setup-build.md#build-aus-der-powershell).

## Code-Landkarte

| Datei | Zuständigkeit |
|---|---|
| `main/main.c` | `app_main`: NVS, Power Mgmt, Licht, startet Zigbee-Task |
| `main/zigbee_config.h` | Zentrale Zigbee-Konstanten (Identität, Endpoint, Keepalive, Pairing/Rejoin-Zeiten) |
| `main/zigbee_device.c` | Stack-Init, Commissioning (Pairing-Fenster, Rejoin-Backoff), Signal-Handler; API `zigbee_device_start_pairing/_factory_reset/_is_joined` |
| `main/clusters/cluster_manager.c` | Baut Endpoint aus ZHA-Vorlage „On/Off Light“ + Zusatzcluster und registriert ihn |
| `main/clusters/*_cluster.c` | Je ein ZCL-Cluster (`on_off` ↔ Licht, `power_config` Akku) |
| `main/handlers/attribute_handler.c` | Routet eingehende ZCL-Aktionen nach Cluster-ID |
| `main/power_management.c` | `esp_pm` (automatischer Light Sleep), DEBUG-Sleep-Bericht |
| `main/Kconfig.projbuild`, `main/app_config.h` | Build-Schalter **DEBUG** (`CONFIG_ZL_DEBUG` → `APP_DEBUG` 0/1). Debug-Abweichungen nur hierüber steuern, siehe [docs/setup-build.md](docs/setup-build.md#debug-schalter) |
| `main/light/light.c` | Lichtsteuerung: Task, Rampen, API `light_set_on`/`light_fade_to` (nicht blockierend) |
| `main/light/light_driver_*.c` | Backends hinter `light_driver_t`: `ws2812` (DEBUG, Onboard-LED), `pwm` (Produktiv, LEDC + Boost-EN) |
| `main/_OLD_esp_zb_light.c` | Altes Beispiel (1.x-API), **nicht** im Build, nicht ändern, nicht als API-Vorlage nutzen |

## Konventionen

- Neue Cluster nach dem Muster in [docs/extending.md](docs/extending.md): `<name>_cluster.[ch]` in `main/clusters/`, Funktionen `<name>_cluster_add/_update/_attribute_changed`, Registrierung in `cluster_manager.c`, Routing in `attribute_handler.c`, Quelle in `main/CMakeLists.txt` eintragen.
- Modulzustand als `static` in der `.c`-Datei; öffentliche API mit Doxygen-Kommentar im Header.
- Logging nur über `ESP_LOGx` mit eigenem `static const char *TAG` pro Modul.
- Fehler über `esp_err_t` zurückgeben; `ESP_ERROR_CHECK` nur bei Init-Fehlern, bei denen ein Weiterlaufen sinnlos ist.
- Konfigurierbare Werte als `#define` in Headern bzw. als Kconfig in `sdkconfig.defaults` – keine Magic Numbers im Code.
- Einrückung 4 Leerzeichen, Klammern im Stil der bestehenden Dateien.
- Doku und Kommentare dürfen Deutsch oder Englisch sein; bestehende Code-Kommentare sind Englisch, Doku ist Deutsch.

## Zigbee-spezifische Regeln

- **Nur die 2.x-API verwenden** (`esp_zigbee.h`, `ezbee/*.h`). Keine `esp_zb_*`-Funktionen aus 1.x und keine Kompatibilitätsschicht (`CONFIG_ZB_SDK_1xx`). API-Fragen gegen die Header in `managed_components/espressif__esp-zigbee-lib/include` bzw. das SDK auf Commit `e2fdf4ad` (= 2.0.5) prüfen, nicht gegen den `main`-Branch (2.1.x).
- **Thread-Safety:** Zigbee-APIs (`ezb_*`, `esp_zigbee_*`) außerhalb des Zigbee-Tasks und seiner Callbacks nur zwischen `esp_zigbee_lock_acquire(portMAX_DELAY)` und `esp_zigbee_lock_release()` aufrufen. Zeitgesteuerte Aktionen: `esp_timer` und im Callback den Lock holen (Muster `retry_timer_cb` in `zigbee_device.c`).
- **Schlaf** steuert ESP-IDF (`esp_pm`, Tickless Idle); es gibt keinen Sleep-Aufruf der Anwendung. Nichts einbauen, das dauerhaft PM-Locks hält oder periodisch weckt (Ziel Z9).
- Im Signal-Handler und in Action-Callbacks nicht blockieren (kein `vTaskDelay`, keine langen I/O-Operationen).
- Attributwerte in ZCL-Einheiten: Batteriespannung 100 mV, Batterieprozent 0–200 (= 0–100 %), `0xFF` = unbekannt.
- Änderungen an Endpoint, Device ID oder Clusterliste erfordern in Z2M ein erneutes Interview – im Ergebnis erwähnen.

## Konfiguration

- `sdkconfig` und `sdkconfig.old` sind generiert und gitignored – Änderungen gehören in `sdkconfig.defaults`.
- `managed_components/` und `build/` nie bearbeiten oder committen.
- Komponenten-Versionen in `main/idf_component.yml` nur nach Rückfrage anheben (`dependencies.lock` ändert sich mit). esp-zigbee-lib ist absichtlich exakt auf `==2.0.5` gepinnt (Begründung in [docs/zigbee-framework.md](docs/zigbee-framework.md#entscheidung-2026-11-07)).

## Bekannte Baustellen

Vor größeren Änderungen [docs/known-issues.md](docs/known-issues.md) lesen. Besonders relevant: Firmware auf Hardware ungetestet, Reaktionszeit/Keepalive (Z4) ungeklärt, `.vscode` zeigt noch auf IDF 5.5.1. Wird ein Punkt behoben, den Eintrag dort entfernen bzw. aktualisieren.

## Dokumentation pflegen

Bei Änderungen an Clustern, GPIOs, Power-Management oder Build-Konfiguration die passende Datei in `docs/` mit aktualisieren.
