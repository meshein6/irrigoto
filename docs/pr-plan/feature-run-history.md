# Per-run history log file + History card

**Branch:** `feature/run-history` · **Type:** feature · **Status:** built, merged into `combined` and flashed to hardware (b535).
storage_log() and its unused daily rotation were removed, as the doc
proposed.

## Summary

Append one line per watering run to `/lfs/logs/runs.csv`. The line holds
start/end time, zone, mode, depth, outcome, volume, supply pressure and (with
solution dosing) the bottle and mL applied. Show the last runs on the landing
page and make the file downloadable.

## Problem

- `storage_init()` creates `/lfs/logs`, and `storage_log(line)` in `storage.c`
  appends to `/lfs/logs/YYYYMMDD.log` with 14-file rotation, **but nothing
  calls `storage_log()`**. The folder is always empty, which is confusing on
  the `/fs` page.
- Run history only exists for the **last** run:
  - `last_water_save_nvs()` stores one `last_water_meta_t` in NVS
  - `/lfs/water/last_log.txt` holds the last run's debug log
  - both are overwritten each run

## Change

At run completion, next to the `last_water_save_nvs()` call, and on every
exit path including cancel/fault, append one row to `/lfs/logs/runs.csv`.
Almost every field already exists in `last_water_meta_t` (the HA completion
event's payload):

| Column | Source |
|---|---|
| `start_local`, `end_local` (ISO 8601) | run-start epoch (new: captured at start); `finish_epoch` |
| `uptime_s` | used when the clock isn't set (then `start_local` is blank, not 1970) |
| `duration_min` | `duration_s` |
| `zone_id`, `zone_name` | `zone_id` + zone file |
| `trigger` | manual web / schedule / HA (new: set where the run is started) |
| `mode`, `depth_in` | mode code → label; `target_depth_mm` → eighths |
| `status` | completed / cancelled / no_supply / water_loss / fault |
| `passes`, `rings`, `rings_supply_limited` | pass counter, `num_rings`, `rings_supply_limited` |
| `volume_l`, `avg_depth_mm`, `coverage_pct`, `score` | existing getters |
| `supply_psi_min/avg/max` | existing |
| `supply_regulated` | if `feature/supply-regulated` is present |
| `bottle`, `speed`, `solution_ml` | if solution dosing is present: bottle, speed, and the estimated mL (calibrated rate × `solution_pump_seconds()`); blank when not dosed |
| `fw_build` | `FW_BUILD` |

- Write the header when the file is created. One `fopen("ab")` per run; runs
  are rare, so flash wear isn't a concern.
- Size cap about 128 KB (~600 rows): rotate to `runs.1.csv`, keeping one old
  file. Check free space first (there's already a guard in `storage.c`).
- Either put `storage_log()` to use for a few system events (boot, OTA, faults,
  schedule changes) or remove it and the unused daily rotation.

**UI and API:**
- `GET /api/runs?limit=20` reads from the end of the file.
- A **History** card on the landing page: date, zone, mode/depth, duration,
  volume, solution mL, status, plus "Download CSV".
- The file also shows under `logs/` on `/fs`.

## Testing

Completed, cancelled and no-supply runs each add a row. With the clock unset,
rows are still ordered by uptime. Rotation at the cap works. `/api/runs`
returns the newest first.
