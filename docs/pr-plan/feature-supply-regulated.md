# "Supply regulated" setting: skip the start-of-run pressure check

**Branch:** `feature/supply-regulated` · **Type:** feature · **Status:** built, merged into `combined` and flashed to hardware (b535).

## Summary

Add one device setting, **Supply regulated** (default **off**). When it's on,
watering runs skip the ~12 s full-open supply pressure check at the start and
trust the pressure calibration. Regulated supplies (city mains, or anything
behind a pressure regulator) don't vary meaningfully from run to run, so the
check adds nothing and costs 12 s of full-throw spray.

## Problem

- Every run starts with the check in `phase_water_zone()` (~11648–11810,
  upstream b361/b363/b365). It aims the nozzle at the zone's deepest bearing,
  opens the valve **fully**, swings ±20° (capped at 25 % of the arc) and takes
  120 pressure samples.
- On a small zone this sprays far past the edge. Example: a zone reaching
  ~10 ft on a unit with ~27.6 ft full throw gets ~17 ft of overspray for 12 s.
- The check exists for **well pump + pressure tank** supplies, which cycle
  (e.g. 45 → 60 PSI), so a fresh measurement matters there.

## What the check feeds today

- `pressure_scale = clamp(live_psi / psi_max, 0.5, 1.5)`, used for:
  - the time estimate (`scale_adj`) and the per-ring PSI estimates
  - **Smooth pass 0 only**: `valve_ring_throw = ring_throw / pressure_scale`.
    Later passes use the per-ring measured correction instead.
  - **Serpentine: not used** for the valve since upstream b428 (a full-open
    reading overstates well/tank sag, which over-opened rings 1.4–2.3×).
- **No-supply abort** (upstream b466): if the 12 s peak stays below
  `WATER_NO_SUPPLY_PSI`, the run aborts as `WATER_STATUS_NO_SUPPLY`, which HA
  reports as MISSED.

## Change

**Firmware, when the setting is on:**
- Skip the nozzle pre-aim and the whole check block (add
  `&& !s_supply_regulated` to the existing `if (!demo_mode && !serpentine_dry
  && …)`).
- `pressure_scale = 1.0`: Smooth's first pass uses the calibration as-is (later
  passes still self-correct), and the estimate uses calibrated flow (the
  history seed still wins when present).
- **Move no-supply detection.** Without the check, a run with the water off
  would skip ring after ring. The first-arc low reading counts as "transient"
  because `s_water_run_had_flow` is still false, so the upstream b468
  water-loss streak never triggers, and the run could go dry for a long time.
  New rule: if no flow has been seen yet and ring 1's first-arc reading stays
  below `WATER_MIN_FLOW_PSI` for ~10 s (one retry), set
  `WATER_STATUS_NO_SUPPLY` and abort.
- Log `Supply check skipped (regulated)` in place of the `Supply:` line.

**Setting plumbing:**
- NVS key `supply_reg` (bool, default off), loaded at boot with the other
  settings.
- `GET/POST /api/supply_regulated?on=0|1`, modelled on `/api/detail_log`
  and `/api/auto_sleep`. Add `"supply_regulated"` to the status JSON and `/api/all`.
- Landing page Device card: a switch styled like Theme, with an ⓘ:

  > **Supply regulated.** Turn on if your water comes from city mains or goes
  > through a pressure regulator. Pressure then stays about the same from run to
  > run, so the sprinkler skips the 12-second full-power pressure check at the
  > start of each run (which can spray past the edge of small zones) and trusts
  > the pressure calibration. Leave off for a well pump or pressure tank, where
  > pressure rises and falls as the pump cycles. The check measures today's
  > supply so the run can adjust. If runs start landing short or long after
  > turning this on, recalibrate pressure or turn it back off.

- Optional: an HA switch entity.

**Optional follow-up:** with a regulated supply, Smooth's pump-aware scheduling
(peak hunting, closed-valve dwells) and Serpentine's supply-trend feed-forward
have nothing to track and could be bypassed for a more predictable order.

## Compatibility

Default off: well/tank users see no change. Demo and serpentine dry runs
already skip the check.

## Testing

- Regulated supply, setting on: no start-of-run spray; depth and throw
  comparable to a run with the check.
- Water turned off, setting on: the run aborts as NO_SUPPLY within ~10–15 s,
  and HA reports MISSED.
- Setting off: identical to today.
