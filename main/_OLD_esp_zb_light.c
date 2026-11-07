/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier:  LicenseRef-Included
 *
 * Zigbee HA_on_off_light Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_zigbee_core.h"
#include "esp_timer.h"
#include "driver/rtc_io.h"


#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile light (End Device) source code.
#endif

#define ESP_ZB_ZCL_BASIC_POWER_SOURCE_BATTERY 0x03

/* Zigbee configuration */
#define INSTALLCODE_POLICY_ENABLE       false                                /* enable the install code policy for security */
#define ED_AGING_TIMEOUT                ESP_ZB_ED_AGING_TIMEOUT_64MIN        /* aging timeout of device */
#define ED_KEEP_ALIVE                   3000                                 /* 3000 millisecond */
#define ESP_ZED_ENDPOINT                1                                    /* esp light device endpoint */
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK /* Zigbee primary channel mask use in the example */

/* Basic manufacturer information */
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"      /* Customized manufacturer name */
#define ESP_MODEL_IDENTIFIER "\x07"CONFIG_IDF_TARGET /* Customized model identifier */

#define ESP_ZB_ZED_CONFIG()                                         \
    {                                                               \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,                       \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,           \
        .nwk_cfg.zed_cfg = {                                        \
            .ed_timeout = ED_AGING_TIMEOUT,                         \
            .keep_alive = ED_KEEP_ALIVE,                            \
        },                                                          \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }


static TimerHandle_t identify_timer = NULL;
static TimerHandle_t identify_timeout_timer = NULL;

// GPIO für PWM (für Helligkeit)
#define LIGHT_PWM_GPIO              13
#define LEDC_TIMER_RESOLUTION       LEDC_TIMER_8_BIT
#define LEDC_MAX_DUTY               (1 << LEDC_TIMER_RESOLUTION) - 1


// Wakeup Button GPIO ("BOOT" Button on ESP32-H2-DevKitC-1)
//#define BOOT_BUTTON_GPIO GPIO_NUM_9
//#define WAKEUP_TIME_US   (60ULL * 1000000ULL) // 60 Sekunden
#define INACTIVITY_TIMEOUT_S    15      // Zeit bis Sleep nach letzter Aktivität
#define SLEEP_DURATION_S        60      // Wie lange geschlafen wird
#define BOOT_BUTTON_GPIO        GPIO_NUM_9

#define MANUF_ID            0x1234
#define CUSTOM_CLUSTER_ID   0xFC00
#define ATTR_SLEEP_TIME     0x0001

//uint16_t sleep_time_s = 60;   // Default 1 Minute
//sleep_time_s = load_sleep_time_from_nvs(60);  // fallback 60s

// Globale Variable für aktuelle Helligkeit (0-254)
static uint8_t current_level = 254;
static uint8_t stored_level = 254;

static bool zigbee_connected = false;
static int64_t last_activity_time = 0;
static bool sleep_enabled = true;

static const char *TAG = "SLEEPY_ZIGBEE_XMAS_LIGHTSTRIP";

static void led_set_power(bool power);
static void led_set_brightness(uint8_t level);
void enter_light_sleep(void);
void send_periodic_reports(void);
static void update_activity_time(void);

/********************* LED functions **************************/

static void configure_pwm(void) {
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_RESOLUTION, // 8-bit resolution (RTC clock in sleep)
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_USE_RC_FAST_CLK // RTC clock can provide PWM while in Light-Sleep
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LIGHT_PWM_GPIO,
        .duty = 0,  // start with LED off
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

/*
 * Set brightness of LED strip (Zigbee level 0-254 into PWM duty 0-1023)
 */
static void led_set_brightness(uint8_t level)
{
    current_level = level;
    // Convert Zigbee Level (0-254) into PWM Duty-Cycle (0-1023)
    uint16_t duty = (uint16_t)((level * 1023) / 254);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "Brightness set to %d (PWM: %d)", level, duty);
}

/*
 * Set brightness of LED strip
 */
static void led_set_power(bool power)
{
    if (power == false) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ESP_LOGI(TAG, "Light turned OFF (stored level: %d)", stored_level);
    } else {
        led_set_brightness(stored_level);
    }
}

static void identify_timer_timeout_callback(TimerHandle_t timer)
{
    xTimerStop(identify_timer, 0);
    xTimerStop(identify_timeout_timer, 0);
    led_set_power(false);
}

static void identify_timer_callback(TimerHandle_t timer)
{
    static bool led_state = false;
    led_set_power(led_state);
    led_state = !led_state;
}

static void start_identify_blink(uint8_t effect_id, uint16_t identify_time)
{
    if (effect_id == ESP_ZB_ZCL_IDENTIFY_EFFECT_ID_BLINK) {
        identify_timeout_timer = xTimerCreate("identityTimeout", pdMS_TO_TICKS(identify_time * 1000), pdTRUE, NULL, identify_timer_timeout_callback);
        if (identify_timer == NULL) {
            identify_timer = xTimerCreate("identify", pdMS_TO_TICKS(500), pdTRUE, NULL, identify_timer_callback);
        }
        stored_level = current_level;
        current_level = 254;
        xTimerStart(identify_timeout_timer, 0);
        xTimerStart(identify_timer, 0);
    } else if (effect_id == ESP_ZB_ZCL_IDENTIFY_EFFECT_ID_STOP) {
        xTimerStop(identify_timer, 0);
        led_set_power(false);
    }
}

