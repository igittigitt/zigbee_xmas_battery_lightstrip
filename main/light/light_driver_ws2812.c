/*
 * Light backend for DEBUG builds: on-board WS2812 of the ESP32-H2-Zero,
 * driven by the RMT peripheral with a plain bytes encoder.
 *
 * The RMT channel is only enabled while a frame is sent: an enabled RMT TX
 * channel without DMA holds an ESP_PM_CPU_FREQ_MAX lock (IDF v5.5.1,
 * esp_driver_rmt/src/rmt_common.c), which would block automatic light sleep.
 * The WS2812 keeps its colour on its own between frames.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "light_config.h"
#include "light_driver.h"

static const char *TAG = "LIGHT_WS2812";

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_encoder;

static uint8_t scale(uint8_t color, uint8_t level)
{
    return (uint8_t)(((uint32_t)color * level) / LIGHT_LEVEL_MAX);
}

static esp_err_t ws2812_set_level(uint8_t level)
{
    // Byte order per LIGHT_WS2812_ORDER_RGB, MSB first
    const uint8_t r = scale(LIGHT_WS2812_COLOR_R, level);
    const uint8_t g = scale(LIGHT_WS2812_COLOR_G, level);
    const uint8_t b = scale(LIGHT_WS2812_COLOR_B, level);
    const uint8_t frame[3] = {
        LIGHT_WS2812_ORDER_RGB ? r : g,
        LIGHT_WS2812_ORDER_RGB ? g : r,
        b,
    };
    const rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };

    ESP_RETURN_ON_ERROR(rmt_enable(s_chan), TAG, "rmt_enable");
    esp_err_t err = rmt_transmit(s_chan, s_encoder, frame, sizeof(frame), &tx_cfg);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s_chan, LIGHT_WS2812_TX_TIMEOUT_MS);
    }
    esp_err_t dis_err = rmt_disable(s_chan);
    return err != ESP_OK ? err : dis_err;
}

static esp_err_t ws2812_init(void)
{
    const rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = LIGHT_WS2812_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LIGHT_WS2812_RMT_RESOLUTION_HZ,
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&chan_cfg, &s_chan), TAG, "rmt_new_tx_channel");

    const rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1, .duration0 = LIGHT_WS2812_T0H_TICKS,
            .level1 = 0, .duration1 = LIGHT_WS2812_T0L_TICKS,
        },
        .bit1 = {
            .level0 = 1, .duration0 = LIGHT_WS2812_T1H_TICKS,
            .level1 = 0, .duration1 = LIGHT_WS2812_T1L_TICKS,
        },
        .flags.msb_first = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&enc_cfg, &s_encoder), TAG, "rmt_new_bytes_encoder");

    // Keep the data line driven during light sleep. With
    // CONFIG_PM_SLP_DISABLE_GPIO the pad would otherwise be released and the
    // board's pull-up R1 (10 kOhm to 3V3) would pull DI high.
    gpio_sleep_sel_dis(LIGHT_WS2812_GPIO);

    ESP_LOGI(TAG, "WS2812 on GPIO %d", LIGHT_WS2812_GPIO);
    return ws2812_set_level(0);
}

const light_driver_t light_driver_ws2812 = {
    .name = "ws2812",
    .gpio_mask = 1ULL << LIGHT_WS2812_GPIO,
    .init = ws2812_init,
    .power = NULL,
    .set_level = ws2812_set_level,
    .fade = NULL,   // software ramp in light.c
};
