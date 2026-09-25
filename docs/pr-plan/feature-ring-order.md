# Per-zone ring order: auto / one ring at a time / section by section

**Branch:** `feature/ring-order` · **Type:** feature · **Status:** planned.
Suggest an upstream issue first: it touches the core ring loop.

## Summary

Add a per-zone **Ring order** setting that controls the order the rings are
watered in, so a run can go smoothly one ring after the next, and finish one
side of a zone before crossing to the other instead of jumping back and forth.
The default, **Auto**, keeps today's behaviour exactly.

## Problem

- Rings are the same for every mode. `phase_water_zone()` builds one list
  before branching on mode: start at the zone's max throw and step inward by
  `WATER_RING_SPACING × (t / act_max_throw)` (min `WATER_MIN_RING_SPACING`),
  plus direct-valve and sprinkler-inside inner rings.
- Only the **order** differs, and the user can't control it:
  - Pulse and Gentle: outer → inner, in sequence.
  - Smooth: its scheduler picks by deficit and pump trend (upstream
    b296/b499), which jumps around.
  - Serpentine: out → in, then in → out, alternating by pass.
- Within a ring, every separate arc ("lobe") is swept before moving inward,
  with a valve-closed jump between lobes. For a rectangle with the sprinkler
  inside, the outer rings split into 2–4 lobes (far sides and corners), so
  **every outer ring jumps across the zone and back**.
- Zone files hold only `name`, `num_points` and the points (`storage.c`), so
  there's no per-zone setting to hang this on today.

## Change

**New per-zone field** `"ring_order"` in the zone JSON (read/write in
`storage_zone_load` / `storage_zone_save`, plus a field on `zone_perimeter_t`):

- `auto` (default): today's per-mode order.
- `sequential`: outer → inner, one ring after the next, **alternating
  direction every ring** so there's no swing back. This also removes the
  open-valve swing back to the arc start at each ring change.
- `sections`: finish one lobe before the next.
  - For each ring, the arcs come from `water_find_arcs()` (up to
    `WATER_MAX_ARCS_PER_RING`, 10° sectors).
  - Link ring *k*'s arcs to ring *k+1*'s arcs where their bearing ranges
    overlap. Going inward, separate lobes **merge** where a smaller ring's arc
    spans both, which forms a tree.
  - Visit the tree depth-first:
    1. Lobe A from its outer ring inward, snaking.
    2. When it reaches the merge ring, one dry hop to lobe B.
    3. Lobe B down to the same merge ring.
    4. Continue the merged rings as single sweeps to the centre.
  - Jumps per pass drop from (lobes − 1) × rings to (lobes − 1).
  - Start at the lobe nearest the nozzle. Run alternate passes in reverse so
    pass 2 starts where pass 1 ended.

**Per mode:**

| Mode | Effect of a non-auto order |
|---|---|
| Pulse / Gentle | Straightforward: replace the ring iteration order. |
| Smooth | Bypass the deficit/pump scheduler. Satisfied rings are still skipped on later passes, but pump-peak timing is lost (only matters on well/tank). |
| Serpentine | `serpentine_build_pass_plan()` emits legs in lobe-tree order, with wet boundary-hug turns inside a lobe and a dry hop only between lobes. |

**UI:** a Zone Setup picker, "Ring order: Auto / One ring at a time / Section by
section", with an ⓘ and saved with the zone. The path preview
(`feature/mode-path-preview`) draws it.

## Compatibility

Missing field = `auto`, so old zone files behave exactly as today.

## Testing

- Dry rehearsal on a rectangle with the sprinkler inside: count valve-closed
  jumps in the log for each setting.
- Wet run with `sections`: no crossing between sides until the rings merge.
  Depth/coverage comparable to `auto`.