static esp_err_t deferred_driver_init(void)
{
    configure_pwm();
    return ESP_OK;
}

/******************************* ZIGBEE Functions **********************************/

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) {

    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:

    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted");
            }
        } else {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            zigbee_connected = true;
            update_activity_time();
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
        break;
    }
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    bool light_state = 0;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);

    ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);

    if (message->info.dst_endpoint == ESP_ZED_ENDPOINT) {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                light_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : light_state;
                ESP_LOGI(TAG, "Light sets to %s", light_state ? "On" : "Off");
                if (light_state == false) {
                    stored_level = current_level;
                }
                led_set_power(light_state);
            }
        }
        else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID) {
                uint16_t identify_time = *(uint16_t *)message->attribute.data.value;
                ESP_LOGI(TAG, "Identify time set to: %d seconds", identify_time);
                if (identify_time > 0) {
                    start_identify_blink(ESP_ZB_ZCL_IDENTIFY_EFFECT_ID_BLINK, identify_time);
                } else {
                    start_identify_blink(ESP_ZB_ZCL_IDENTIFY_EFFECT_ID_STOP, 0);
                }
            }
        }
        else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID) {
                uint8_t level = *(uint8_t*)message->attribute.data.value;
                if (level > 254) {
                    ESP_LOGW(TAG, "Invalid level: %d, capping to 254", level);
                    level = 254;
                }
                led_set_brightness(level);
                ESP_LOGI(TAG, "Level changed to: %d", level);
            }
        }
    }
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;

    switch (callback_id) {
        case ESP_ZB_CORE_IDENTIFY_EFFECT_CB_ID:
            update_activity_time();
            esp_zb_zcl_identify_effect_message_t *identify_msg = (esp_zb_zcl_identify_effect_message_t *)message;
            ESP_LOGI(TAG, "Identify effect: %d, duration: %d", identify_msg->effect_id, identify_msg->effect_variant);
            start_identify_blink(identify_msg->effect_id, identify_msg->effect_variant);
            break;

        case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
            update_activity_time();
            ESP_LOGI(TAG, "Received attribute update");
            ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
            break;
            
        case ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID:
            update_activity_time();
            break;
            
        case ESP_ZB_CORE_REPORT_ATTR_CB_ID:
            update_activity_time();
            break;
/*            
        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
            ESP_LOGI(TAG, "Device started");
            break;
            
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            ESP_LOGI(TAG, "Device rebooted");
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device already commissioned");
                zigbee_connected = true;
                update_activity_time();
            }
            break;
*/            
            
        default:
            ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
            break;
    }

    return ret;
}

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    // Create endpoint list
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    
    // Define endpoint configuration
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ESP_ZED_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };
        
    // create cluster list
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    //------------------------ Create Basic Cluster ----------------------------
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_BATTERY
    };
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&basic_cfg);
    // Add attributes to Basic cluster
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, "\x09Espressif"); // Pascal-String, max 32 bytes
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,  "\x08ESP32H2"); // Pascal-String, max 32 bytes
    // Add to cluster list
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    //------------------------ Create On/Off Cluster ----------------------------
    esp_zb_on_off_cluster_cfg_t on_off_cfg = {
        .on_off = false  // Initial state: off
    };
    esp_zb_attribute_list_t *on_off_cluster = esp_zb_on_off_cluster_create(&on_off_cfg);
    // Add attributes to Basic cluster
    // Add to cluster list
    esp_zb_cluster_list_add_on_off_cluster(cluster_list, on_off_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    //------------------------ Create Level Cluster ----------------------------
    uint8_t level_value = 254;
    uint16_t remaining_time = 0;
    esp_zb_level_cluster_cfg_t level_cfg = {
        .current_level = level_value // Initial state
    };
    esp_zb_attribute_list_t *level_cluster = esp_zb_level_cluster_create(&level_cfg);
    esp_zb_level_cluster_add_attr(level_cluster, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, &level_value);
    esp_zb_level_cluster_add_attr(level_cluster, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_REMAINING_TIME_ID, &remaining_time);
    // Add attributes to Level cluster
    // Add to cluster list
    esp_zb_cluster_list_add_level_cluster(cluster_list, level_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    //------------------------ Create Identify Cluster (Server-Role) ----------------------------
    esp_zb_identify_cluster_cfg_t identify_cfg = {
        .identify_time = 10
    };
    esp_zb_attribute_list_t *identitfy_cluster = esp_zb_identify_cluster_create(&identify_cfg);
    // Add attributes to identify cluster
    // Add to cluster list
    esp_zb_cluster_list_add_identify_cluster(cluster_list, identitfy_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    //------------------------ Create Groups Cluster ----------------------------
    esp_zb_groups_cluster_cfg_t group_cfg = {
            .groups_name_support_id =   true
    };
    esp_zb_attribute_list_t *groups_cluster = esp_zb_groups_cluster_create(&group_cfg);
    // Add attributes to groups cluster
    // Add to cluster list
    esp_zb_cluster_list_add_groups_cluster(cluster_list, groups_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    //------------------------ Create Scene Cluster ----------------------------
    esp_zb_scenes_cluster_cfg_t scene_cfg = {
        .current_group = 0,
        .current_scene = 0,
        .name_support = true,
        .scene_valid = true,
        .scenes_count = 0
    };
    esp_zb_attribute_list_t *scenes_cluster = esp_zb_scenes_cluster_create(&scene_cfg);
    // Add attributes to scene cluster
    // Add to cluster list
    esp_zb_cluster_list_add_scenes_cluster(cluster_list, scenes_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // Add cluster list to endpoint
    esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);

    // Add cluster list to endpoint
    esp_zb_device_register(ep_list);

    //esp_zb_device_add_set_attr_value_cb(zb_attribute_handler); // act on attribute changes only
    esp_zb_core_action_handler_register(zb_action_handler); // act on everything
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));

    // (re)start activity timer
    update_activity_time();

    esp_zb_stack_main_loop(); // inifinite loop
}

// Update Activity Timer
static void update_activity_time(void) {
    last_activity_time = esp_timer_get_time() / 1000000; // in Sekunden
    ESP_LOGI(TAG, "Activity updated");
}

// Periodische Reports senden (Batterie, etc.)
void send_periodic_reports(void) {
    if (!zigbee_connected) {
        return;
    }
    
    ESP_LOGI(TAG, "Sending periodic reports...");
    
    // Hier kannst du Batteriespannung, Sensordaten etc. senden
    // Beispiel: Battery Voltage Report
    // esp_zb_zcl_report_attr_cmd_t report_cmd = { ... };
    // esp_zb_zcl_report_attr_cmd_req(&report_cmd);
    
    update_activity_time();
}

/************************** Power/Sleep Management Task ******************************/

// Sleep-Management Task
static void sleep_management_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sleep management task started");
    
    while (1) {
        // Warte kurz
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Prüfe ob Zigbee verbunden ist
        if (!zigbee_connected) {
            continue;
        }
        
        // Prüfe ob Sleep aktiviert ist
        if (!sleep_enabled) {
            continue;
        }
        
        // Berechne Inaktivitätszeit
        int64_t current_time = esp_timer_get_time() / 1000000;
        int64_t inactive_time = current_time - last_activity_time;
        
        // Wenn Inaktivitätszeit erreicht, gehe in Sleep
        if (inactive_time >= INACTIVITY_TIMEOUT_S) {
            ESP_LOGI(TAG, "Inactivity timeout reached (%lld s), entering sleep", inactive_time);
            
            // Sende vor dem Sleep noch Reports
            send_periodic_reports();
            
            // Gehe in Light Sleep
            enter_light_sleep();
            
            // Nach Aufwachen: Activity Timer zurücksetzen
            ESP_LOGI(TAG, "Woke up from sleep!");
            update_activity_time();
            
            // Sende Reports nach Aufwachen
            send_periodic_reports();
        }
    }
}

