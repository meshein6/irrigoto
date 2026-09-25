/*
 * solution.c -- "Apply solution" dosing. See solution.h.
 */
#include "solution.h"
#include "pump.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "solution";

#define NVS_NS          "solution"
#define NVS_KEY_CAL     "cal"
#define NVS_KEY_RT      "rt"
#define CAL_MAGIC       0x534F4C31u   /* 'SOL1' */
#define RT_MAGIC        0x534F5254u   /* 'SORT' */
#define TASK_STACK      3072
#define TICK_MS         100

/* ── Persistent state ─────────────────────────────────────────────────────── */

/* speed index: 0 = 100 %, 1 = 80 %, 2 = 60 % */
static const uint8_t SPEEDS[3] = { 100, 80, 60 };

typedef struct {
    uint32_t magic;
    float    rate[SOLUTION_BOTTLES][3];   /* mL/s, 0 = not set */
} sol_cal_t;

typedef struct {
    uint32_t id;           /* schedule entry id, 0 = free slot */
    uint16_t runs_since;   /* watered runs since the last dose (Every Nth) */
    uint8_t  rot;          /* rotation pointer into the entry's bottle set */
    uint8_t  _pad;
} sol_rt_entry_t;

typedef struct {
    uint32_t       magic;
    sol_rt_entry_t e[SCHEDULE_MAX_ENTRIES];
} sol_rt_t;

static sol_cal_t s_cal;
static sol_rt_t  s_rt;

static int speed_index(uint8_t speed)
{
    for (int i = 0; i < 3; i++) if (SPEEDS[i] == speed) return i;
    return -1;
}

static void nvs_load(const char *key, void *blob, size_t sz, uint32_t magic)
{
    nvs_handle_t h;
    memset(blob, 0, sz);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t got = sz;
        if (nvs_get_blob(h, key, blob, &got) != ESP_OK || got != sz
                || *(uint32_t *)blob != magic) {
            memset(blob, 0, sz);
        }
        nvs_close(h);
    }
    *(uint32_t *)blob = magic;
}

static void nvs_store(const char *key, const void *blob, size_t sz)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, key, blob, sz);
    nvs_commit(h);
    nvs_close(h);
}

static sol_rt_entry_t *rt_find(uint32_t id, bool create)
{
    if (id == 0) return NULL;
    sol_rt_entry_t *free_slot = NULL;
    for (int i = 0; i < SCHEDULE_MAX_ENTRIES; i++) {
        if (s_rt.e[i].id == id) return &s_rt.e[i];
        if (!free_slot && s_rt.e[i].id == 0) free_slot = &s_rt.e[i];
    }
    if (!create) return NULL;
    if (!free_slot) free_slot = &s_rt.e[0];     /* table full: recycle slot 0 */
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->id = id;
    return free_slot;
}

/* ── Live run ─────────────────────────────────────────────────────────────── */

typedef enum { PH_IDLE, PH_WAITING, PH_DELAY, PH_DOSING, PH_DONE } phase_t;

static struct {
    bool           armed;
    uint32_t       entry_id;     /* 0 = manual */
    uint8_t        bottle;       /* 1..3 */
    solution_cfg_t cfg;
    int            est_run_min;
    volatile phase_t phase;
    volatile bool  flow;         /* set by solution_note_flow() */
    volatile bool  stop;         /* set by solution_on_run_end() */
    volatile bool  task_alive;
    bool           started;      /* pump ran at least once this run */
    uint32_t       pump_ms;      /* accumulated pump-on time */
} s_run;

static uint8_t popcount3(uint8_t m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1); }

static uint8_t nth_set_bit(uint8_t mask, uint8_t k)
{
    for (uint8_t b = 0; b < SOLUTION_BOTTLES; b++)
        if (mask & (1u << b)) { if (k == 0) return b + 1; k--; }
    return 1;
}

/* Sleep in TICK_MS slices so a stop request is honoured within 100 ms. */
static bool wait_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += TICK_MS) {
        if (s_run.stop) return false;
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
    return !s_run.stop;
}

