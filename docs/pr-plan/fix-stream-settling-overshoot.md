# Fix: the stream over- and undershoots when the throw distance changes

**Branch:** `fix/stream-settling-overshoot` · **Type:** fix · **Status:**
planned, cause not yet identified. Observed 2026-09-25: on a distance change
the stream visibly overshoots or undershoots and takes roughly half a second
to settle to the new distance.

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
