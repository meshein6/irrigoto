#pragma once
/*
 * solution.h -- "Apply solution": dose a watering run from one of three
 * bottles using the peristaltic pumps (pump.h).
 *
 * Owns three things, kept out of the schedule store so upstream snapshots and
 * HA schedule pushes can never disturb them:
 *   - calibration  : mL/s per bottle per pump speed, entered on the Bottle
 *                    calibration page, stored in /lfs/cal/bottle.json with
 *                    built-in defaults (0.30 / 0.23 / 0.17 mL/s). Used ONLY
 *                    for the "~X mL per run" estimate; the device cannot
 *                    measure flow.
 *   - run counters : per schedule-entry id, "runs since last dose" (Every
 *                    Nth run) and the bottle rotation pointer.
 *   - the live run : armed config for the watering run in progress, the
 *                    state machine that waits for water to flow, then holds
 *                    the pump on (or pulses it) until the run ends.
 *
 * Lifecycle, driven by the watering code in irrigoto.c:
 *   solution_arm_entry() / solution_arm_manual()   before the run starts
 *   solution_note_flow()                           water confirmed flowing
 *   solution_on_run_end()                          every exit path
 *
 * The pump is only ever on between note_flow + SOLUTION_START_DELAY_S and
 * on_run_end. pump_stop() is also wired into the host's motor-rail-off, so a
 * rail drop from any other path (sleep, fault) kills it too.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "irrigoto_types.h"

#define SOLUTION_BOTTLES        3
#define SOLUTION_START_DELAY_S  10      /* after water is confirmed flowing */
#define SOLUTION_EVERY_N_MIN    2
#define SOLUTION_EVERY_N_MAX    20
#define SOLUTION_PULSE_S_MIN    1
#define SOLUTION_PULSE_S_MAX    600

/* Defaults applied wherever a stored field is 0 (migrated / HA-inserted). */
#define SOLUTION_DEF_BOTTLES    0x01    /* bottle 1 */
#define SOLUTION_DEF_SPEED      100
#define SOLUTION_DEF_ON_S       2
#define SOLUTION_DEF_OFF_S      4
#define SOLUTION_DEF_EVERY_N    3

enum { SOLUTION_WHEN_EVERY = 0, SOLUTION_WHEN_NTH = 1 };

/* The per-run dosing recipe, defaults already applied. */
typedef struct {
    uint8_t  bottles;   /* bitmask bit0..2; a manual run has exactly one bit */
    uint8_t  speed;     /* 60 / 80 / 100 */
    uint8_t  pulse;     /* 0 continuous, 1 on/off */
    uint16_t on_s;
    uint16_t off_s;
} solution_cfg_t;

void  solution_init(void);        /* load cal file + NVS; call after pump_init() and storage_init() */

/* ── Enable ("Bottles" device setting) ──
 * Off: no dose is ever armed (manual or scheduled) and the pages hide every
 * bottle control. Entry settings are kept, so turning it back on restores
 * them. Persisted in NVS; first boot defaults to on only if the unit already
 * had a bottle calibration. */
bool  solution_enabled(void);
void  solution_set_enabled(bool on);

/* ── Calibration ── */
float solution_rate(uint8_t bottle, uint8_t speed);                 /* mL/s (calibrated or default); 0 = bad args */
bool  solution_set_rate(uint8_t bottle, uint8_t speed, float ml_s);  /* persists to /lfs/cal/bottle.json; 0 = back to default; false = bad args */
/* JSON: {"speeds":[100,80,60],"rates":[[full,med,low],[..],[..]],
 *        "defaults":[full,med,low],"enabled":b} (bottle 1..3 x speed 100/80/60) */
int   solution_cal_json(char *buf, size_t len);

/* ── Schedule entry helpers ── */
void  solution_cfg_from_entry(const schedule_entry_t *e, solution_cfg_t *cfg);
/* Clamp/normalize the solution fields of an entry in place (used by the
 * schedule parser). Zeros are left as zeros = "default". */
void  solution_entry_normalize(schedule_entry_t *e);
/* Estimated mL for one run of est_run_min minutes with this recipe from
 * this bottle. Negative when the bottle/speed has no calibration. */
float solution_estimate_ml(const solution_cfg_t *cfg, uint8_t bottle, int est_run_min);
/* Append the entry's solution fields as JSON members (leading comma included). */
int   solution_entry_json(const schedule_entry_t *e, char *buf, size_t len);

/* ── Run lifecycle ── */
void  solution_arm_entry(const schedule_entry_t *e);   /* NULL / disabled = no dose */
void  solution_arm_manual(uint8_t bottle, const solution_cfg_t *cfg);
void  solution_note_flow(void);
void  solution_on_run_end(void);

/* ── Status ── */
bool     solution_armed(void);      /* this run will / is dosing */
bool     solution_pumping(void);    /* pump currently on for a dose */
uint8_t  solution_bottle(void);     /* bottle chosen for this run, 0 = none */
uint32_t solution_pump_seconds(void);   /* accumulated pump-on time this run */
/* JSON object: {"enabled":b,"armed":b,"phase":"idle|waiting|delay|dosing|done","bottle":n,
 *               "speed":n,"pulse":b,"pump_s":n,"est_ml":x} */
int   solution_status_json(char *buf, size_t len);
