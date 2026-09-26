# Coverage on the zone, depth + passes on the run, speed solved from both

**Branch:** `feature/coverage-and-passes` · **Type:** feature · **Status:**
planned. Owner design, 2026-09-26: *"I should get coverage settings in the
zone config and then for an actual run I should be able to control the depth
and number of passes which calculates a speed for you. Make sure to bound
these two variables to avoid creating runs that are too fast."*

## Why this shape is right

The two knobs belong in different places, and the current code has neither:

- **Coverage** (how close the rings are) is a property of the **zone** — its
  size, its shape, where the sprinkler stands. It does not change run to run.
- **Depth and passes** are properties of the **run**. They are what you decide
  when you water.
- **Speed is not a knob at all.** It is the consequence of the other three,
  and the firmware already solves for it.

This supersedes the abandoned per-zone `ring_order` (see
`feature-ring-order.md`), which put a run property on the zone.

## What already exists

`serpentine_ring_dps(ring_throw, inner_throw, active_deg, per_pass_depth,
spd, have_spd)` solves degrees-per-second from the deposit target:

```c
Q_ref = NOZZLE_FLOW_K * powf(ref_psi, NOZZLE_FLOW_N);
dps   = Q_ref * active_deg / (per_pass_depth * 60000.0f * ring_area);
if (dps < min_dps) dps = min_dps;        // spd->min_continuous_dps
if (dps > max_dps) dps = max_dps;        // last point of the speed table
```

So the solver, and the bounds, are already there and already measured — this
unit has a `/lfs/cal/speed.json`. Three things are missing: the user cannot
choose the pass count, the coverage is a fixed constant, and **when the clamp
fires nothing says so.**

## The bound, stated properly

The clamp is the whole safety story, and it fails in both directions:

- **dps hits `max_dps`** — the sweep cannot go fast enough to deposit as
  little as asked. The pass lays down MORE than `depth / passes`, so the run
  overshoots the target. This is the "too fast" case: too many passes for
  too little depth.
- **dps hits `min_dps`** — the sweep cannot go slow enough to deposit as much
  as asked. The pass lays down LESS, and the target is never met; worse, near
  the floor the nozzle stalls rather than crawling. This is too few passes for
  too much depth, and it is also where runoff lives.

Neither is currently visible. A run just quietly misses its target.

## Change

### 1. Zone config — Coverage

New per-zone field `coverage` in the zone JSON (`storage_zone_load/_save/
_parse_json`, plus a field on `zone_perimeter_t`). Missing reads as Standard,
so existing zones are unchanged.

Ring pitch is `WATER_RING_SPACING (700 mm) × (throw / act_max_throw)`, floored
at `WATER_MIN_RING_SPACING` (80 mm). Coverage scales that constant:

| Setting | Scale | Rings on a 3.3 m zone |
| --- | --- | --- |
| Standard | ×1.00 | ~18 |
| Fine | ×0.75 | ~23 |
| Finest | ×0.50 | ~29 |

All fit under `WATER_MAX_RINGS_CAL` (36).

**Scale it in both places the constant is used**, not just ring generation:
the same value is the assumed footprint width for depth accounting
(`ring_covers()`, the ±spacing/2 bands at lines ~9006, ~19824, ~19973).
Closer rings with an unchanged footprint would double-count the overlap and
report depth that was never applied.

**Serpentine and Sections have a hard limit here.** `SERPENTINE_MAX_LEGS` is
256 and the measured log already hit `leg cap (256) hit -- truncating` at
20 rings — the outer third of that zone was never planned. Finer coverage
makes that worse. The cap cannot simply be raised: `s_serpentine_legs` is
already 8 KB and DRAM is at 95.7 %. So either restrict Fine/Finest to the
non-serpentine modes, or log the truncation loudly instead of silently
watering part of the zone.

### 2. Run config — Depth and Passes

Depth already exists (eighths, 1..8). Add **Passes** to the Water modal and
to each schedule entry, and derive:

```
per_pass_depth = (depth8 * 3.175) / passes
```

Feed that to the existing solver instead of the current implicit per-pass
value. Pulse keeps its own meaning (its pass IS the depth unit).

### 3. Bound it where the user can see it

Compute the required dps for the chosen depth and passes and check it against
the speed map BEFORE the run starts:

- Show the resulting sweep speed and the estimated run time as the pickers
  move — the same live-feedback pattern the solution mL estimate already uses.
- When the combination would clamp, say which way and what to change:
  *"Too many passes for this depth — the sprinkler cannot sweep fast enough,
  so each pass would apply more than intended. Try 3 passes."*
- Offer the valid pass range for the chosen depth rather than letting the
  user find the edge by trial.

A small endpoint (`GET /api/run_plan?zone=&depth=&passes=`) returning the
per-ring dps, the clamp state and the estimated minutes keeps the arithmetic
in the firmware, where the pressure cal and speed map already live, instead
of duplicating the flow model in JavaScript.

## Build order

1. Zone coverage — self-contained: field, storage, Zone Setup picker, scale
   applied to generation and footprint together.
2. `/api/run_plan` — the solver made queryable, no behaviour change.
3. Passes in the Water modal and schedule entries, driven by (2).
4. The serpentine leg-cap decision, once (1) shows how bad it gets.

## Testing

- A zone at each coverage setting: ring count in the log matches the table,
  and reported depth stays consistent (the footprint scaling is right if
  depth does NOT jump when only coverage changes).
- Depth 1/2" at 1, 2, 4 and 8 passes: run time should scale roughly
  inversely with pass count until a clamp fires, and the UI should refuse or
  warn at exactly the point the firmware would clamp.
- A deliberately impossible combination in each direction, confirming the
  message names the right fix.
