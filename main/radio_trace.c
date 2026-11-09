/*
 * DEBUG radio trace, see radio_trace.h.
 *
 * Frame formats and their sources:
 * - frame[0] is the PHR length including the 2 FCS octets; on RX the FCS
 *   octets are replaced by RSSI/LQI (esp_ieee802154.h; OpenThread port
 *   esp_openthread_radio.c in ESP-IDF 5.5.4 uses frame[0] as mLength).
 * - MAC frame control bits (type 0-2, security 3, PAN ID compression 6,
 *   dst mode 10-11, version 12-13, src mode 14-15): ESP-IDF
 *   ieee802154/private_include/esp_ieee802154_frame.h and OpenThread
 *   mac_frame.hpp (both in ESP-IDF 5.5.4).
 * - MAC command IDs (association request 0x01, association response 0x02,
 *   beacon request 0x07): OpenThread mac_frame.hpp, Wireshark
 *   packet-ieee802154.h.
 * - Beacon superframe spec (PAN coordinator bit 14, association permit
 *   bit 15), GTS spec (descriptor count bits 0-2, then directions octet and
 *   3 octets per descriptor), pending address spec (short count bits 0-2,
 *   extended count bits 4-6), association response payload (short address,
 *   status; 0 success, 1 PAN at capacity, 2 PAN access denied):
 *   Wireshark packet-ieee802154.h/.c only. Single source - the decoded
 *   beacon fields (permit join, capacities, EXT PAN) can be cross-checked
 *   with the stack's own scan log (ZL_DEBUG_DIAG_SCAN).
 * - Zigbee beacon info (15 octets): Zigbee spec R23 (05-3474-23) Figure 3-53.
 * - NWK header: Figure 3-4/3-5; NWK command IDs: Table 3-50 (reference
 *   column: 0x01 = 3.4.1 Route Request ... 0x0f = 3.4.15).
 */
#include "radio_trace.h"
#include "sdkconfig.h"

#if CONFIG_ZL_DEBUG_RADIO_TRACE

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_ieee802154.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "radio_trace";

#define RT_QUEUE_LEN            24
#define RT_TASK_STACK_SIZE      3072
#define RT_TASK_PRIORITY        2
/** Quiet time after the last beacon request before the scan summary is logged. */
#define RT_SCAN_FLUSH_MS        500
#define RT_MAX_PSDU             127
#define RT_FCS_LEN              2

/* MAC frame control */
#define MAC_FCF_TYPE_MASK       0x0007
#define MAC_FCF_SECURITY        0x0008
#define MAC_FCF_PANID_COMP      0x0040
#define MAC_FCF_DST_MODE_SHIFT  10
#define MAC_FCF_VERSION_SHIFT   12
#define MAC_FCF_SRC_MODE_SHIFT  14
#define MAC_ADDR_NONE           0
#define MAC_ADDR_SHORT          2
#define MAC_ADDR_EXT            3
#define MAC_TYPE_BEACON         0
#define MAC_TYPE_DATA           1
#define MAC_TYPE_CMD            3
#define MAC_CMD_ASSOC_REQ       0x01
#define MAC_CMD_ASSOC_RSP       0x02
#define MAC_CMD_BEACON_REQ      0x07
#define MAC_SHORT_BROADCAST     0xFFFF

/* Beacon (superframe, GTS, pending address specification) */
#define MAC_SF_PAN_COORD        0x4000
#define MAC_SF_ASSOC_PERMIT     0x8000
#define MAC_GTS_COUNT_MASK      0x07
#define MAC_GTS_DESC_LEN        3
#define MAC_PEND_SHORT_MASK     0x07
#define MAC_PEND_EXT_SHIFT      4
#define MAC_PEND_EXT_MASK       0x07

/* Association request capability information */
#define MAC_CAP_FFD             0x02
#define MAC_CAP_RX_ON_IDLE      0x08
#define MAC_CAP_ALLOC_ADDR      0x80

/* Zigbee beacon info (Figure 3-53) */
#define ZB_BEACON_INFO_LEN      15
#define ZB_PROTOCOL_ID          0x00

