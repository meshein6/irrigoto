# Depth should mean a slower sweep, not more passes

**Branch:** `feature/depth-by-speed` · **Type:** feature · **Status:** planned.
Owner preference, 2026-09-25: *"reconsider if depth means slower and more
precise or more passes. I'd rather have slower and just more water that way."*

## How depth works today

Depth is eighths of an inch (`depth8`, 1..8) and becomes a millimetre target:

```c
depth_mm = depth8 * 3.175f;
```

What each mode then does with it differs, and none of them slows down:

- **Pulse:** `passes = depth8`. A 1/2″ run is literally the 1/8″ pass four
  times over. Four full sweeps of the zone, four sets of ring transitions.
- **Gentle / Smooth / Serpentine / Sections:** adaptive multipass toward
  `depth_mm`, capped at 20 or 30, exiting when the rings meet target.

So more depth always means **more laps**, never a slower lap.

## Why slower is better here

The per-ring sweep speed already exists as the lever:
`serpentine_ring_dps(ring_throw, inner_throw, active_deg, depth_mm, ...)`
solves for degrees-per-second from the deposit target — so the machinery to
water more deeply in a *single* slower pass is already there for serpentine;
it is just bounded by what one pass is allowed to deposit.

A slower single pass beats N fast passes for this hardware:

- Every extra pass repeats the ring transitions, and those are where the
  waste and the wandering live (see the Sections work — a pass over a waisted
  zone spent 79 m of travel to lay 113 m of water).
- Ring transitions are also where the over/undershoot happens (see
  `fix-stream-settling-overshoot`), so fewer of them is less spray landing
  in the wrong place.
- Soil takes water better applied slowly than in repeated bursts, up to the
  infiltration rate.

## Change

- Add a per-run **Application** choice: *More passes* (today's behaviour) or
  *Slower, fewer passes* (default to the latter once validated).
- For the slower mode, drive `depth_mm` into the dps solver for the whole
  target rather than per-pass, and clamp to the slowest reliable nozzle speed
  from the speed map (`min_continuous_dps`) — below that the nozzle stalls
  rather than crawls, which is a hard floor, not a preference.
- When the target needs more than the floor allows, fall back to the minimum
  number of passes that does fit, rather than silently under-watering.
- Pulse is the odd one out: its pass *is* the depth unit. Either leave it on
  the old behaviour or exclude it from the choice.

## Risks

- Runoff. A slow pass past the soil's infiltration rate puddles instead of
  soaking, which is the exact failure the multipass design was avoiding. Needs
  a wet test at 1/2″ and 1″ before this becomes the default.
- The depth estimator and the solution-dosing mL estimate both key off run
  length; a slower single pass changes total run time and therefore the
  estimated dose. Check `schedule_estimate_duration_min` and
  `solution_estimate_ml` together.

## Testing

Same zone, same target depth, both settings: compare total run time, measured
depth from the heatmap, and coverage score. Watch for pooling at the inner
rings, which is where the sweep is slowest already.
