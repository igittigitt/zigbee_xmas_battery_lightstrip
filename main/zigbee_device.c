/*
 * Zigbee stack setup and commissioning (esp-zigbee-lib 2.0.5).
 *
 * Structure follows the esp-zigbee-sdk 2.0.5 examples
 * examples/sleepy_devices/light_sleep_end_device and
 * examples/home_automation_devices/on_off_light. Details: docs/zigbee.md.
 *
 * Threading: everything in this file runs in the Zigbee task (signal handler,
 * action callbacks) except the esp_timer callback and the public functions,
 * which acquire the Zigbee lock (docs/en/developing.rst, "not thread-safe").
 */
#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_zigbee.h"
#include "zigbee_config.h"
#include "zigbee_device.h"
#include "cluster_manager.h"
#include "on_off_cluster.h"
#include "level_cluster.h"
#include "attribute_handler.h"
#include "status_indicator.h"
#include "app_config.h"

static const char *TAG = "ZIGBEE_DEVICE";

/* Delayed commissioning (retry / rejoin). Only one request is pending at a time. */
static esp_timer_handle_t s_retry_timer;
static ezb_bdb_comm_mode_mask_t s_retry_mode;

/* End of the current pairing window (esp_timer time in us), 0 = no window. */
static int64_t s_pairing_deadline_us;

/* Rejoin retries: number of attempts so far and current backoff delay. */
static uint32_t s_rejoin_attempts;
static uint32_t s_rejoin_delay_ms;

/*
 * DEBUG diagnostic scan (APP_DEBUG): while it runs, the next commissioning
 * attempt is held back and scheduled from the end of the scan.
 */
static bool s_diag_scan_pending;
static uint32_t s_diag_delay_ms;
static unsigned s_diag_beacons;

/* ---------------------------------------------------------------------------
 * Diagnostics: neighbor table and beacons (Zigbee task context)
 * ------------------------------------------------------------------------- */

static const char *relationship_str(uint8_t relationship)
{
    switch (relationship) {
    case EZB_NWK_RELATIONSHIP_PARENT:  return "parent";
    case EZB_NWK_RELATIONSHIP_CHILD:   return "child";
    case EZB_NWK_RELATIONSHIP_SIBLING: return "sibling";
    default:                           return "none";
    }
}

static const char *device_type_str(uint8_t device_type)
{
    switch (device_type) {
    case EZB_NWK_DEVICE_TYPE_COORDINATOR: return "coordinator";
    case EZB_NWK_DEVICE_TYPE_ROUTER:      return "router";
    case EZB_NWK_DEVICE_TYPE_END_DEVICE:  return "end device";
    default:                              return "unknown";
    }
}

/*
 * Log the neighbor table (ezbee/nwk.h). Production: parent only. DEBUG: all
 * entries, i.e. the candidates the stack knows for a (re)join. Whether the
 * stack keeps scan results in this table after a failed attempt is not
 * documented [UNBELEGT]; the log shows it. RSSI is omitted: the field read
 * 127 in tests (known-issues 1c), LQI is meaningful.
 */
static void log_neighbors(const char *when)
{
    ezb_nwk_info_iterator_t it = EZB_NWK_INFO_ITERATOR_INIT;
    ezb_nwk_neighbor_info_t nbr;
    unsigned count = 0;

    while (ezb_nwk_get_next_neighbor(&it, &nbr) == EZB_ERR_NONE) {
        count++;
        if (!APP_DEBUG && nbr.relationship != EZB_NWK_RELATIONSHIP_PARENT) {
            continue;
        }
        ESP_LOGI(TAG, "%s: neighbor 0x%04hx (IEEE 0x%016llx) %s, %s, depth %u, LQI %u, outgoing cost %u", when,
                 nbr.short_addr, (unsigned long long)nbr.ieee_addr.u64, device_type_str(nbr.device_type),
                 relationship_str(nbr.relationship), nbr.depth, nbr.lqi, nbr.outgoing_cost);
    }
    if (APP_DEBUG) {
        ESP_LOGI(TAG, "%s: %u neighbor(s)", when, count);
    }
}

static void start_commissioning(ezb_bdb_comm_mode_mask_t mode)
{
    ezb_err_t err = ezb_bdb_start_top_level_commissioning(mode);
    if (err != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Start commissioning (mode 0x%02x) failed: 0x%04x", mode, err);
    }
}

/* esp_timer task context: hand the request over to the Zigbee stack. */
static void retry_timer_cb(void *arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    start_commissioning(s_retry_mode);
    esp_zigbee_lock_release();
}

