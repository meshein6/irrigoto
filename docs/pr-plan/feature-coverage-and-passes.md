# Coverage on the zone, depth + passes on the run, speed solved from both

**Branch:** `feature/coverage-and-passes` · **Type:** feature · **Status:**
BUILT in b556-b560, dry-verified on hardware; wet test outstanding, and
per-schedule-entry passes still to do (see "What is not done" at the end).
Owner design, 2026-09-26: *"I should get coverage settings in the
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


---

# Build notes (b556-b560)

## What landed

**Coverage, on the zone.** `zone_perimeter_t` carries a `coverage` byte
(0/1/2 = Standard/Fine/Finest); 0 is what every pre-existing zone reads as, so
nothing already saved changes. It persists in the zone JSON and through
`storage_zone_parse_json`, so an imported zone keeps it. `zone_load_nvs` now
zeroes before reading, which is what makes a shorter pre-b556 NVS blob leave
the new tail byte at Standard instead of stack garbage.

The scale reaches ring generation and the depth accounting through
`s_ring_pitch_mm` / `s_ring_footprint_mm`, threaded to **all four**
ring-generation sites and **all three** footprint-tolerance sites, so the two
cannot drift apart. `water_run_t.ring_footprint_mm` records what the run used
and `ring_covers()` reads it back, so a Finest run does not double-count its
ring overlap and report depth it never applied — and the heatmap stays correct
across a reboot, since it is persisted in the run JSON.

Picker sits in Zone Setup under the readout, with a one-line note per option.
Saved with the zone, which also drops the cached heatmap — correct here, since
changing ring pitch invalidates the old ring layout.

**Passes, on the run.** `s_web_water_passes`, consumed once by
`phase_water_zone` like the depth beside it, 0 = each mode's existing
behaviour. `per_pass_target = depth_mm / passes` feeds the speed solver in both
paths — the shared ring loop (which already computed `depth_mm / passes`, it
just had no way to be told) and `serpentine_build_pass_plan` for
Serpentine/Sections. Pulse, whose pass *is* its depth unit, takes the request as
its pass count outright.

**The bound, made visible.** `GET /api/run_plan?zone=&depth=&passes=` walks the
same ring ladder at the zone's coverage, asks the solver for each ring's speed,
and reports the clamp. The Water modal shows it live as the pickers move:
sweep speed and estimated minutes when the combination works, and when it does
not, which way it is wrong and what to change.

The advice **solves for** the smallest pass count that clears the clamp rather
than suggesting "double it" — on the measured zone a 1/8" target needs 3
passes, and doubling from 1 to 2 would still have been clamped.

## Two findings from building it

**This unit has no slow-speed floor.** `/lfs/cal/speed.json` has
`min_continuous_dps: 0.00`, so `serpentine_ring_dps` clamps *nothing* on the
slow side and the solver will command a sweep slower than the nozzle can
sustain — it stalls instead of crawling. `/api/run_plan` judges against a
conservative 10.9 dps, reports `speed_floor_cal: false`, and says so in the
advice rather than quietly implying the limit is measured.

**The zone is already under-delivering, and the model predicts it.** At 1/8"
in one pass every ring wants a sweep below the floor. The last real run's own
record agrees: `target_depth_mm 3.175`, `actual_avg_depth_mm 1.53` — 48% of
target. That is exactly the "too few passes" failure, and until now nothing
anywhere said so; the run just reported completion.

Measured plan output (zone 0, 237 deg arc, 20 rings, Standard coverage):

```
depth  passes  per_pass   dps range      clamp  suggest  est_min
1/8    1       3.17 mm    10.9-10.9      slow   3        7.2
1/8    2       1.59 mm    10.9-11.4      slow   3       14.4
1/8    4       0.79 mm    10.9-22.8      none   3       20.8
2/8    8       0.79 mm    10.9-22.8      none   6       41.6
8/8    8       3.17 mm    10.9-10.9      slow   0       57.9
```

The 7.2 min estimate for 1/8" x1 sits against a measured 5.9 min for the last
real run, so the flow model is in the right place.

Note that run time does not fall as passes rise: the same water is being
applied either way. Passes change *how* it is applied — many light fast passes
against one slow heavy one — not how long it takes. Where the clamp bites,
more passes cost more time, because every pass then runs pinned at the floor.

## What is not done

- **Passes on schedule entries.** Only manual runs carry the parameter. The
  schedule blob needs a schema bump with an NVS migration, plus the HA package,
  and scheduled runs keep each mode's existing behaviour until then.
- **No wet test.** Everything above is dry: endpoint arithmetic, ring counts
  and persistence. Whether Fine/Finest actually improve uniformity, and whether
  the suggested pass count reaches target, needs a run and a heatmap.
- **The serpentine leg cap** (build order step 4) is untouched. b547 already
  warns loudly on truncation; finer coverage will make it fire sooner, and the
  measured log hit the cap at 20 rings on Standard.
- **Speed calibration should be re-run** on this unit before trusting the
  slow-side bound, since the floor it is being judged against is assumed.