/* NWK frame control (Figure 3-5) */
#define NWK_FCF_TYPE_MASK       0x0003
#define NWK_TYPE_CMD            1
#define NWK_FCF_MULTICAST       0x0100
#define NWK_FCF_SECURITY        0x0200
#define NWK_FCF_SRC_ROUTE       0x0400
#define NWK_FCF_DST_IEEE        0x0800
#define NWK_FCF_SRC_IEEE        0x1000
#define NWK_HDR_MIN_LEN         8
#define NWK_MULTICAST_CTRL_LEN  1
#define NWK_IEEE_LEN            8

typedef enum {
    RT_RX = 0,
    RT_TX_DONE,
    RT_TX_FAILED,
} rt_dir_t;

typedef struct {
    uint8_t dir;            // rt_dir_t
    uint8_t channel;
    bool has_link;          // rssi/lqi valid: received frame or ACK of a sent frame
    int8_t rssi;
    uint8_t lqi;
    uint8_t tx_err;         // esp_ieee802154_tx_error_t, RT_TX_FAILED only
    uint8_t len;            // MPDU length without FCS
    uint8_t psdu[RT_MAX_PSDU];
} rt_event_t;

typedef struct {
    uint8_t type;
    uint8_t dst_mode;
    uint8_t src_mode;
    bool security;
    uint16_t dst_pan;
    uint16_t src_pan;
    uint16_t dst_short;
    uint16_t src_short;
    uint64_t dst_ext;
    uint64_t src_ext;
    uint8_t hdr_len;        // offset of the MAC payload
} mac_hdr_t;

typedef struct {
    unsigned requests;
    unsigned failed;
    unsigned beacons;
    uint32_t channels;      // bit n = channel n
} rt_scan_t;

static QueueHandle_t s_queue;
/* All driver callbacks come from the single 802.15.4 interrupt, which does not
 * nest with itself, so one static buffer suffices for the ISR path. */
static rt_event_t s_isr_event;
static volatile uint32_t s_dropped;
static rt_scan_t s_scan;    // radio_trace task only

/* ---------------------------------------------------------------------------
 * Frame parsing (ISR and task)
 * ------------------------------------------------------------------------- */

static IRAM_ATTR uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static IRAM_ATTR uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

static IRAM_ATTR bool read_addr(const uint8_t *p, uint8_t len, uint8_t *off, uint8_t mode,
                      uint16_t *short_addr, uint64_t *ext_addr)
{
    if (mode == MAC_ADDR_SHORT) {
        if (*off + 2 > len) {
            return false;
        }
        *short_addr = rd16(p + *off);
        *off += 2;
    } else if (mode == MAC_ADDR_EXT) {
        if (*off + 8 > len) {
            return false;
        }
        *ext_addr = rd64(p + *off);
        *off += 8;
    }
    return true;
}

/* MAC header of a 2003/2006 frame without MAC security. false = not decodable. */
static IRAM_ATTR bool mac_parse(const uint8_t *p, uint8_t len, mac_hdr_t *h)
{
    if (len < 3) {
        return false;
    }
    uint16_t fcf = rd16(p);
    h->type = fcf & MAC_FCF_TYPE_MASK;
    h->security = (fcf & MAC_FCF_SECURITY) != 0;
    h->dst_mode = (fcf >> MAC_FCF_DST_MODE_SHIFT) & 0x3;
    h->src_mode = (fcf >> MAC_FCF_SRC_MODE_SHIFT) & 0x3;
    uint8_t version = (fcf >> MAC_FCF_VERSION_SHIFT) & 0x3;
    if (version > 1 || h->dst_mode == 1 || h->src_mode == 1 || h->security) {
        return false;   // 2015 frames (IEs), reserved modes, MAC security: not used by Zigbee joins
    }
    h->dst_pan = h->src_pan = MAC_SHORT_BROADCAST;
    h->dst_short = h->src_short = MAC_SHORT_BROADCAST;
    h->dst_ext = h->src_ext = 0;

    uint8_t off = 3;    // frame control + sequence number
    if (h->dst_mode != MAC_ADDR_NONE) {
        if (off + 2 > len) {
            return false;
        }
        h->dst_pan = rd16(p + off);
        off += 2;
        if (!read_addr(p, len, &off, h->dst_mode, &h->dst_short, &h->dst_ext)) {
            return false;
        }
    }
    if (h->src_mode != MAC_ADDR_NONE) {
        if ((fcf & MAC_FCF_PANID_COMP) && h->dst_mode != MAC_ADDR_NONE) {
            h->src_pan = h->dst_pan;
        } else {
            if (off + 2 > len) {
                return false;
            }
            h->src_pan = rd16(p + off);
            off += 2;
        }
        if (!read_addr(p, len, &off, h->src_mode, &h->src_short, &h->src_ext)) {
            return false;
        }
    }
    h->hdr_len = off;
    return off <= len;
}