static void start_retry_timer(uint32_t delay_ms)
{
    esp_timer_stop(s_retry_timer);  // ESP_ERR_INVALID_STATE if not running: ignored
    esp_err_t err = esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scheduling commissioning failed: %s", esp_err_to_name(err));
    }
}

/*
 * Active scan callback (ezb_nwk_active_scan_callback_t). Called per beacon and
 * with NULL at the end of the scan. Assumption (not documented for
 * ezb_nwk_scan): it runs in the Zigbee task like the other stack callbacks.
 * It only logs and starts an esp_timer, which is safe from any task.
 */
static void diag_scan_cb(ezb_nwk_active_scan_result_t *result, void *user_ctx)
{
    if (result != NULL) {
        ezb_extpanid_t own;
        ezb_nwk_get_extended_panid(&own);   // zero while factory new
        bool own_network = own.u64 != 0 && own.u64 == result->extpanid.u64;
        s_diag_beacons++;
        ESP_LOGI(TAG, "Beacon from 0x%04hx: PAN 0x%04hx (EXT 0x%016llx%s), channel %u, permit join %d, "
                      "end device capacity %d, router capacity %d, update ID %u",
                 result->shortaddr, result->panid, (unsigned long long)result->extpanid.u64,
                 own_network ? ", own network" : "", result->channel_number, result->permit_join,
                 result->enddev_capacity, result->router_capacity, result->nwk_update_id);
        return;
    }
    ESP_LOGI(TAG, "Diagnostic scan done: %u beacon(s)", s_diag_beacons);
    log_neighbors("After scan");
    if (s_diag_scan_pending) {     // cleared if a join or new pairing came first
        s_diag_scan_pending = false;
        start_retry_timer(s_diag_delay_ms);
    }
}

/* DEBUG: start the diagnostic scan; the retry follows from its end. false = not started. */
static bool diag_scan_start(uint32_t delay_ms)
{
    const ezb_nwk_scan_req_t req = {
        .scan_type = EZB_NWK_SCAN_TYPE_ACTIVE,
        .scan_duration = ZB_DIAG_SCAN_DURATION,
        .scan_channels = ZB_PRIMARY_CHANNEL_MASK,
        .active_scan_cb = diag_scan_cb,
        .user_ctx = NULL,
    };

    log_neighbors("After failed attempt");
    s_diag_beacons = 0;
    ezb_err_t err = ezb_nwk_scan(&req);
    if (err != EZB_ERR_NONE) {
        ESP_LOGW(TAG, "Diagnostic scan not started: 0x%04x", err);
        return false;
    }
    s_diag_delay_ms = delay_ms;
    s_diag_scan_pending = true;
    return true;
}

static void schedule_commissioning(ezb_bdb_comm_mode_mask_t mode, uint32_t delay_ms)
{
    esp_timer_stop(s_retry_timer);
    s_diag_scan_pending = false;
    s_retry_mode = mode;
    if (APP_DEBUG && diag_scan_start(delay_ms)) {
        return;
    }
    start_retry_timer(delay_ms);
}

static void cancel_scheduled_commissioning(void)
{
    s_diag_scan_pending = false;
    esp_timer_stop(s_retry_timer);
}

static void pairing_start(void)
{
    // A retry may still be scheduled (e.g. steering failed, then the stack left
    // the network): starting twice fails with EZB_ERR_BUSY.
    cancel_scheduled_commissioning();
    s_pairing_deadline_us = esp_timer_get_time() + (int64_t)ZB_PAIRING_TIMEOUT_MS * 1000;
    ESP_LOGI(TAG, "Pairing window open for %d s (network steering)", ZB_PAIRING_TIMEOUT_MS / 1000);
    status_indicator_pairing();
    start_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
}

static void pairing_retry_or_stop(void)
{
    if (s_pairing_deadline_us != 0 && esp_timer_get_time() < s_pairing_deadline_us) {
        schedule_commissioning(EZB_BDB_MODE_NETWORK_STEERING, ZB_STEERING_RETRY_MS);
        return;
    }
    s_pairing_deadline_us = 0;
    ESP_LOGW(TAG, "Pairing window closed without joining a network; "
                  "waiting for zigbee_device_start_pairing()");
    status_indicator_stop(false);   // goal Z10: light off after the pairing timeout
}

