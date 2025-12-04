# Rechargeable Battery Powered Zigbee LED Christmas Lightstrip

Dieses Projekt dient keinem speziellen Zweck, es ist eher eine Machbarkeitsstudie und zum lernen.

## Hardware Required

* ESP32-H2
* Li-Ion Batterie (18650) 3.000 - 5.000 mAh
* LED KSQ-Shield (~100 mA)
* Buck-Boost Controller 2,0 .. 5,0 V Eingang, 3,3 V Ausgang, 0,5 - 1 A

## Configure the project

Before project configuration and build, make sure to set the correct chip target using `idf.py --preview set-target TARGET` command.

## Erase the NVRAM

Before flash it to the board, it is recommended to erase NVRAM if user doesn't want to keep the previous examples or other projects stored info using `idf.py -p PORT erase-flash`

## Build and Flash

Build the project, flash it to the board, and start the monitor tool to view the serial output by running `idf.py -p PORT flash monitor`.

(To exit the serial monitor, type ``Ctrl-]``.)

## Example Output


## Light Control Functions

 * By toggling the switch button (BOOT) on the ESP32-H2 board loaded with the `HA_on_off_switch` example, the LED on this board loaded with `HA_on_off_light` example will be on and off.

## Troubleshooting

# ESP32-H2 Zigbee Node für Z2MQTT

Modulare Zigbee-Codebasis für ESP32-H2 Supermini mit ESP-IDF 5.5.1 und VS Code.

## Features

- Modulare Cluster-Architektur
- Einfaches Hinzufügen neuer Cluster
- Attribute Change Handler
- Power Management (Light Sleep & Deep Sleep)
- Batteriebetrieb-Support
- Z2MQTT-kompatibel

## Projektstruktur

```
zigbee_node/
├── main/
│   ├── main.c                      # Hauptprogramm
│   ├── zigbee_config.h             # Zigbee-Konfiguration
│   ├── zigbee_device.[ch]          # Zigbee-Device-Management
│   ├── power_management.[ch]       # Power Management
│   ├── clusters/                   # Cluster-Implementierungen
│   │   ├── cluster_manager.[ch]    # Cluster-Manager
│   │   ├── on_off_cluster.[ch]     # On/Off Cluster
│   │   └── temperature_cluster.[ch] # Temperature Cluster
│   └── handlers/
│       └── attribute_handler.[ch]  # Attribute Change Handler
```

## Installation

### Voraussetzungen

1. **ESP-IDF 5.5.1** installiert
2. **VS Code** mit ESP-IDF Extension
3. **ESP32-H2 Supermini** Board

### Setup

```bash
# Projekt klonen/erstellen
mkdir zigbee_node && cd zigbee_node

# ESP-IDF Environment laden
. $HOME/esp/esp-idf/export.sh

# Projekt konfigurieren
idf.py set-target esp32h2
idf.py menuconfig

# Kompilieren und flashen
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Verwendung

### Neue Cluster hinzufügen

1. **Neuen Cluster erstellen** (`main/clusters/my_cluster.h`):

```c
#ifndef MY_CLUSTER_H
#define MY_CLUSTER_H

#include "esp_zigbee_core.h"

void my_cluster_add(esp_zb_cluster_list_t *cluster_list);
void my_cluster_attribute_changed(uint16_t attr_id, esp_zb_zcl_attr_data_t *value);
void my_cluster_update(void);

#endif
```

2. **Cluster implementieren** (`main/clusters/my_cluster.c`):

```c
#include "esp_log.h"
#include "my_cluster.h"
#include "zigbee_config.h"

static const char *TAG = "MY_CLUSTER";

void my_cluster_add(esp_zb_cluster_list_t *cluster_list)
{
    // Cluster-Konfiguration
    esp_zb_attribute_list_t *cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_XXX);
    
    // Attribute hinzufügen
    esp_zb_cluster_add_attr(cluster, ESP_ZB_ZCL_CLUSTER_ID_XXX, 
                            ESP_ZB_ZCL_ATTR_XXX_ID, 
                            ESP_ZB_ZCL_ATTR_TYPE_XXX, 
                            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, 
                            &value);
    
    esp_zb_cluster_list_add_xxx_cluster(cluster_list, cluster, 
                                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    ESP_LOGI(TAG, "Cluster added");
}

void my_cluster_attribute_changed(uint16_t attr_id, esp_zb_zcl_attr_data_t *value)
{
    // Auf Attributänderungen reagieren
    ESP_LOGI(TAG, "Attribute changed: 0x%x", attr_id);
}