/* ISR filter: beacons, beacon/association requests, association responses,
 * unicast NWK commands. Data requests (every keepalive) are not traced.
 * ISR path in IRAM like the driver and the Zigbee library callbacks; if-chain
 * instead of switch, so no jump table in flash rodata. */
static IRAM_ATTR bool rt_is_interesting(rt_dir_t dir, const uint8_t *p, uint8_t len)
{
    mac_hdr_t h;
    if (!mac_parse(p, len, &h)) {
        return false;
    }
    if (h.type == MAC_TYPE_BEACON) {
        return dir == RT_RX;
    }
    if (h.type == MAC_TYPE_CMD) {
        if (h.hdr_len >= len) {
            return false;
        }
        if (dir == RT_RX) {
            return p[h.hdr_len] == MAC_CMD_ASSOC_RSP;
        }
        return p[h.hdr_len] == MAC_CMD_ASSOC_REQ || p[h.hdr_len] == MAC_CMD_BEACON_REQ;
    }
    if (h.type == MAC_TYPE_DATA) {
        if (h.dst_mode == MAC_ADDR_NONE ||
            (h.dst_mode == MAC_ADDR_SHORT && h.dst_short == MAC_SHORT_BROADCAST) ||
            h.hdr_len + NWK_HDR_MIN_LEN > len) {
            return false;
        }
        return (rd16(p + h.hdr_len) & NWK_FCF_TYPE_MASK) == NWK_TYPE_CMD;
    }
    return false;
}

static IRAM_ATTR void rt_capture(rt_dir_t dir, const uint8_t *frame, uint8_t channel,
                       const esp_ieee802154_frame_info_t *link, uint8_t tx_err)
{
    if (s_queue == NULL || frame == NULL || frame[0] <= RT_FCS_LEN || frame[0] > RT_MAX_PSDU) {
        return;
    }
    uint8_t len = frame[0] - RT_FCS_LEN;
    if (!rt_is_interesting(dir, frame + 1, len)) {
        return;
    }

    bool in_isr = xPortInIsrContext();
    rt_event_t local;
    rt_event_t *ev = in_isr ? &s_isr_event : &local;
    ev->dir = dir;
    ev->channel = channel;
    ev->has_link = link != NULL;
    ev->rssi = link ? link->rssi : 0;
    ev->lqi = link ? link->lqi : 0;
    ev->tx_err = tx_err;
    ev->len = len;
    for (uint8_t i = 0; i < len; i++) {
        ev->psdu[i] = frame[1 + i];
    }

    if (in_isr) {
        BaseType_t woken = pdFALSE;
        if (xQueueSendFromISR(s_queue, ev, &woken) != pdTRUE) {
            s_dropped++;
        }
        if (woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else if (xQueueSend(s_queue, ev, 0) != pdTRUE) {
        s_dropped++;
    }
}

/* ---------------------------------------------------------------------------
 * Driver callback wrappers (-Wl,--wrap, see main/CMakeLists.txt)
 * ------------------------------------------------------------------------- */

void __real_esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info);
void __real_esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack,
                                         esp_ieee802154_frame_info_t *ack_frame_info);
void __real_esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error);
void __wrap_esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info);
void __wrap_esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack,
                                         esp_ieee802154_frame_info_t *ack_frame_info);
void __wrap_esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error);

/* Copy first: the original callback hands the buffer back to the driver. */
IRAM_ATTR void __wrap_esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info)
{
    rt_capture(RT_RX, frame, frame_info ? frame_info->channel : 0, frame_info, ESP_IEEE802154_TX_ERR_NONE);
    __real_esp_ieee802154_receive_done(frame, frame_info);
}

