# Fix: schedules and timestamps follow a timezone set on the device

**Branch:** `fix/timezone` · **Type:** fix · **Status:** built and confirmed
on hardware (as part of `combined`). **Cut from `feature/wifi-power-settings`**,
because the zone picker is a section of that branch's System settings modal.
Merge or upstream that branch first.

## Problem

1. **The zone can only be changed by reflashing.** Schedules are interpreted
   through libc's `TZ`, which the `on_boot` hook sets from the
   `device_posix_tz` substitution (default US Eastern). A unit anywhere else
   needs a custom build.
2. **Once a zone can be changed at runtime, `localtime_r` ignores it.**
   Reported 2026-09-26: *"The next run timestamp is still showing EST. Fix the
   timezone across the entire device."* A unit with `America/Los_Angeles`
   saved rendered every timestamp in Eastern time, three hours out, while
   `/api/system` looked correct:

   ```
   tz       : PST8PDT,M3.2.0/2,M11.1.0/2
   tz_name  : America/Los_Angeles
   tz_saved : true
   offset   : -240 min          <- EDT, should be -420 (PDT)
   ```

   The `tz` field was our copy of what was *saved*, not what libc was
   *using*, so the response could not tell "the zone never applied" from "the
   zone applied and the clock is wrong."

## Cause of (2), measured, not inferred

`localtime_r` on this build fills in the wall-clock fields using a stale
numeric zone while labelling them with the current one. With
`TZ=PST8PDT,M3.2.0/2,M11.1.0/2` in the environment:

| | value |
| --- | --- |
| `getenv("TZ")` | `PST8PDT,M3.2.0/2,M11.1.0/2` |
| `tm_isdst` from `localtime_r` | 1 |
| `strftime("%Z")` | `PDT` |
| offset via `localtime_r` − `gmtime_r` | **−240** (wrong) |
| offset via `mktime` round trip | **−420** (right) |

So `localtime_r` knows the zone is Pacific and says so, then applies the
Eastern offset. `mktime`, in the same binary, follows `TZ` correctly, which is
why scheduled runs fired at the right wall-clock time while the UI showed them
three hours late.

The decisive test: setting the zone to `UTC0` at runtime did **not** move
`localtime_r`'s answer either. The numeric rule it applies is frozen at
whatever the `on_boot` hook installed, and no `setenv` + `tzset` dislodges it.
`tz_guard()` could never catch this: it compares the TZ *string*, which was
right the whole time.

## Change

### Zone setting (System settings modal)

- A **Timezone** section in the System settings modal: 31 common zones, a
  custom POSIX TZ string, or back to the firmware default. If the phone's zone
  differs from the device's, the modal offers a one-tap **Use it**, and it
  shows the device's current local time and UTC offset.
- The saved zone is kept in NVS (`tz_posix`, plus the IANA name as a label in
  `tz_name`). `tz_init()`, called from `irrigoto_init()` right after the
  `on_boot` hook, applies it before the schedule task starts.
- Saving applies immediately, with no reboot. The armed scheduled run is a UTC
  epoch computed under the old zone, so it is disarmed and re-armed from the
  schedule table at the next sleep.
- **Firmware default** erases the saved keys and restores `device_posix_tz`.
- `tz_guard()`, polled from `esphome_idle_task`, puts the saved zone back and
  logs a warning if anything else rewrites `TZ`.
- `tz_posix_valid()` rejects junk (newlib would otherwise silently fall back
  to UTC); an invalid string gets a 400.
- The zone list's POSIX rules were checked against tzdata offsets for January
  and July.
- `/api/system`: `GET` adds `tz`, `tz_name`, `tz_default`, `tz_saved`, `now`,
  `tz_offset_min`; `POST tz=<POSIX>&tz_name=<IANA>` saves a zone (empty `tz`
  returns to the default).
- `esphome/irrigoto-core.yaml`: comment noting `device_posix_tz` is now only
  the default.

### Local time that follows the saved zone

- **`irrigoto_tz_offset_min_at(t)`**: minutes east of UTC at an instant,
  solved by fixed point against `mktime`. Guess the local fields, ask `mktime`
  which instant they name, correct by the error. Converges in two passes away
  from a DST transition and three at one, and bails out past ±14 h.
- **`irrigoto_localtime_r(t, out)`**: `localtime_r`'s contract exactly,
  implemented as `gmtime_r(t + offset)`. `tm_isdst` comes from libc, which
  gets that part right, so a struct handed back to `mktime` or formatted with
  `%Z` still behaves. Declared in `irrigoto_api.h`.
- Every local-time call site in `irrigoto.c` and `irrigoto.cpp` uses it: the
  schedule solver's day seeding and weekday test, the schedule executor, the
  rain-delay display and HA's next-run / delay sensors. (On `combined`, the
  run-history stamps and `schedule_entry_for_run` use it too; those only exist
  with `feature/run-history`.)
- `tz_apply()` bounces through `UTC0` before applying the real string, so
  `tzset()` cannot skip the parse as a no-op.
- **`/api/system` reports libc's actual state**: `tz_env`, `tz_abbr`,
  `tz_isdst` and `tz_offset_lt_min` (the old `localtime_r` derivation) next
  to `tz_offset_min`. The two offsets disagreeing is this bug's signature.
  `/api/schedule`'s INFO line logs both for the same reason. Those two
  diagnostics are the only remaining direct `localtime_r` calls.

## Compatibility

- A device with no saved zone keeps using `device_posix_tz`, exactly as today.
- ESPHome's own `time:` component keeps its compiled `device_timezone`. That
  only affects ESPHome-side formatting, not the schedule executor.
- A wrong zone makes schedules fire at the wrong wall-clock time until it is
  corrected; the modal shows the device's local time next to the picker so a
  mistake is visible straight away.

## Testing

Done:
- Browser test of the modal against a mock API: the firmware default is shown,
  **Use it** picks the phone's zone, and a list zone, a custom string and the
  firmware default each post the right `tz` / `tz_name`.
- `tz_posix_valid()` compiled with host gcc against stubs: accepts every rule
  in the list plus `<+0530>-5:30`, rejects `EST`, `5EST` and strings with
  spaces.
- On hardware (`combined`), zone saved as `America/Los_Angeles`:

  ```
  tz_offset_min     -420      device local 2026-09-26 18:20:15
  tz_offset_lt_min  -240      host   local 2026-09-26 18:20:15
  next run          2026-09-27 06:00   (entry says 06:00)
  ```

  Device local time matches the host to the second, and the next-run time
  matches the entry's hour. `tz_offset_lt_min` is still −240 (libc is still
  wrong), and nothing reads it any more.

Still to do:
- Compile this branch on its own (it was built and tested as part of
  `combined`).
- Re-check at the November DST transition, the one case where the fixed point
  needs its third pass.
- After HA time syncs, the log should show no repeated "TZ was changed ...
  elsewhere" warnings.