void my_cluster_update(void)
{
    // Attribute aktualisieren (z.B. Sensorwerte)
    // esp_zb_zcl_set_attribute_val(...);
}
```

3. **Cluster registrieren** in `cluster_manager.c`:

```c
#include "my_cluster.h"

esp_zb_cluster_list_t *cluster_manager_create_clusters(void)
{
    // ... existing code ...
    
    my_cluster_add(cluster_list);  // Cluster hinzufügen
    
    return cluster_list;
}

void cluster_manager_update_attributes(void)
{
    // ... existing code ...
    
    my_cluster_update();  // Update hinzufügen
}
```

4. **Handler registrieren** in `attribute_handler.c`:

```c
case ESP_ZB_ZCL_CLUSTER_ID_XXX:
    my_cluster_attribute_changed(message->attribute.id, &message->attribute.data);
    break;
```

5. **CMakeLists.txt aktualisieren**:

```cmake
SRCS 
    "main.c"
    "zigbee_device.c"
    "clusters/my_cluster.c"  # Neue Datei hinzufügen
    ...
```

### Power Management konfigurieren

In `power_management.h`:

```c
#define SLEEP_INTERVAL_MS           30000  // Sleep-Intervall
#define LIGHT_SLEEP_ENABLED         1      // Light Sleep aktivieren
#define DEEP_SLEEP_ENABLED          0      // Deep Sleep aktivieren
```

**Light Sleep:**
- Erhält Zigbee-Verbindung
- Schnelles Aufwachen
- Ideal für Sensoren mit häufigen Updates

**Deep Sleep:**
- Trennt Zigbee-Verbindung
- Sehr niedriger Stromverbrauch
- Erfordert Network-Rejoin beim Aufwachen
- Ideal für Sensoren mit sehr seltenen Updates (z.B. alle 15 Minuten)

## Hardware-Anpassungen

### GPIO-Pins anpassen

In `on_off_cluster.c`:

```c
#define LED_GPIO GPIO_NUM_8  // An deine Hardware anpassen
```

### Sensoren anbinden

In `temperature_cluster.c`:

```c
void temperature_cluster_update(void)
{
    // Beispiel: BME280 Sensor lesen
    int16_t temp = bme280_read_temperature();
    temperature_cluster_set_value(temp);
}
```

## Z2MQTT Integration

Der Node erscheint automatisch in Z2MQTT nach dem Pairing. Zum Pairing:

1. Z2MQTT in Pairing-Modus versetzen
2. ESP32-H2 starten oder Reset drücken
3. LED blinkt während des Pairings
4. Nach erfolgreichem Pairing erscheint das Gerät in Z2MQTT

### Konfiguration in Z2MQTT

Das Gerät wird automatisch erkannt als:
- **Manufacturer:** Espressif
- **Model:** ESP32-H2.Sensor
- **Endpoint:** 1

Verfügbare Entities (je nach Clustern):
- `switch` (On/Off Cluster)
- `sensor.temperature` (Temperature Cluster)
- `sensor.battery` (Power Configuration)

## Debugging

```bash
# Monitor mit Log-Output
idf.py monitor

# Log-Level anpassen in sdkconfig.defaults
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
```

## Best Practices

1. **Modularer Aufbau:** Jeder Cluster in separater Datei
2. **Error Handling:** Immer Rückgabewerte prüfen
3. **Logging:** Aussagekräftige Log-Messages verwenden
4. **Power Management:** Bei Batteriebetrieb Light Sleep nutzen
5. **Attribute Updates:** Nur bei tatsächlichen Änderungen senden

## Beispiel: Humidity Cluster hinzufügen

1. Erstelle `humidity_cluster.c` und `humidity_cluster.h`
2. Implementiere nach dem Pattern von `temperature_cluster.c`
3. Registriere in `cluster_manager.c`
4. Füge Handler in `attribute_handler.c` hinzu
5. Update `CMakeLists.txt`

## Troubleshooting

**Device joined nicht:**
- Zigbee Channel prüfen (muss mit Coordinator übereinstimmen)
- InstallCode Policy prüfen
- Z2MQTT Logs checken

**Hoher Stromverbrauch:**
- Power Management aktivieren
- Sleep-Intervall anpassen
- Nicht benötigte Peripherie deaktivieren

**Attribute Updates kommen nicht an:**
- Reporting konfigurieren in `esp_zb_zcl_reporting_info_t`
- Network Stability prüfen
- Signal Strength checken

## Lizenz

MIT License - frei verwendbar für private und kommerzielle Projekte.

## Credits

Basierend auf Espressif ESP-IDF Zigbee Examples