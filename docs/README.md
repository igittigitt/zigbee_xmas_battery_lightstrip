# Dokumentation – zigbee-light

Batteriebetriebene Zigbee-LED-Lichterkette auf Basis des **ESP32-H2**, gebaut mit **ESP-IDF 5.5.4** und dem Espressif-Zigbee-SDK (`esp-zigbee-lib` **2.0.5**). Ziel ist ein schlafendes Zigbee End Device (Sleepy End Device), das in **Zigbee2MQTT** auftaucht, per On/Off geschaltet werden kann und möglichst stromsparend läuft.

> Status: Prototyp. Die Firmware baut fehlerfrei, ist aber auf Hardware noch nicht getestet. Taster und Tiefentladeschutz fehlen noch; die Akkumessung ist umgesetzt, aber ohne Hardware ungetestet. Siehe [Bekannte Probleme](known-issues.md).

## Inhalt

| Dokument | Inhalt |
|---|---|
| [goals.md](goals.md) | **Fachliche Zielbeschreibung: was wir tun und was nicht** |
| [hardware.md](hardware.md) | Bauteile, Stromversorgung, GPIO-Belegung |
| [setup-build.md](setup-build.md) | Toolchain, Devcontainer, Build, Flash, Monitor |
| [zigbee-framework.md](zigbee-framework.md) | Zigbee-Bibliotheken, Versionen, Kompatibilität, Programmiermodell, 1.x vs. 2.x |
| [architecture.md](architecture.md) | Modulaufbau, Startablauf, Datenfluss |
| [zigbee.md](zigbee.md) | Gerätemodell, Endpoint, Cluster, Commissioning, Zigbee2MQTT |
| [power-management.md](power-management.md) | Automatischer Light Sleep, Keepalive/Reaktionszeit, DEBUG-Sleep-Simulation |
| [power-measurement.md](power-measurement.md) | Strommessung mit Nordic PPK2: Aufbauten, Szenarien, Laufzeitrechnung |
| [extending.md](extending.md) | Neue Cluster hinzufügen (Schritt für Schritt) |
| [known-issues.md](known-issues.md) | Offene Punkte, Inkonsistenzen, TODOs |

## Schnellstart

```bash
idf.py set-target esp32h2
idf.py -p COM5 erase-flash      # einmalig, löscht alte Zigbee-Netzwerkdaten
idf.py -p COM5 flash monitor
```

Danach in Zigbee2MQTT „Permit join“ aktivieren. Das Gerät sucht 3 min lang nach einem Netz und meldet sich als `zigbee-light` / `ZL-LightChain` (siehe [zigbee.md](zigbee.md#zigbee2mqtt)).