IRAM_ATTR void __wrap_esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack,
                                         esp_ieee802154_frame_info_t *ack_frame_info)
{
    rt_capture(RT_TX_DONE, frame, esp_ieee802154_get_channel(), ack ? ack_frame_info : NULL,
               ESP_IEEE802154_TX_ERR_NONE);
    __real_esp_ieee802154_transmit_done(frame, ack, ack_frame_info);
}

IRAM_ATTR void __wrap_esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error)
{
    rt_capture(RT_TX_FAILED, frame, esp_ieee802154_get_channel(), NULL, (uint8_t)error);
    __real_esp_ieee802154_transmit_failed(frame, error);
}

/* ---------------------------------------------------------------------------
 * Decoding and logging (radio_trace task)
 * ------------------------------------------------------------------------- */

static const char *tx_err_str(uint8_t err)
{
    switch (err) {
    case ESP_IEEE802154_TX_ERR_NONE:        return "none";
    case ESP_IEEE802154_TX_ERR_CCA_BUSY:    return "CCA busy";
    case ESP_IEEE802154_TX_ERR_ABORT:       return "abort";
    case ESP_IEEE802154_TX_ERR_NO_ACK:      return "no ACK";
    case ESP_IEEE802154_TX_ERR_INVALID_ACK: return "invalid ACK";
    case ESP_IEEE802154_TX_ERR_COEXIST:     return "coexist";
    case ESP_IEEE802154_TX_ERR_SECURITY:    return "security";
    default:                                return "unknown";
    }
}

static const char *nwk_cmd_str(uint8_t id)
{
    static const char *const names[] = {
        [0x01] = "route request",       [0x02] = "route reply",
        [0x03] = "network status",      [0x04] = "leave",
        [0x05] = "route record",        [0x06] = "rejoin request",
        [0x07] = "rejoin response",     [0x08] = "link status",
        [0x09] = "network report",      [0x0a] = "network update",
        [0x0b] = "end device timeout request",
        [0x0c] = "end device timeout response",
        [0x0d] = "link power delta",    [0x0e] = "network commissioning request",
        [0x0f] = "network commissioning response",
    };
    if (id < sizeof(names) / sizeof(names[0]) && names[id] != NULL) {
        return names[id];
    }
    return "unknown";
}

static const char *assoc_status_str(uint8_t status)
{
    switch (status) {
    case 0x00: return "success";
    case 0x01: return "PAN at capacity";
    case 0x02: return "PAN access denied";
    default:   return "other";
    }
}

static void fmt_addr(char *buf, size_t size, uint8_t mode, uint16_t short_addr, uint64_t ext_addr)
{
    if (mode == MAC_ADDR_SHORT) {
        snprintf(buf, size, "0x%04x", short_addr);
    } else if (mode == MAC_ADDR_EXT) {
        snprintf(buf, size, "0x%016llx", (unsigned long long)ext_addr);
    } else {
        snprintf(buf, size, "-");
    }
}

/* Result of a sent frame: ACK link quality or the TX error. */
static void fmt_result(char *buf, size_t size, const rt_event_t *ev)
{
    if (ev->dir == RT_TX_FAILED) {
        snprintf(buf, size, "FAILED (%s)", tx_err_str(ev->tx_err));
    } else if (ev->has_link) {
        snprintf(buf, size, "ACK LQI %u, RSSI %d dBm", ev->lqi, ev->rssi);
    } else {
        snprintf(buf, size, "sent, no ACK requested");
    }
}

static void scan_flush(void)
{
    if (s_scan.requests == 0) {
        return;
    }
    char list[80];
    size_t pos = 0;
    list[0] = '\0';
    for (unsigned ch = 11; ch <= 26 && pos < sizeof(list); ch++) {
        if (s_scan.channels & (1UL << ch)) {
            pos += snprintf(list + pos, sizeof(list) - pos, "%s%u", pos ? "," : "", ch);
        }
    }
    ESP_LOGI(TAG, "Scan: %u beacon request(s) (%u failed) on channel(s) %s, %u beacon(s) received",
             s_scan.requests, s_scan.failed, list, s_scan.beacons);
    s_scan = (rt_scan_t){0};
}

