# "Regulated water supply": trust the pressure calibration during a run

**Branch:** `feature/supply-regulated` · **Type:** feature · **Status:** built, merged into `combined` and flashed to hardware (b535).

## Summary

Add one device setting, **Regulated water supply** (default **off**). When on,
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

  > **Regulated water supply.** Turn on if your water comes from city mains or goes
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

## b543: it also skips the per-ring pressure hunt

Shipped originally as "skip the 12 s check". A second, larger effect was
identified afterwards from the symptom "the stream over- and undershoots for
about half a second when the distance changes".

`water_hold_pressure()` moves the valve to the angle the calibration says
produces the target pressure (feedforward), and then corrects that angle
against measured pressure up to `WATER_PRESSURE_ITER` (8) times, waiting
500 ms after each nudge. That settling is the visible overshoot, and it
happens at every distance change, not just at run start.

On a regulated supply the calibrated angle is already right, so the loop is
chasing noise -- its tolerance is 0.15 PSI against a supply measured cycling
around 6.5 PSI. The correction is now gated: `water_hold_pressure_ex(...,
correct)` takes the feedforward always and the loop only when asked. The two
watering call sites (`phase_water_zone` ring 0, `water_cleanup_pass`) pass
`!s_supply_regulated`; the five calibration call sites always correct, since
measuring is the entire point there.

So the setting now means one coherent thing: **trust the pressure calibration
instead of measuring during the run.** Both effects follow from that.

Renamed to "Regulated water supply" at the same time. The NVS key
(`supply_reg`) and the endpoint (`/api/supply_regulated`) are unchanged, so
no stored state or integration breaks.
