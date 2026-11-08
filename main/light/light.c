#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "app_config.h"
#include "light.h"
#include "light_config.h"
#include "light_driver.h"

static const char *TAG = "LIGHT";

typedef enum {
    LIGHT_CMD_FADE,     // fade to level within ramp_ms
    LIGHT_CMD_PATTERN,  // flash pattern, then fade to s_target within ramp_ms
} light_cmd_type_t;

typedef struct {
    light_cmd_type_t type;
    uint8_t level;              // FADE: target level, PATTERN: level of the "on" phases
    uint32_t ramp_ms;           // FADE: ramp, PATTERN: ramp to s_target afterwards
    light_pattern_t pattern;    // PATTERN only
} light_cmd_t;

static const light_driver_t *s_drv;
static QueueHandle_t s_queue;
static uint8_t s_level;                 // momentary output level (light task only)
static volatile uint8_t s_target;       // last requested (persistent) level
static volatile uint8_t s_on_level = LIGHT_LEVEL_ON;   // level used for "on" (dimming)

static esp_err_t light_power(bool on)
{
    return s_drv->power ? s_drv->power(on) : ESP_OK;
}

static bool light_cmd_pending(void)
{
    return uxQueueMessagesWaiting(s_queue) > 0;
}

/**
 * Software ramp via set_level(). Aborts early if a new command arrives,
 * s_level then holds the level reached so far.
 */
static void light_software_ramp(uint8_t target, uint32_t duration_ms)
{
    const uint8_t start = s_level;
    const uint32_t steps = duration_ms / LIGHT_SW_RAMP_STEP_MS;

    for (uint32_t i = 1; i <= steps; i++) {
        if (light_cmd_pending()) {
            return;
        }
        uint8_t level = (uint8_t)(start + ((int32_t)target - start) * (int32_t)i / (int32_t)steps);
        if (s_drv->set_level(level) == ESP_OK) {
            s_level = level;
        }
        vTaskDelay(pdMS_TO_TICKS(LIGHT_SW_RAMP_STEP_MS));
    }
    if (s_drv->set_level(target) == ESP_OK) {
        s_level = target;
    }
}

/** Move the output to level; ramp_ms refers to a full 0 <-> max transition. */
static void light_apply_level(uint8_t level, uint32_t ramp_ms)
{
    if (level == s_level) {
        return;
    }

    if (s_level == 0) {
        esp_err_t err = light_power(true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Power on failed: %s", esp_err_to_name(err));
            return;
        }
    }

    // Scale the ramp to the actual distance
    uint32_t duration_ms = ramp_ms * (uint32_t)abs((int)level - (int)s_level) / LIGHT_LEVEL_MAX;

    if (duration_ms == 0) {
        if (s_drv->set_level(level) == ESP_OK) {
            s_level = level;
        }
    } else if (s_drv->fade) {
        esp_err_t err = s_drv->fade(level, duration_ms);
        if (err == ESP_OK) {
            s_level = level;
        } else {
            ESP_LOGE(TAG, "Fade failed: %s", esp_err_to_name(err));
        }
    } else {
        light_software_ramp(level, duration_ms);
    }

    if (s_level == 0) {
        light_power(false);
    }
}

/**
 * Run a flash pattern ("on" phase at pattern level, then "off" phase), count
 * times or endlessly, then fade to s_target. With off_ms = 0 (count = 1) the
 * light fades from the pulse level directly to s_target. A new command aborts the
 * pattern; the new command then defines the light state.
 */
static void light_run_pattern(const light_cmd_t *cmd)
{
    const light_pattern_t *p = &cmd->pattern;
    TickType_t last_wake = xTaskGetTickCount();

    for (uint32_t i = 0; p->count == 0 || i < p->count; i++) {
        if (light_cmd_pending()) {
            return;
        }
        light_apply_level(cmd->level, LIGHT_BLINK_RAMP_MS);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(p->on_ms));
        if (p->off_ms == 0) {
            break;  // single steady pulse: fade directly to s_target
        }
        if (light_cmd_pending()) {
            return;
        }
        light_apply_level(0, LIGHT_BLINK_RAMP_MS);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(p->off_ms));
    }
    if (!light_cmd_pending()) {
        light_apply_level(s_target, cmd->ramp_ms);
    }
}

static void light_task(void *arg)
{
    light_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.type == LIGHT_CMD_PATTERN) {
                light_run_pattern(&cmd);
            } else {
                light_apply_level(cmd.level, cmd.ramp_ms);
            }
            ESP_LOGD(TAG, "Level now %u", s_level);
        }
    }
}

esp_err_t light_init(void)
{
    if (s_queue) {
        return ESP_OK;
    }

    s_drv = APP_DEBUG ? &light_driver_ws2812 : &light_driver_pwm;

    esp_err_t err = s_drv->init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Backend %s init failed: %s", s_drv->name, esp_err_to_name(err));
        return err;
    }

    // Queue length 1 + xQueueOverwrite: only the newest request matters
    s_queue = xQueueCreate(1, sizeof(light_cmd_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(light_task, "light", LIGHT_TASK_STACK_SIZE, NULL,
                    LIGHT_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Light initialised, backend: %s (DEBUG=%d)", s_drv->name, APP_DEBUG);
    return ESP_OK;
}

esp_err_t light_fade_to(uint8_t level, uint32_t ramp_ms)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    const light_cmd_t cmd = {
        .type = LIGHT_CMD_FADE,
        .level = level,
        .ramp_ms = ramp_ms,
    };
    s_target = level;
    xQueueOverwrite(s_queue, &cmd);
    return ESP_OK;
}

esp_err_t light_set_on(bool on)
{
    return light_fade_to(on ? s_on_level : 0, LIGHT_RAMP_MS);
}

esp_err_t light_set_on_level(uint8_t level)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    s_on_level = level ? level : 1;     // "on" never means dark
    if (s_target == 0) {
        return ESP_OK;                  // off: only remember the level
    }
    return light_fade_to(s_on_level, LIGHT_RAMP_MS);
}

static esp_err_t light_pattern_queue(const light_pattern_t *pattern, uint32_t final_ramp_ms)
{
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    // off_ms = 0 is only allowed for a single steady pulse (count = 1)
    if (!pattern || pattern->on_ms < LIGHT_BLINK_RAMP_MS ||
        (pattern->off_ms < LIGHT_BLINK_RAMP_MS && !(pattern->off_ms == 0 && pattern->count == 1))) {
        return ESP_ERR_INVALID_ARG;
    }
    const light_cmd_t cmd = {
        .type = LIGHT_CMD_PATTERN,
        .level = pattern->level,
        .ramp_ms = final_ramp_ms,
        .pattern = *pattern,
    };
    xQueueOverwrite(s_queue, &cmd);
    return ESP_OK;
}

esp_err_t light_pattern_start(const light_pattern_t *pattern)
{
    // Afterwards back to the current state, quickly (like the blink edges)
    return light_pattern_queue(pattern, LIGHT_BLINK_RAMP_MS);
}

esp_err_t light_pattern_then(const light_pattern_t *pattern, bool on)
{
    // Afterwards switch to on/off with the soft ramp (goal Z3)
    s_target = on ? s_on_level : 0;
    return light_pattern_queue(pattern, LIGHT_RAMP_MS);
}

uint8_t light_get_level(void)
{
    return s_target;
}

uint64_t light_get_gpio_mask(void)
{
    return s_drv ? s_drv->gpio_mask : 0;
}