static void log_beacon(const rt_event_t *ev, const mac_hdr_t *h)
{
    const uint8_t *p = ev->psdu;
    uint8_t len = ev->len;
    unsigned off = h->hdr_len;
    char src[20];
    fmt_addr(src, sizeof(src), h->src_mode, h->src_short, h->src_ext);
    s_scan.beacons++;

    if (off + 4 > len) {
        ESP_LOGW(TAG, "RX beacon ch %u from %s: truncated", ev->channel, src);
        return;
    }
    uint16_t superframe = rd16(p + off);
    off += 2;
    uint8_t gts_count = p[off++] & MAC_GTS_COUNT_MASK;
    if (gts_count > 0) {
        off += 1 + MAC_GTS_DESC_LEN * gts_count;
    }
    if (off >= len) {
        ESP_LOGW(TAG, "RX beacon ch %u from %s: truncated", ev->channel, src);
        return;
    }
    uint8_t pending = p[off++];
    off += 2 * (pending & MAC_PEND_SHORT_MASK) + 8 * ((pending >> MAC_PEND_EXT_SHIFT) & MAC_PEND_EXT_MASK);
    bool permit = (superframe & MAC_SF_ASSOC_PERMIT) != 0;
    bool pan_coord = (superframe & MAC_SF_PAN_COORD) != 0;

    if (off + ZB_BEACON_INFO_LEN > len || p[off] != ZB_PROTOCOL_ID) {
        ESP_LOGI(TAG, "RX beacon ch %u from %s PAN 0x%04x: LQI %u, RSSI %d dBm | permit join %d, "
                      "no Zigbee beacon info",
                 ev->channel, src, h->src_pan, ev->lqi, ev->rssi, permit);
        return;
    }
    const uint8_t *z = p + off;
    ESP_LOGI(TAG, "RX beacon ch %u from %s%s PAN 0x%04x: LQI %u, RSSI %d dBm | permit join %d, "
                  "router cap %d, ED cap %d, depth %u, EXT PAN 0x%016llx, profile %u, protocol %u, update ID %u",
             ev->channel, src, pan_coord ? " (coordinator)" : "", h->src_pan, ev->lqi, ev->rssi, permit,
             (z[2] >> 2) & 0x1, (z[2] >> 7) & 0x1, (z[2] >> 3) & 0xF,
             (unsigned long long)rd64(z + 3), z[1] & 0xF, z[1] >> 4, z[14]);
}

static void log_mac_cmd(const rt_event_t *ev, const mac_hdr_t *h)
{
    const uint8_t *p = ev->psdu;
    uint8_t len = ev->len;
    uint8_t off = h->hdr_len;
    uint8_t id = p[off];
    char addr[20];
    char result[40];

    if (id == MAC_CMD_BEACON_REQ) {
        s_scan.requests++;
        s_scan.channels |= 1UL << (ev->channel & 31);
        if (ev->dir == RT_TX_FAILED) {
            s_scan.failed++;
        }
        return;
    }
    scan_flush();
    if (id == MAC_CMD_ASSOC_REQ) {
        uint8_t cap = off + 1 < len ? p[off + 1] : 0;
        fmt_addr(addr, sizeof(addr), h->dst_mode, h->dst_short, h->dst_ext);
        fmt_result(result, sizeof(result), ev);
        ESP_LOGI(TAG, "TX association request ch %u to %s (PAN 0x%04x), capability 0x%02x "
                      "(%s, rx on idle %d, alloc addr %d): %s",
                 ev->channel, addr, h->dst_pan, cap, (cap & MAC_CAP_FFD) ? "FFD" : "RFD",
                 (cap & MAC_CAP_RX_ON_IDLE) != 0, (cap & MAC_CAP_ALLOC_ADDR) != 0, result);
    } else if (id == MAC_CMD_ASSOC_RSP) {
        fmt_addr(addr, sizeof(addr), h->src_mode, h->src_short, h->src_ext);
        if (off + 4 > len) {
            ESP_LOGW(TAG, "RX association response from %s: truncated", addr);
            return;
        }
        uint8_t status = p[off + 3];
        ESP_LOGI(TAG, "RX association response ch %u from %s: status 0x%02x (%s), short address 0x%04x, "
                      "LQI %u, RSSI %d dBm",
                 ev->channel, addr, status, assoc_status_str(status), rd16(p + off + 1), ev->lqi, ev->rssi);
    }
}