static void rejoin_reset(void)
{
    s_rejoin_attempts = 0;
    s_rejoin_delay_ms = 2 * ZB_REJOIN_RETRY_MIN_MS;
}

/* Fast phase (1 s, like the Espressif example), then exponential backoff. */
static void rejoin_schedule(void)
{
    uint32_t delay_ms = ZB_REJOIN_RETRY_MIN_MS;

    if (s_rejoin_attempts >= ZB_REJOIN_FAST_ATTEMPTS) {
        delay_ms = s_rejoin_delay_ms;
        s_rejoin_delay_ms = (s_rejoin_delay_ms >= ZB_REJOIN_RETRY_MAX_MS / 2) ? ZB_REJOIN_RETRY_MAX_MS
                                                                               : s_rejoin_delay_ms * 2;
    }
    s_rejoin_attempts++;
    ESP_LOGI(TAG, "Rejoin attempt %" PRIu32 " in %" PRIu32 " ms", s_rejoin_attempts + 1, delay_ms);
    schedule_commissioning(EZB_BDB_MODE_INITIALIZATION, delay_ms);
}

/* Log the parent (DEBUG: whole neighbor table) and the TX power after joining. */
static void log_parent(void)
{
    log_neighbors("Joined");
    if (APP_DEBUG) {
        // Diagnostics for docs/known-issues.md 1c: the IDF 802.15.4 driver
        // defaults to the maximum (ESP32-H2: 20 dBm); does the stack change it?
        int8_t tx_power = 0;
        ezb_get_tx_power(&tx_power);
        ESP_LOGI(TAG, "TX power %d dBm", tx_power);
    }
}

static void on_network_joined(const char *how)
{
    ezb_extpanid_t ext_pan_id;
    ezb_nwk_get_extended_panid(&ext_pan_id);
    ESP_LOGI(TAG, "%s: PAN ID 0x%04hx (EXT 0x%016llx), channel %d, short address 0x%04hx", how,
             ezb_nwk_get_panid(), (unsigned long long)ext_pan_id.u64, ezb_nwk_get_current_channel(),
             ezb_nwk_get_short_address());
    s_pairing_deadline_us = 0;
    rejoin_reset();
    cancel_scheduled_commissioning();
    log_parent();
    on_off_cluster_sync_attribute();
    level_cluster_sync_attribute();
    // Confirmation pattern, then the StartUpOnOff state (goals Z10/Z12); no
    // effect after a silent rejoin during operation.
    status_indicator_connected(on_off_cluster_get_state());
}

/* ---------------------------------------------------------------------------
 * Signal handler (Zigbee task context, must not block)
 * ------------------------------------------------------------------------- */

static ezb_bdb_comm_status_t signal_bdb_status(const ezb_app_signal_t *app_signal)
{
    const ezb_bdb_comm_status_t *status = ezb_app_signal_get_params(app_signal);
    return status ? *status : EZB_BDB_STATUS_SUCCESS;
}

static bool zigbee_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t type = ezb_app_signal_get_type(app_signal);

    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        // esp_zigbee_start(false): the application starts the BDB initialization
        // explicitly, also to rejoin a stored network (migration guide v2.x,
        // commissioning.rst).
        // Persistent data is loaded at this point (app_signals.h): determine the
        // StartUpOnOff state (goal Z6); the light shows it after connecting.
        // Until then: search indication (goal Z12); replaced by the pairing
        // indication if the device turns out to be factory new.
        on_off_cluster_load_startup();
        level_cluster_load_startup();   // brightness for "on" (StartUpCurrentLevel)
        status_indicator_searching();
        ESP_LOGI(TAG, "Stack started, BDB initialization");
        start_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = signal_bdb_status(app_signal);
        if (status != EZB_BDB_STATUS_SUCCESS) {
            // Expected several times after a reboot (stack behaviour, known-issues 1c)
            ESP_LOGW(TAG, "%s failed (status 0x%02x)", ezb_app_signal_to_string(type), status);
            status_indicator_check_timeout();
            rejoin_schedule();
        } else if (ezb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "Factory new, no stored network");
            pairing_start();
        } else {
            on_network_joined("Rejoined stored network");
        }
        break;
    }

    case EZB_BDB_SIGNAL_STEERING:
        if (signal_bdb_status(app_signal) == EZB_BDB_STATUS_SUCCESS) {
            on_network_joined("Joined network");
        } else {
            ESP_LOGI(TAG, "Network steering failed (status 0x%02x)", signal_bdb_status(app_signal));
            pairing_retry_or_stop();
        }
        break;

    case EZB_ZDO_SIGNAL_PARENT_LOST:
        // Raised after keepalive failed for ZB_ED_TIMEOUT (app_signals.h). Rejoin via
        // BDB initialization, which tries secure and TC rejoin on all channels since
        // 2.0.5 (RELEASE_NOTES.md). Whether this is the intended reaction is not
        // documented by Espressif.
        ESP_LOGW(TAG, "Parent lost");
        rejoin_reset();
        rejoin_schedule();
        break;

    case EZB_ZDO_SIGNAL_LEAVE: {
        const ezb_zdo_signal_leave_params_t *leave = ezb_app_signal_get_params(app_signal);
        if (leave && leave->leave_type == EZB_ZDO_LEAVE_TYPE_RESET) {
            // Removed from the network (e.g. "Remove" in Zigbee2MQTT): offer re-pairing.
            ESP_LOGW(TAG, "Left network (reset)");
            pairing_start();
        } else {
            ESP_LOGI(TAG, "Left network (rejoin)");
        }
        break;
    }

    case EZB_NWK_SIGNAL_NETWORK_STATUS: {
        const ezb_nwk_signal_network_status_params_t *ns = ezb_app_signal_get_params(app_signal);
        if (ns) {
            ESP_LOGW(TAG, "Network status 0x%02x (address 0x%04hx)", ns->status, ns->network_addr);
        }
        break;
    }

    default:
        ESP_LOGI(TAG, "Signal %s (0x%02x)", ezb_app_signal_to_string(type), (unsigned)type);
        break;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Stack setup
 * ------------------------------------------------------------------------- */

