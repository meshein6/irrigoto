/*
 * pump.c -- peristaltic solution pump driver. See pump.h.
 */
#include "pump.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "pump";

#define PUMP_GPIO_DRIVE   GPIO_NUM_16
#define PUMP_GPIO_SEL2    GPIO_NUM_21
#define PUMP_GPIO_SEL3    GPIO_NUM_19
#define PUMP_LEDC_TIMER   LEDC_TIMER_3
#define PUMP_LEDC_CH      LEDC_CHANNEL_7
#define PUMP_LEDC_MODE    LEDC_HIGH_SPEED_MODE
#define PUMP_PWM_HZ       20000u          /* measured: only frequency that runs loaded */
#define PUMP_SELECT_SETTLE_MS 20          /* selector high before drive, as probed */

#define PCUR_ZERO_MV      142.0f
#define PCUR_MA_PER_MV    0.2f

static pump_hal_t s_hal;
static bool       s_running   = false;
static bool       s_pwm       = false;    /* LEDC attached to the drive pin */
static bool       s_own_rail  = false;    /* we raised the rail -> we lower it */
static uint8_t    s_pump      = 0;
static uint8_t    s_speed     = 0;
static int64_t    s_start_us  = 0;

static gpio_num_t selector_for(uint8_t pump)
{
    return (pump == 2) ? PUMP_GPIO_SEL2 :
           (pump == 3) ? PUMP_GPIO_SEL3 : GPIO_NUM_NC;
}

/* All three control lines back to floating inputs -- the idle state the
 * hardware was probed in. */
static void lines_release(void)
{
    if (s_pwm) {
        ledc_stop(PUMP_LEDC_MODE, PUMP_LEDC_CH, 0);
        ledc_timer_pause(PUMP_LEDC_MODE, PUMP_LEDC_TIMER);
        s_pwm = false;
    }
    gpio_reset_pin(PUMP_GPIO_DRIVE);
    gpio_set_direction(PUMP_GPIO_DRIVE, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PUMP_GPIO_DRIVE, GPIO_FLOATING);
    gpio_set_direction(PUMP_GPIO_SEL2, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PUMP_GPIO_SEL2, GPIO_FLOATING);
    gpio_set_direction(PUMP_GPIO_SEL3, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PUMP_GPIO_SEL3, GPIO_FLOATING);
}

void pump_init(const pump_hal_t *hal)
{
    memset(&s_hal, 0, sizeof(s_hal));
    if (hal) s_hal = *hal;
    lines_release();
}

bool pump_start(uint8_t pump, uint8_t speed_pct)
{
    if (pump < 1 || pump > PUMP_COUNT) return false;
    uint8_t speed = pump_clamp_speed(speed_pct);

    /* Latest request wins: a running pump is killed, not refused. */
    if (s_running) {
        bool had_rail = s_own_rail;
        lines_release();
        s_running = false;
        s_own_rail = had_rail;            /* keep the rail through the swap */
    }

    if (s_hal.rail_is_on && !s_hal.rail_is_on()) {
        if (s_hal.rail_on) s_hal.rail_on();
        s_own_rail = true;
    }

    gpio_num_t sel = selector_for(pump);
    if (sel != GPIO_NUM_NC) {
        gpio_set_level(sel, 1);
        gpio_set_direction(sel, GPIO_MODE_OUTPUT);
        vTaskDelay(pdMS_TO_TICKS(PUMP_SELECT_SETTLE_MS));
    }

    if (speed < PUMP_SPEED_MAX) {
        ledc_timer_config_t tc = {
            .speed_mode      = PUMP_LEDC_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num       = PUMP_LEDC_TIMER,
            .freq_hz         = PUMP_PWM_HZ,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        ledc_timer_config(&tc);
        ledc_channel_config_t cc = {
            .gpio_num   = PUMP_GPIO_DRIVE,
            .speed_mode = PUMP_LEDC_MODE,
            .channel    = PUMP_LEDC_CH,
            .timer_sel  = PUMP_LEDC_TIMER,
            .duty       = (uint32_t)speed * 1023u / 100u,
            .hpoint     = 0,
        };
        ledc_channel_config(&cc);
        s_pwm = true;
    } else {
        gpio_set_level(PUMP_GPIO_DRIVE, 1);
        gpio_set_direction(PUMP_GPIO_DRIVE, GPIO_MODE_OUTPUT);
    }

    s_running  = true;
    s_pump     = pump;
    s_speed    = speed;
    s_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "pump %u ON at %u%%", pump, speed);
    return true;
}

void pump_stop(void)
{
    if (!s_running) return;
    s_running = false;
    lines_release();
    ESP_LOGI(TAG, "pump %u OFF after %lu ms", s_pump,
             (unsigned long)((esp_timer_get_time() - s_start_us) / 1000));
    s_pump = 0; s_speed = 0;
    if (s_own_rail) {
        s_own_rail = false;
        if (s_hal.rail_off) s_hal.rail_off();
    }
}

bool     pump_running(void)    { return s_running; }
uint8_t  pump_active(void)     { return s_running ? s_pump : 0; }
uint8_t  pump_speed(void)      { return s_running ? s_speed : 0; }
uint32_t pump_elapsed_ms(void)
{
    return s_running ? (uint32_t)((esp_timer_get_time() - s_start_us) / 1000) : 0;
}

float pump_read_ma(void)
{
    if (!s_hal.pcur_mv) return 0.0f;
    float ma = ((float)s_hal.pcur_mv() - PCUR_ZERO_MV) * PCUR_MA_PER_MV;
    return ma < 0.0f ? 0.0f : ma;
}