static void log_nwk_cmd(const rt_event_t *ev, const mac_hdr_t *h)
{
    const uint8_t *p = ev->psdu;
    uint8_t len = ev->len;
    unsigned off = h->hdr_len;
    uint16_t fcf = rd16(p + off);
    char mac_src[20];
    char mac_dst[20];
    char cmd[48];
    char link[40];

    scan_flush();
    fmt_addr(mac_src, sizeof(mac_src), h->src_mode, h->src_short, h->src_ext);
    fmt_addr(mac_dst, sizeof(mac_dst), h->dst_mode, h->dst_short, h->dst_ext);
    if (fcf & NWK_FCF_SECURITY) {
        snprintf(cmd, sizeof(cmd), "secured (command ID encrypted)");
    } else if (fcf & NWK_FCF_SRC_ROUTE) {
        snprintf(cmd, sizeof(cmd), "source routed (not decoded)");
    } else {
        unsigned cmd_off = off + NWK_HDR_MIN_LEN
                           + ((fcf & NWK_FCF_DST_IEEE) ? NWK_IEEE_LEN : 0)
                           + ((fcf & NWK_FCF_SRC_IEEE) ? NWK_IEEE_LEN : 0)
                           + ((fcf & NWK_FCF_MULTICAST) ? NWK_MULTICAST_CTRL_LEN : 0);
        if (cmd_off < len) {
            snprintf(cmd, sizeof(cmd), "0x%02x %s", p[cmd_off], nwk_cmd_str(p[cmd_off]));
        } else {
            snprintf(cmd, sizeof(cmd), "truncated");
        }
    }
    if (ev->dir == RT_RX) {
        snprintf(link, sizeof(link), "LQI %u, RSSI %d dBm", ev->lqi, ev->rssi);
    } else {
        fmt_result(link, sizeof(link), ev);
    }
    ESP_LOGI(TAG, "%s NWK command ch %u, MAC %s -> %s, NWK 0x%04x -> 0x%04x: %s | %s",
             ev->dir == RT_RX ? "RX" : "TX", ev->channel, mac_src, mac_dst,
             rd16(p + off + 4), rd16(p + off + 2), cmd, link);
}

static void rt_task(void *arg)
{
    rt_event_t ev;
    uint32_t reported_drops = 0;

    for (;;) {
        // Block indefinitely unless a scan summary is pending (no periodic wakeup)
        TickType_t wait = s_scan.requests ? pdMS_TO_TICKS(RT_SCAN_FLUSH_MS) : portMAX_DELAY;
        if (xQueueReceive(s_queue, &ev, wait) != pdTRUE) {
            scan_flush();
            continue;
        }
        mac_hdr_t h;
        if (mac_parse(ev.psdu, ev.len, &h)) {
            switch (h.type) {
            case MAC_TYPE_BEACON:
                log_beacon(&ev, &h);
                break;
            case MAC_TYPE_CMD:
                log_mac_cmd(&ev, &h);
                break;
            case MAC_TYPE_DATA:
                log_nwk_cmd(&ev, &h);
                break;
            default:
                break;
            }
        }
        uint32_t dropped = s_dropped;
        if (dropped != reported_drops) {
            ESP_LOGW(TAG, "%" PRIu32 " frame(s) not traced (queue full)", dropped - reported_drops);
            reported_drops = dropped;
        }
    }
}

esp_err_t radio_trace_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(RT_QUEUE_LEN, sizeof(rt_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(rt_task, "radio_trace", RT_TASK_STACK_SIZE, NULL, RT_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "802.15.4 radio trace active (DEBUG, passive)");
    return ESP_OK;
}

#else // !CONFIG_ZL_DEBUG_RADIO_TRACE

esp_err_t radio_trace_init(void)
{
    return ESP_OK;
}

#endif
