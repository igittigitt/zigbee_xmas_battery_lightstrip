# Neue Cluster hinzufügen

API-Stand: esp-zigbee-lib 2.0.5 (`ezb_*`), siehe [zigbee-framework.md](zigbee-framework.md#programmiermodell-205). Als Beispiel dient der Cluster **Level Control** (`0x0008`, `EZB_ZCL_CLUSTER_ID_LEVEL`). Dimmen ist im Projekt zurückgestellt ([goals.md](goals.md)); das Beispiel zeigt nur das Vorgehen.

Grundregel: Cluster, die die ZHA-Vorlage schon mitbringt (Basic, Identify, Groups, Scenes, On/Off und Level Control beim „Dimmable Light“), werden **nicht** zusätzlich angelegt. Ihr Modul behandelt nur Attributänderungen, Beispiel [on_off_cluster.c](../main/clusters/on_off_cluster.c). Neue Cluster werden wie unten an den Endpoint gehängt, Beispiel [power_config_cluster.c](../main/clusters/power_config_cluster.c).

## 1. Header – `main/clusters/level_cluster.h`

```c
#ifndef LEVEL_CLUSTER_H
#define LEVEL_CLUSTER_H

#include "esp_err.h"
#include "esp_zigbee.h"

/** Add the Level Control cluster (server) to the endpoint (before registration). */
esp_err_t level_cluster_add(ezb_af_ep_desc_t ep_desc);

/** Handle a changed attribute (Zigbee task, must not block). */
void level_cluster_attribute_changed(const ezb_zcl_attribute_t *attribute);

#endif // LEVEL_CLUSTER_H
```

## 2. Implementierung – `main/clusters/level_cluster.c`

```c
#include "esp_check.h"
#include "esp_log.h"
#include "level_cluster.h"

static const char *TAG = "LEVEL_CLUSTER";

esp_err_t level_cluster_add(ezb_af_ep_desc_t ep_desc)
{
    const ezb_zcl_level_cluster_server_config_t cfg = {
        .current_level = EZB_ZCL_LEVEL_CURRENT_LEVEL_DEFAULT_VALUE,
    };
    ezb_zcl_cluster_desc_t cluster = ezb_zcl_level_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER);
    ESP_RETURN_ON_FALSE(cluster, ESP_ERR_NO_MEM, TAG, "Cluster allocation");
    return esp_zigbee_err_to_esp(ezb_af_endpoint_add_cluster_desc(ep_desc, cluster));
}

void level_cluster_attribute_changed(const ezb_zcl_attribute_t *attribute)
{
    if (attribute->id == EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID && attribute->data.value) {
        uint8_t level = *(const uint8_t *)attribute->data.value;
        ESP_LOGI(TAG, "CurrentLevel %u", level);
        // light_fade_to(level, ...);   // non-blocking, light task
    }
}
```

Attributwerte, die das Gerät selbst ändert (z. B. Messwerte), setzt man mit `ezb_zcl_set_attr_value(ep, cluster_id, EZB_ZCL_CLUSTER_SERVER, attr_id, EZB_ZCL_STD_MANUF_CODE, &value, false)`. Außerhalb des Zigbee-Tasks muss der Aufruf zwischen `esp_zigbee_lock_acquire(portMAX_DELAY)` und `esp_zigbee_lock_release()` stehen; Beispiel: `on_off_cluster_set_state()`.

## 3. In `cluster_manager.c` registrieren

```c
#include "level_cluster.h"
// in cluster_manager_register_device(), vor ezb_af_device_add_endpoint_desc():
ESP_RETURN_ON_ERROR(level_cluster_add(ep_desc), TAG, "Level Control");
```

Passt danach die Gerätevorlage nicht mehr, z. B. bei Dimmen „Dimmable Light“ (`0x0101`) statt „On/Off Light“, die ZHA-Vorlage in `cluster_manager.c` wechseln (`ezbee/zha.h`).

## 4. Attributänderungen in `attribute_handler.c` routen

```c
case EZB_ZCL_CLUSTER_ID_LEVEL:
    level_cluster_attribute_changed(&message->in.attribute);
    break;
```

## 5. In `main/CMakeLists.txt` eintragen

```cmake
SRCS
    ...
    "clusters/level_cluster.c"
```

Zusätzliche IDF-Komponenten (z. B. `esp_adc`) unter `REQUIRES` ergänzen.

## Checkliste

- [ ] `.c`/`.h` angelegt, Funktionsnamen nach Schema `<name>_cluster_*`
- [ ] in `cluster_manager_register_device()` eingetragen (nur wenn nicht schon in der ZHA-Vorlage)
- [ ] ggf. Routing in `attribute_handler.c`
- [ ] Quelle in `main/CMakeLists.txt`
- [ ] Gerätevorlage / Device ID noch passend?
- [ ] Zigbee-API außerhalb des Zigbee-Tasks nur mit Lock
- [ ] `idf.py build` ohne neue Warnungen; nach dem Flashen in Z2M neu interviewen
- [ ] Doku aktualisieren: [zigbee.md](zigbee.md), Log-Tag in [setup-build.md](setup-build.md#logging)

## Typische Erweiterungen für dieses Projekt

- **Akkumessung (Z7):** ADC in [power_config_cluster.c](../main/clusters/power_config_cluster.c), Funktion `power_config_cluster_update()` mit Lock.
- **Attribut zu einem Vorlagen-Cluster hinzufügen:** Beispiel `StartUpOnOff` in `on_off_cluster_add()`: `ezb_af_endpoint_get_cluster_desc()` → `ezb_zcl_<cluster>_cluster_desc_add_attr()`. Bei Bedarf `ezb_zcl_attr_desc_set_access(… | EZB_ZCL_ATTR_ACCESS_PERSISTENT)`, damit der Wert einen Neustart übersteht.
- **Taster (Z5):** kurz → `on_off_cluster_set_state()`, lang → `zigbee_device_factory_reset()`, nach abgelaufenem Pairing-Fenster → `zigbee_device_start_pairing()`.
