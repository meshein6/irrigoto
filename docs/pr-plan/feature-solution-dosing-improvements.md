# Solution dosing improvements: calibration file, default rates, enable toggle

**Branch:** `feature/solution-dosing-improvements` (cut from
`feature/solution-dosing`, not from `main`) · **Type:** feature · **Status:**
planned. Could also be folded into `feature/solution-dosing` before that goes
upstream.

## Summary

Three improvements to solution dosing that make it fit the rest of the
firmware and stay out of the way on units without pumps:

1. Store the bottle calibration as a file, like every other calibration.
2. Ship sensible default rates, so the estimate works before calibrating.
3. Add a device setting that turns the whole feature on or off.

## 1. Calibration as a file

**Now:** rates are an NVS blob. `solution_init()` loads the key
`NVS_KEY_CAL`, and `solution_set_rate()` stores it (`solution.c`). Every other
calibration is JSON on LittleFS: `/lfs/cal/pressure.json` and
`/lfs/cal/speed.json` (see `storage.h`).

**Change:**
- Add `/lfs/cal/bottle.json` using the same shape `solution_cal_json()`
  already emits: `{"speeds":[100,80,60],"rates":[[full,med,low],[…],[…]]}`.
- Add `storage_bottle_save()` / `storage_bottle_load()` in `storage.c`, written
  like `storage_spd_save/load` (same `fopen` error logging, same `json_get_*`
  helpers).
- `solution_init()` reads the file. If it's missing and the NVS blob exists,
  write the file once from NVS, then erase the NVS key. The per-entry run
  counters (`NVS_KEY_RT`) stay in NVS; they're runtime state, not
  calibration.
- Update the file-layout comment at the top of `storage.h`/`storage.c`.
- Result: the calibration shows on the `/fs` Filesystem page, can be
  downloaded and backed up, and survives the same things the other cal files do.

## 2. Default rates

**Now:** an uncalibrated rate is 0, so the page shows "calibrate bottle N" and
no estimate (`landing.html`, `schedule.html`).

**Change:** built-in defaults for every bottle, used when the file is missing:

| Speed | Rate |
|---|---|
| Full (100 %) | 0.30 mL/s |
| Medium (80 %) | 0.23 mL/s |
| Low (60 %) | 0.17 mL/s |

- Put them in a `static const` table in `solution.c`, not in the HTML.
- The `/bottle_cal` fields show the loaded values, so they're never blank.
- Optional: a "Reset to defaults" button on `/bottle_cal`.

## 3. "Bottles" enable toggle

**Now:** the Bottle cal link, the Water modal's Apply solution block and the
schedule entries' Apply solution block always show, even on units with no
pumps.

**Change:**
- A **Bottles** switch in the landing page's Device card, styled like the
  Theme switch. It's saved in NVS (e.g. key `bottles_en`) and **defaults to
  off**.
- An API endpoint `GET/POST /api/bottles?on=0|1`, modelled on
  `/api/detail_log`. The state is also included in the status JSON / `/api/all`.
- **When off:**
  - The Bottle cal link and both Apply solution blocks are hidden.
  - The firmware ignores `solution_*` on `POST /zone/water` and doesn't arm
    doses from schedule entries.
  - Existing entry settings are **kept**, not cleared, so turning it back on
    restores them.
- ⓘ text: "Turn on if your unit has the three solution pumps and bottles
  fitted. Adds Apply solution to manual runs and schedule entries and the
  Bottle calibration page."

## Testing

- Fresh flash (no file): defaults appear on `/bottle_cal`, estimates work, and
  `/fs` shows `cal/bottle.json` after the first save.
- Upgrade from NVS rates: values carry over into the file, and the NVS key is gone.
- Toggle off: no bottle UI anywhere; a manual run with `solution_*` params
  doesn't pump; a scheduled entry with dosing enabled doesn't pump.
- Toggle on: everything is back with the previous settings.
