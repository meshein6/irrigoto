# Delivering the target depth when the sweep cannot go slow enough

**Branch:** `feature/slow-sweep-floor` · **Type:** feature · **Status:**
proposed, nothing built. Written out of the b556-b560 coverage/passes work,
which made the problem measurable for the first time.

## The problem, with numbers

Deposit on a ring is

```
depth_per_pass = Q(psi) / (sweep_speed x ring_area)
```

so to lay down more water in one pass the sweep has to go slower. Below some
speed the nozzle motor stalls rather than crawling, and that floor is a hard
physical limit. When the target needs a slower sweep than the floor allows,
the run silently misses.

On the measured zone (20 rings, 237 deg arc) a 1/8" target in one pass wants
**2.7-5.7 deg/s**. The floor is about **10.9 deg/s**. So the run is asking for
a sweep 2-4x slower than the mechanism can sustain.

The unit's own run record agrees, and did before any of this was modelled:

```
target_depth_mm   3.175
actual_avg_depth_mm 1.53      <- 48% of target
```

Nothing anywhere reported that. The run logged "completed".

## What the code does today, per mode

This matters, because the three paths behave differently and only one of them
is actually correct:

| Path | Slow clamp | What happens when the target needs a slower sweep |
| --- | --- | --- |
| **Pulse** (shared ring loop) | **exempt** — `if (!use_pulse_mode && nozzle_dps < min_dps)` | Correct. `nozzle_sweep_pulse` advances a fixed step then dwells, and `dwell_ms = step_ms_ideal - (pulse_ms + SETTLE_MS)` simply grows. Effective speed can go arbitrarily low because it is not the motor's speed. |
| **Gentle / Smooth** (shared ring loop) | clamped at `min_continuous_dps` | Under-delivers. Both force a continuous sweep by design, so the clamp is honest — but the deficit is invisible. |
| **Serpentine / Sections** (`serpentine_ring_dps`) | clamped at `min_continuous_dps`, **which is 0.00 on this unit** | Worst case. No effective clamp, so the planner commands a sub-stall speed and the glide engine tries to execute it. |

The last real run was Sections, so it was taking the third path.

**Pulse already solved this problem.** Everything below is about giving the
other modes the same escape.

## Proposals, cheapest first

### 1. Give `min_continuous_dps` a real value (one line, do this first)

`/lfs/cal/speed.json` on this unit:

```json
"min_continuous_dps": 0.00,
"deg_per_sec": [10.25, 12.80, 23.29, ...]
```

The floor is zero while the slowest *measured* point in the same file is 10.25
deg/s. One of the two writers sets it properly
(`map.min_continuous_dps = map.deg_per_sec[0]`, irrigoto.c ~4810) and the file
was evidently written by the other path.

Fall back to `deg_per_sec[0]` whenever the stored floor is <= 0, at load time,
so every consumer gets a sane number without a recalibration. This does not
improve anything on its own — it stops Serpentine and Sections commanding
speeds the motor cannot hold, converting a stall into an honest, reported
under-delivery.

`/api/run_plan` already reports `speed_floor_cal: false` for exactly this case.

### 2. Calibrate the pulse step so pulse-and-dwell is smooth

`jog_pulse_duty` and `jog_pulse_ms` are both **0**, so `nozzle_sweep_pulse`
falls back to duty 200 for 80 ms — about **3.9 deg per pulse**, which at a 3 m
throw is a ~200 mm jump between wetted spots. That coarseness is the reason
pulse reads as "accurate but stationary" in the code comments, and it is also
what made Pulse overshoot every arc bound until b550.

The same file records `jog_deg_per_pulse: 0.1801`, so a ~0.2 deg step is
achievable on this hardware — the parameters that produce it just are not
stored. `phase_jog_pulse_cal()` (irrigoto.c ~4427) exists to measure them.

Run it, persist duty/ms, and a pulse step becomes small enough that
pulse-and-dwell is visually continuous. This is the enabler for (3).

### 3. Drop below the floor by dwelling, not by slowing the motor

With (2) in place, extend the escape Pulse already has to the modes that
currently clamp:

- **Serpentine / Sections:** where a ring's solved dps is below the floor,
  plan that ring as pulse-and-dwell instead of a continuous glide. The leg
  list already carries a per-leg dps, so this is a per-ring decision, not a
  per-run mode change. The boundary-hug connectors stay continuous — they are
  travel, not deposition.
- **Gentle / Smooth:** offer it rather than imposing it. Both exist
  specifically to keep the nozzle moving (seed beds, bare soil), so
  stop-and-go may defeat the point. A per-run choice, defaulted off until a
  wet test says otherwise.

This removes the under-delivery at its cause instead of working around it.

**Risk, stated plainly:** during a dwell the nozzle is stationary and water
lands on one spot. Whether that reads as even application depends on the spray
fan width against the pulse step — which is why (2) comes first. Needs a wet
test with a heatmap before it becomes any mode's default.

### 4. Fix the footprint model — and let coverage do real work

This is a correction to what b556-b560 actually implemented, and it is
probably the largest single win.

The plan doc said to scale the ring footprint with the ring pitch, and that is
what shipped: `s_ring_footprint_mm` moves with `s_ring_pitch_mm`. That is the
right rule for the *coverage map* — it stops closer rings double-counting the
same ground.

It is the wrong rule for *depth*. Physically the nozzle lays a stream of some
fixed width W, set by the spray fan and the throw, not by how far apart we
choose to put the rings. If pitch < W the wetted bands genuinely overlap and
the depth genuinely adds. Tying the footprint to the pitch asserts the bands
never overlap, which means:

- Finest coverage halves the pitch and so roughly doubles the number of times
  a given patch of ground is passed over — but the model credits the same
  depth, so **coverage currently cannot help with the clamp at all**.
- The real behaviour is that Finest coverage at 1/8" should deliver close to
  2x the depth per pass, which is precisely the 2-4x shortfall measured above.

The fix is to measure W (it is a function of throw, and the heatmap CSV from a
single-ring run already contains the data to fit it), store it in the
calibration, and use W for depth while keeping the pitch-based band for the
coverage map. Then coverage becomes a genuine third lever alongside depth and
passes, and `/api/run_plan` can say "Finest coverage would reach this target in
one pass" instead of only ever suggesting more passes.

### 5. More passes (already shipped, works today)

`/api/run_plan` solves for the smallest pass count that clears the clamp — 3
for 1/8" on this zone. It costs run time and repeats every ring transition,
which is where travel waste and stream wander live, but it needs nothing new
and it reaches target today.

## Suggested order

1. (1) now — it is one line and it stops Sections commanding sub-stall speeds.
2. (5) is already available; use 3 passes at 1/8" meanwhile.
3. (2) next, because it is a calibration run and unblocks (3).
4. (4) is the one worth real design time — it makes coverage mean something.
5. (3) last, gated on a wet test.

## How to tell it worked

Same zone, 1/8" target, before and after: `actual_avg_depth_mm` in
`/zone/last_water` should approach `target_depth_mm` (3.175) instead of sitting
near 1.53, without the pass count having to rise. The heatmap should not
develop banding, which is the tell that a dwell step is too coarse.