// Light Sleep Funktion
void enter_light_sleep(void) {
    ESP_LOGI(TAG, "Entering light sleep for %d seconds...", SLEEP_DURATION_S);
    
    // Konfiguriere Wake-up Quellen
    
    // 1. Timer Wake-up (in Mikrosekunden)
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_S * 1000000ULL);
    
    // 2. GPIO Wake-up (BOOT Button auf GPIO9)
    // Konfiguriere GPIO als Input mit Pull-up
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&gpio_conf);
    
    // GPIO9 Wake-up bei LOW (Button gedrückt)
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable(BOOT_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
     
    // Informiere Zigbee Stack über Sleep (wichtig für ZED!)
    // Der Stack muss dem Parent mitteilen, dass wir schlafen
    esp_zb_sleep_enable(true);
    
    // Gehe in Light Sleep
    esp_light_sleep_start();
    
    // Deaktiviere GPIO wakeup nach dem Aufwachen
    gpio_wakeup_disable(BOOT_BUTTON_GPIO);

    // Prüfe Wake-up Grund
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wakeup caused by timer");
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
            ESP_LOGI(TAG, "Wakeup caused by BOOT button (GPIO%d)", BOOT_BUTTON_GPIO);
            break;
        default:
            ESP_LOGI(TAG, "Wakeup caused by: %d", wakeup_reason);
            break;
    }
    
    // Reaktiviere Zigbee nach Sleep
    esp_zb_sleep_enable(false);
}

/************************** MAIN **********************/
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Zigbee Sleepy End Device");
    
    // Zigbee init
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    // Starte Zigbee Task
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
    
    // Starte Sleep Management Task
    xTaskCreate(sleep_management_task, "Sleep_mgmt", 3072, NULL, 4, NULL);
}
