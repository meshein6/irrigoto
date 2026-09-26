# Fix: the stream over- and undershoots when the throw distance changes

**Branch:** `fix/stream-settling-overshoot` · **Type:** fix · **Status:**
FIXED in b543 and confirmed on hardware. Observed 2026-09-25: on a distance
change the stream visibly overshoots or undershoots and takes roughly half a
second to settle to the new distance.

## Cause (confirmed)

Candidate 3 below, essentially: closed-loop pressure seeking rather than
trusting a precalculated angle. `water_hold_pressure()` runs a feedforward to
the calibrated angle and then corrects that angle against measured pressure
up to `WATER_PRESSURE_ITER` (8) times with a 500 ms settle after each nudge.
That settling is the overshoot, and it runs at every distance change.

The owner identified this from the symptom before the code was read --
"is it trying to do a closed loop pressure adjustment vs saying x actuator
opening IS the target for this pass" -- which is exactly what it was doing.

## Fix

Gated on **Regulated water supply**: `water_hold_pressure_ex(..., correct)`
always does the feedforward, and only runs the correction loop when asked.
The two watering call sites pass `!s_supply_regulated`; the five calibration
call sites always correct, since measuring is the point there. On a well or
pressure tank the loop still runs, which is where it earns its keep.

Confirmed by the owner on hardware after b543: the overshoot is gone.

## Still open

Whether throws land accurately over a whole run without the correction. The
loop was also compensating for any error in the pressure calibration, so a
zone whose cal is off will now show it. The tell is throws landing
consistently short or long; the fix for that is to recalibrate pressure, not
to re-enable the hunt.

## Original triage notes (kept for the record)

## What is actually being observed

Throw distance is set by the valve angle, so "changing distance" means a
valve move — at every ring transition, and continuously in the modes that
feed the valve forward. Half a second of wrong distance at each of ~20 rings
per pass is a lot of water in the wrong place, and it lands *outside* the
intended ring, which the depth accounting will not notice.

## Candidate causes, cheapest to check first

1. **Hydraulic settling, not control error.** The existing supply check
   already waits `vTaskDelay(1500)` after opening the valve "settle after
   valve open", so the system is known to need time to stabilise after a
   valve move. If this is it, the fix is to hold the sweep until pressure is
   stable rather than to touch the valve control.
2. **Valve position overshoot.** `valve_goto` drives to an angle with its own
   stop behaviour; if it overshoots mechanically and corrects, the stream
   follows. Check `s_valve_last_dir` and the backlash compensation — there is
   existing lash characterisation (`exp_lash_run`) that may already quantify
   this.
3. **Feed-forward fighting the correction.** Serpentine feeds the valve
   forward from the supply trend (`Serpentine ff r%d: rho ... -> valve`).
   If the feed-forward and the per-ring correction disagree for the first
   moments of a ring, that reads exactly as over- then under-shoot.
4. **Pressure-derived estimate lagging.** The per-ring valve angle is
   computed from `pressure_scale` and the cal table; if the reading used is
   stale by a sample or two the first part of the ring is aimed with the
   previous ring's pressure.

## How to tell them apart

The run log already records what is needed, and the detail log records more:

- If **pressure** is still moving while the stream is wrong, it is (1).
- If pressure is flat but the **valve angle** is still moving, it is (2).
- If both are settled and only the *commanded* angle changes, it is (3) or (4).

`/zone/last_log` plus the pressure trace (armed for smooth/gentle/serpentine)
should separate these without any new instrumentation. Do this before
changing any control code.

## Note

Do not tune the valve PID as a first move. Three separate mechanisms above
produce the same visible symptom, and the codebase has a history of exactly
this failure mode — a plausible cause acted on without evidence, which then
has to be reverted (see b535/b536 supply-regulated in the git log).
