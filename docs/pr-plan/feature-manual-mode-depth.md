# Manual run: pick mode and depth separately, with mode tooltips

**Branch:** `feature/manual-mode-depth` · **Type:** feature · **Status:**
planned. Page-only change; no firmware changes.

## Summary

The device's Water Zone modal offers a fixed list of presets. This change
replaces them with two pickers, **Mode** and **Depth**, the same way schedule
entries and Home Assistant already work, and adds an ⓘ tooltip explaining each
mode. The tooltip also goes next to the Mode picker on schedule entries.

## Problem

- `landing.html` Water modal: presets 1/8″, 1/4″, 1/8″×2, Gentle 1/8″,
  Gentle 1/4″, Smooth 1/8″, Serpentine 1/8″, Demo, Chase. There's no way to run
  e.g. Smooth 1/2″ by hand.
- Schedule entries (`schedule.html`) and HA's quick-water card
  (`irrigoto_device.yaml`: `water_mode` + `water_depth` selects → `water_now`
  script) already pick Mode and Depth (1/8″–1″) separately.
- The firmware already supports it: `POST /zone/water` takes `mode=` plus an
  optional `depth=` in eighths (1..8).

## Change (`landing.html`)

- Replace the preset grid with:
  - **Mode:** Pulse · Gentle · Smooth · Serpentine · Demo · Chase
  - **Depth:** 1/8″ … 1″ (same labels as schedule.html's `DEPTH_LABELS`)
- Demo: hide Depth. Chase: hide Depth and show the existing duration slider
  (`chase-row`).
- Send the web digits the schedule and HA already use: Pulse=1, Gentle=5,
  Smooth=7, Serpentine=8, Demo=`d`, Chase=`c`, i.e.
  `id=…&mode=<digit>&depth=<1..8>` (no depth for Demo/Chase).
- Replace the hard-coded "~13 min / ~26 min" hints with the estimate for the
  chosen mode × depth, or drop them. (If solution dosing is present,
  `/api/solution_est` already returns `est_min`.)
- Remember the last mode/depth per browser (`localStorage`, in try/catch).
- If solution dosing is merged, keep its Apply solution block under the
  pickers. It stays hidden for Demo/Chase as it is now.

## Mode tooltips (landing modal and each schedule entry)

One shared text table, matching the README's "Watering modes" table:

- **Pulse:** steps ring by ring, outer to inner, stopping to fine-tune pressure
  at each step. Most precise placement, but stop-start and slower.
- **Gentle:** low pressure over many light passes. Safe for seed and bare soil.
- **Smooth:** continuous sweep, then re-waters only rings that are still short,
  timed to the pump cycle. Good default.
- **Serpentine:** one continuous back-and-forth glide that never stops.
  Smoothest, with no start-of-row bursts, but less precise at edges and near
  the sprinkler.
- **Chase:** play mode for dogs, 1–10 min. Not tracked as watering.
- **Demo:** max-speed sweep. Not tracked as watering.

Opens on tap (hover alone doesn't work on phones). Use a `button` with
`aria-describedby`.

## Unchanged

The three mode encodings (web digit, HA select index, schedule number) are
untouched. This only changes how the modal presents them, so none of the other
sites in `docs/adding_a_watering_mode.md` need edits.

## Testing

Each mode × a few depths starts with the right `mode`/`depth` (check
`last_log` "Web mode" and target depth). Demo and Chase behave as before. The
tooltip opens and closes on a phone.