static esp_err_t zigbee_setup(void)
{
    const esp_zigbee_config_t config = {
        .device_config = {
            .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,
            .install_code_policy = ZB_INSTALLCODE_POLICY,
            .zed_config = {
                .ed_timeout = ZB_ED_TIMEOUT,
                .keep_alive = ZB_KEEP_ALIVE_MS,
            },
        },
        .platform_config = {
            .storage_partition_name = ZB_STORAGE_PARTITION_NAME,
            .radio_config = {
                .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,
            },
        },
    };
    ESP_RETURN_ON_ERROR(esp_zigbee_init(&config), TAG, "esp_zigbee_init");

    // Commissioning parameters (as in the 2.0.5 examples)
    ezb_aps_secur_enable_distributed_security(false);
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_bdb_set_primary_channel_set(ZB_PRIMARY_CHANNEL_MASK)),
                        TAG, "primary channel set");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_bdb_set_secondary_channel_set(ZB_SECONDARY_CHANNEL_MASK)),
                        TAG, "secondary channel set");
    ESP_RETURN_ON_ERROR(esp_zigbee_err_to_esp(ezb_app_signal_add_handler(zigbee_signal_handler)),
                        TAG, "signal handler");
    ezb_nwk_set_rx_on_when_idle(ZB_RX_ON_WHEN_IDLE);

    // Data model: endpoint with clusters, then ZCL action callbacks
    ESP_RETURN_ON_ERROR(cluster_manager_register_device(), TAG, "device registration");
    ezb_zcl_core_action_handler_register(attribute_handler_callback);

    return ESP_OK;
}

void zigbee_device_task(void *arg)
{
    const esp_timer_create_args_t timer_args = {
        .callback = retry_timer_cb,
        .name = "zb_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_retry_timer));
    rejoin_reset();

    ESP_ERROR_CHECK(zigbee_setup());
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    ESP_LOGI(TAG, "Zigbee stack running (keepalive %d ms)", ZB_KEEP_ALIVE_MS);

    esp_zigbee_launch_mainloop();   // returns only after esp_zigbee_stop()

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------------------
 * Public API (any task)
 * ------------------------------------------------------------------------- */

esp_err_t zigbee_device_start_pairing(void)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    esp_zigbee_lock_acquire(portMAX_DELAY);
    if (esp_zigbee_is_started() && !ezb_bdb_dev_joined()) {
        cancel_scheduled_commissioning();
        pairing_start();
        ret = ESP_OK;
    }
    esp_zigbee_lock_release();
    return ret;
}

void zigbee_device_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset");
    esp_zigbee_lock_acquire(portMAX_DELAY);
    esp_zigbee_factory_reset();     // erases the Zigbee datasets and restarts
}

bool zigbee_device_is_joined(void)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    bool joined = ezb_bdb_dev_joined();
    esp_zigbee_lock_release();
    return joined;
}