/* ms == 0 means "until stopped". */
static void run_pump_for(uint32_t ms)
{
    if (!pump_start(s_run.bottle, s_run.cfg.speed)) return;
    s_run.started = true;
    while (!s_run.stop && (ms == 0 || pump_elapsed_ms() < ms))
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    s_run.pump_ms += pump_elapsed_ms();
    pump_stop();
}

static void solution_task(void *arg)
{
    (void)arg;
    s_run.phase = PH_WAITING;
    while (!s_run.flow && !s_run.stop) vTaskDelay(pdMS_TO_TICKS(TICK_MS));

    if (!s_run.stop) {
        s_run.phase = PH_DELAY;
        ESP_LOGI(TAG, "water flowing -- dosing bottle %u at %u%% in %d s",
                 s_run.bottle, s_run.cfg.speed, SOLUTION_START_DELAY_S);
        wait_ms(SOLUTION_START_DELAY_S * 1000u);
    }

    if (!s_run.stop) {
        s_run.phase = PH_DOSING;
        if (s_run.cfg.pulse) {
            while (!s_run.stop) {
                run_pump_for(s_run.cfg.on_s * 1000u);
                wait_ms(s_run.cfg.off_s * 1000u);
            }
        } else {
            run_pump_for(0);
        }
    }

    pump_stop();
    s_run.phase = PH_DONE;
    s_run.task_alive = false;
    vTaskDelete(NULL);
}

static void arm(uint32_t entry_id, uint8_t bottle, const solution_cfg_t *cfg)
{
    solution_on_run_end();              /* never stack two runs */
    memset(&s_run, 0, sizeof(s_run));
    s_run.armed    = true;
    s_run.entry_id = entry_id;
    s_run.bottle   = bottle;
    s_run.cfg      = *cfg;
    s_run.phase    = PH_IDLE;
    s_run.task_alive = true;
    if (xTaskCreatePinnedToCore(solution_task, "solution", TASK_STACK, NULL, 5,
                                NULL, PRO_CPU_NUM) != pdPASS) {
        ESP_LOGE(TAG, "task create failed -- no dose this run");
        s_run.armed = false;
        s_run.task_alive = false;
    }
}

/* ── Public: init / calibration ───────────────────────────────────────────── */

void solution_init(void)
{
    nvs_load(NVS_KEY_CAL, &s_cal, sizeof(s_cal), CAL_MAGIC);
    nvs_load(NVS_KEY_RT,  &s_rt,  sizeof(s_rt),  RT_MAGIC);
    memset(&s_run, 0, sizeof(s_run));
}

float solution_rate(uint8_t bottle, uint8_t speed)
{
    int si = speed_index(speed);
    if (bottle < 1 || bottle > SOLUTION_BOTTLES || si < 0) return 0.0f;
    return s_cal.rate[bottle - 1][si];
}

bool solution_set_rate(uint8_t bottle, uint8_t speed, float ml_s)
{
    int si = speed_index(speed);
    if (bottle < 1 || bottle > SOLUTION_BOTTLES || si < 0) return false;
    if (!(ml_s >= 0.0f) || ml_s > 100.0f) return false;
    s_cal.rate[bottle - 1][si] = ml_s;
    nvs_store(NVS_KEY_CAL, &s_cal, sizeof(s_cal));
    return true;
}

int solution_cal_json(char *buf, size_t len)
{
    int n = snprintf(buf, len, "{\"speeds\":[100,80,60],\"rates\":[");
    for (int b = 0; b < SOLUTION_BOTTLES; b++) {
        n += snprintf(buf + n, len - n, "%s[%.3f,%.3f,%.3f]", b ? "," : "",
                      s_cal.rate[b][0], s_cal.rate[b][1], s_cal.rate[b][2]);
    }
    n += snprintf(buf + n, len - n, "]}");
    return n;
}

/* ── Public: schedule entry helpers ───────────────────────────────────────── */

