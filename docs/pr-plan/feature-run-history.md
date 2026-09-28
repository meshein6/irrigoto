# Per-run history log file + History card

**Branch:** `feature/run-history` · **Type:** feature · **Status:** planned.

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

---

# Upcoming runs

The History card says what *happened*. The Schedule card says what is
*configured*. Neither answers **what is about to run** — the question asked
before going away for a weekend, or right after setting a rain delay.
`next_run` gives one answer and not the shape of the week.

So the same card, mirrored: the next five scheduled runs above History, in the
same row geometry, because the two are read together — *it watered at 6, it
waters again at 6*.

## The device expands the list

`/api/upcoming?n=5` returns it already expanded. Two of the three rules it has
to get right live on the device and nowhere else:

- **the day-mask walk across a DST boundary** — the fire time is recomputed
  and its day-of-week re-checked, because a DST shift can reorder them;
- **the rain delay**, which suppresses everything before `delay_until`.

Re-deriving those in JavaScript would be a third copy of rules that have
already drifted once. The page formats; it does not decide.

## One schedule walk, one timezone derivation

`irrigoto_schedule_next_run()` and `schedule_next_run_full()` each carried
their own copy of the 15-day day-mask walk, and this needed a third. Three
copies of a walk that has to agree with itself about DST is three chances to
disagree, so it is now one function — `sched_entry_next_fire(entry, after)` —
and the callers differ only in what they keep. The rain-delay floor is
likewise one function, so every caller sees the same effective "next fire".

The local-time offset had the same problem: it was computed inline inside
`api_schedule_handler`. `sched_tz_offset_min()` is now shared.

The list walks the schedule with a per-entry cursor, repeatedly taking the
earliest next fire and advancing that entry alone, so several entries
interleave correctly.

## Empty is three different answers

An empty list can mean the clock has not synced, there are no enabled entries,
or a delay covers the whole horizon. The endpoint reports `clock` and
`entries` so the card can say which. Without a clock it also reports
`delay_until: 0` — the stored value cannot be compared against anything, and
sending it rendered a rain-delay banner dated in the past.

## What this branch cannot answer yet

- **The bottle column.** Whether a given future run gets solution, and from
  which, comes from the per-entry rotation counters in `solution.c`, which is
  not on this branch. `bottle` is emitted as `0` so the wire format and the
  page are already final; `feature/solution-dosing` fills it in with
  `solution_forecast_bottle(e, W->seen[best])` — the call site is marked.
- **The times are libc's.** This uses `localtime_r`, as the rest of the
  schedule code on this branch does. `fix/timezone` replaces those calls
  because libc applies a stale numeric zone while labelling it correctly;
  until that merges, these times are wrong by the same amount as every other
  schedule timestamp here, which is at least consistent.

## A note on stack, and on DRAM

The first version of this handler put a `schedule_t` (1028 B), a
`zone_perimeter_t` (728 B) and a 1600-byte response buffer on the stack, then
called `schedule_estimate_duration_min()`, which puts a 1768-byte
`water_run_t` on top — about 5.5 KB against the 8 KB httpd task stack. The
first request took the device down hard enough that the bootloader rolled back
to the previous build. The big locals are on the heap.

This branch builds at **95.1% DRAM**, about 1.2 KB from the ceiling. That is
not this feature — the handler's working set is heap-allocated and its strings
are flash. It is that the branch predates b547, which frees 8.5 KB; whichever
order these merge in, b547 needs to land before much else does.
