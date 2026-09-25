# Fix: valve left open during the swing back at each ring change

**Branch:** `fix/pulse-ring-swing` · **Type:** fix · **Status:** planned.

## Summary

In the ring-by-ring modes, after finishing a ring the nozzle swings back to
the start of the arc for the next ring **with the valve still open at the
previous (farther) ring's setting**. The result is a burst of long-throw water
across the zone at the start of almost every row. The code comment at that spot
says the move happens with the valve closed, but no code closes it.

## Field observation

- Pulse 1/8″ run on a ~10 ft zone, on a unit with ~27.6 ft of full throw.
- After the start-of-run supply check, most rows began with about a second of
  near-full-strength stream before dropping to the ring's throw.
- **Serpentine showed no bursts** on the same zone. It alternates direction
  every ring and never swings back, which points at the swing-back.

## Cause (upstream build 527, `phase_water_zone()`)

- **Pulse** sweeps every ring clockwise: `cw = (smooth_mode || gentle_mode) ?
  (pass % 2 == 0) : true` (~line 12177). **Gentle and Smooth** use one
  direction for all rings in a pass. So in all three, the nozzle ends each ring
  at the far end of the arc.
- Rings go outer → inner. For the first arc of ring > 0, the "reposition"
  branch (~12697) calls `nozzle_goto(seg_origin)` if the nozzle is more than
  6° ahead or 3° behind, **but doesn't close the valve first**. Its comment
  reads "Reposition under valve-closed".
- The valve is only closed between arcs of the *same* ring (`!is_last_arc`,
  ~12998, "Between segments: close the valve while the nozzle transits").
- The valve is moved to the new ring's target only **after** the swing
  ("Open valve to ring target", ~12729). So the whole swing sprays at the
  previous ring's throw.

## Change

The minimal fix:
- In the ring-change reposition branch, close the valve (`valve_goto_ex(
  VALVE_CLOSED_DEG, …, -1)`, like the between-segments close) before
  `nozzle_goto(seg_origin)`. The existing "Open valve to ring target" step
  then reopens it at the new ring's setting.
- The inner-ring path (`water_seat_valve_from_closed`) already seats from
  closed, so it's unaffected.

Alternative, larger change (maintainer's call):
- Alternate sweep direction every ring in Pulse (like Serpentine) so there's no
  swing back at all, and the time is saved as well.
- This changes the watering pattern and interacts with the CW-arc 7° inertia
  pre-compensation (`seg_deg -= 7.0f` for CW arcs), which would need a CCW
  equivalent. Better as a follow-up.

## Side effect worth checking

Closing and reopening the valve at every ring adds a closed→open transition.
On a well/tank supply, line pressure builds up while the valve is shut, so the
first moment after reopening may still throw a little long. If testing shows
that, a second step would be: move the valve to the new ring's setting *during*
the swing instead of closing it, or ramp the reopen.

## Testing

- Dry rehearsal: log shows the valve closing before each ring-change
  reposition.
- Wet run, Pulse 1/8″, small zone: no long-throw burst at row starts. Compare
  the `last_log` ring timings before and after.
- Smooth and Gentle: same check, since they share the reposition branch.