void solution_cfg_from_entry(const schedule_entry_t *e, solution_cfg_t *cfg)
{
    cfg->bottles = (e->solution_bottles & 0x07) ? (e->solution_bottles & 0x07) : SOLUTION_DEF_BOTTLES;
    cfg->speed   = e->solution_speed ? pump_clamp_speed(e->solution_speed) : SOLUTION_DEF_SPEED;
    cfg->pulse   = e->solution_pulse ? 1 : 0;
    cfg->on_s    = e->solution_pulse_on_s  ? e->solution_pulse_on_s  : SOLUTION_DEF_ON_S;
    cfg->off_s   = e->solution_pulse_off_s ? e->solution_pulse_off_s : SOLUTION_DEF_OFF_S;
}

void solution_entry_normalize(schedule_entry_t *e)
{
    e->solution_enabled  = e->solution_enabled ? 1 : 0;
    e->solution_bottles &= 0x07;
    e->solution_when     = (e->solution_when == SOLUTION_WHEN_NTH) ? SOLUTION_WHEN_NTH : SOLUTION_WHEN_EVERY;
    if (e->solution_every_n) {
        if (e->solution_every_n < SOLUTION_EVERY_N_MIN) e->solution_every_n = SOLUTION_EVERY_N_MIN;
        if (e->solution_every_n > SOLUTION_EVERY_N_MAX) e->solution_every_n = SOLUTION_EVERY_N_MAX;
    }
    if (e->solution_speed) e->solution_speed = pump_clamp_speed(e->solution_speed);
    e->solution_pulse = e->solution_pulse ? 1 : 0;
    if (e->solution_pulse_on_s  > SOLUTION_PULSE_S_MAX) e->solution_pulse_on_s  = SOLUTION_PULSE_S_MAX;
    if (e->solution_pulse_off_s > SOLUTION_PULSE_S_MAX) e->solution_pulse_off_s = SOLUTION_PULSE_S_MAX;
}

float solution_estimate_ml(const solution_cfg_t *cfg, uint8_t bottle, int est_run_min)
{
    float rate = solution_rate(bottle, cfg->speed);
    if (rate <= 0.0f) return -1.0f;
    float pump_s = (float)est_run_min * 60.0f - SOLUTION_START_DELAY_S;
    if (pump_s < 0.0f) pump_s = 0.0f;
    if (cfg->pulse) pump_s *= (float)cfg->on_s / (float)(cfg->on_s + cfg->off_s);
    return pump_s * rate;
}

int solution_entry_json(const schedule_entry_t *e, char *buf, size_t len)
{
    solution_cfg_t c; solution_cfg_from_entry(e, &c);
    return snprintf(buf, len,
        ",\"solution_enabled\":%u,\"solution_bottles\":%u,\"solution_when\":%u,"
        "\"solution_every_n\":%u,\"solution_speed\":%u,\"solution_pulse\":%u,"
        "\"solution_pulse_on_s\":%u,\"solution_pulse_off_s\":%u",
        e->solution_enabled ? 1 : 0, c.bottles,
        e->solution_when == SOLUTION_WHEN_NTH ? 1 : 0,
        e->solution_every_n ? e->solution_every_n : SOLUTION_DEF_EVERY_N,
        c.speed, c.pulse, c.on_s, c.off_s);
}

/* ── Public: run lifecycle ────────────────────────────────────────────────── */

void solution_arm_entry(const schedule_entry_t *e)
{
    solution_on_run_end();
    if (!e || !e->solution_enabled) return;

    sol_rt_entry_t *rt = rt_find(e->id, true);
    if (e->solution_when == SOLUTION_WHEN_NTH) {
        uint8_t n = e->solution_every_n ? e->solution_every_n : SOLUTION_DEF_EVERY_N;
        /* runs_since counts completed watered runs; this run is number
         * runs_since+1. Dose when that reaches N. Counter commit happens
         * in solution_on_run_end() once we know the run actually watered. */
        if (rt && rt->runs_since + 1 < n) {
            ESP_LOGI(TAG, "entry %lu: run %u of %u -- no dose this time",
                     (unsigned long)e->id, rt->runs_since + 1, n);
            return;
        }
    }

    solution_cfg_t cfg; solution_cfg_from_entry(e, &cfg);
    uint8_t count  = popcount3(cfg.bottles);
    uint8_t bottle = nth_set_bit(cfg.bottles, rt ? (rt->rot % count) : 0);
    arm(e->id, bottle, &cfg);
}

