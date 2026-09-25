#pragma once
/*
 * pump.h -- peristaltic solution pump driver (OtO board, three pumps).
 *
 * Hardware (owner-probed 2026-09-20, see README.md "Pump hardware"):
 *   GPIO16  drive, active-high, needs the 9 V motor rail. PWM at 20 kHz only;
 *           lower frequencies stall under roller load.
 *   GPIO21  selects pump 2, GPIO19 selects pump 3; neither = pump 1. The
 *           selector is raised ~20 ms BEFORE the drive and held for the run.
 *   Speed is a PWM duty in percent. 60 % is the measured floor under load;
 *   below that the motor twitches or stalls. 100 % drives the pin as a plain
 *   level (no LEDC).
 *
 * One pump at a time by construction (they share the drive line). pump_start()
 * on a running pump kills it and takes over -- callers never have to stop
 * first. pump_stop() is idempotent and is the single "everything off" path:
 * the host wires it into its motor-rail-off so a rail drop can never leave the
 * drive line asserted.
 *
 * The driver never touches the rail or the ADC directly; the host supplies
 * those through pump_hal_t so this file stays portable across upstream
 * snapshots.
 */
#include <stdint.h>
#include <stdbool.h>

#define PUMP_COUNT      3
#define PUMP_SPEED_MIN  60
#define PUMP_SPEED_MAX  100

typedef struct {
    void     (*rail_on)(void);     /* 9 V motor rail up (may block ~100 ms) */
    void     (*rail_off)(void);    /* 9 V motor rail down                    */
    bool     (*rail_is_on)(void);
    uint32_t (*pcur_mv)(void);     /* pump current-sense ADC, millivolts     */
} pump_hal_t;

void    pump_init(const pump_hal_t *hal);

/* Start pump 1..3 at speed 60..100 %. Clamps speed. Turns the rail on if it
 * was off and remembers that so pump_stop() gives it back. Returns false only
 * for a bad pump number. */
bool    pump_start(uint8_t pump, uint8_t speed_pct);

/* Drive low, selectors released, LEDC detached, rail released if this driver
 * raised it. Safe to call at any time, from any task. */
void    pump_stop(void);

bool    pump_running(void);
uint8_t pump_active(void);        /* 1..3 while running, else 0 */
uint8_t pump_speed(void);         /* current speed %, 0 when idle */
uint32_t pump_elapsed_ms(void);   /* since the current pump_start(), 0 when idle */

/* One PCur sample converted to mA (INA4180A3 chain: 142 mV zero, 0.2 mA/mV).
 * Single samples on a brushed motor swing +/-50 %; average in the caller. */
float   pump_read_ma(void);

/* Clamp helper shared by the UI/API layers so every path agrees. */
static inline uint8_t pump_clamp_speed(int pct)
{
    if (pct < PUMP_SPEED_MIN) return PUMP_SPEED_MIN;
    if (pct > PUMP_SPEED_MAX) return PUMP_SPEED_MAX;
    return (uint8_t)pct;
}