void solution_arm_manual(uint8_t bottle, const solution_cfg_t *cfg)
{
    solution_on_run_end();
    if (bottle < 1 || bottle > SOLUTION_BOTTLES || !cfg) return;
    solution_cfg_t c = *cfg;
    c.bottles = (uint8_t)(1u << (bottle - 1));
    c.speed   = pump_clamp_speed(c.speed);
    if (c.on_s  < SOLUTION_PULSE_S_MIN) c.on_s  = SOLUTION_DEF_ON_S;
    if (c.off_s < SOLUTION_PULSE_S_MIN) c.off_s = SOLUTION_DEF_OFF_S;
    if (c.on_s  > SOLUTION_PULSE_S_MAX) c.on_s  = SOLUTION_PULSE_S_MAX;
    if (c.off_s > SOLUTION_PULSE_S_MAX) c.off_s = SOLUTION_PULSE_S_MAX;
    arm(0, bottle, &c);
}

void solution_note_flow(void)
{
    if (s_run.armed) s_run.flow = true;
}

/* Every exit path lands here. Also called by arm() to clear a stale run and
 * by the host before sleep. Idempotent. */
void solution_on_run_end(void)
{
    if (s_run.task_alive) {
        s_run.stop = true;
        for (int i = 0; i < 30 && s_run.task_alive; i++) vTaskDelay(pdMS_TO_TICKS(10));
    }
    pump_stop();
    if (!s_run.armed) return;

    if (s_run.entry_id) {
        sol_rt_entry_t *rt = rt_find(s_run.entry_id, true);
        if (rt) {
            if (s_run.started)   { rt->runs_since = 0; rt->rot++; }
            else if (s_run.flow) { rt->runs_since++; }
            nvs_store(NVS_KEY_RT, &s_rt, sizeof(s_rt));
        }
    }
    if (s_run.started) {
        ESP_LOGI(TAG, "run end: bottle %u, %u%%, pump on %lu s%s",
                 s_run.bottle, s_run.cfg.speed,
                 (unsigned long)(s_run.pump_ms / 1000),
                 s_run.entry_id ? "" : " (manual)");
    }
    s_run.armed = false;
    s_run.phase = PH_IDLE;
}

/* ── Public: status ───────────────────────────────────────────────────────── */

bool     solution_armed(void)        { return s_run.armed; }
bool     solution_pumping(void)      { return s_run.armed && pump_running(); }
uint8_t  solution_bottle(void)       { return s_run.armed ? s_run.bottle : 0; }
uint32_t solution_pump_seconds(void)
{
    return (s_run.pump_ms + (pump_running() ? pump_elapsed_ms() : 0)) / 1000;
}

int solution_status_json(char *buf, size_t len)
{
    static const char *PH[] = { "idle", "waiting", "delay", "dosing", "done" };
    float rate = s_run.armed ? solution_rate(s_run.bottle, s_run.cfg.speed) : 0.0f;
    return snprintf(buf, len,
        "{\"armed\":%s,\"phase\":\"%s\",\"bottle\":%u,\"speed\":%u,\"pulse\":%u,"
        "\"pump_s\":%lu,\"ml\":%.1f}",
        s_run.armed ? "true" : "false", PH[s_run.phase],
        solution_bottle(), s_run.armed ? s_run.cfg.speed : 0,
        s_run.armed ? s_run.cfg.pulse : 0,
        (unsigned long)solution_pump_seconds(),
        rate > 0.0f ? (float)solution_pump_seconds() * rate : 0.0f);
}
