# Fix: every rendered timestamp used the compiled zone, not the saved one

**Branch:** `fix/localtime-ignores-saved-timezone` · **Type:** fix · **Status:**
FIXED in b549 and confirmed on hardware. Reported 2026-09-26: *"The next run
timestamp is still showing EST. Fix the timezone across the entire device."*

## Symptom

A unit with `America/Los_Angeles` saved in System settings rendered every
timestamp in Eastern time — three hours out. `/api/system` looked correct and
gave no hint of the problem:

```
tz       : PST8PDT,M3.2.0/2,M11.1.0/2
tz_name  : America/Los_Angeles
tz_saved : true
offset   : -240 min          <- EDT, should be -420 (PDT)
```

The `tz` field is our own copy of what was *saved*, not what libc is *using*,
so the response could not distinguish "the zone never applied" from "the zone
applied and the clock is wrong."

## Cause (measured, not inferred)

`localtime_r` on this build fills in the wall-clock fields using a stale
numeric zone while labelling them with the current one. Measured on b548 with
`TZ=PST8PDT,M3.2.0/2,M11.1.0/2` present in the environment:

| | value |
| --- | --- |
| `getenv("TZ")` | `PST8PDT,M3.2.0/2,M11.1.0/2` |
| `tm_isdst` from `localtime_r` | 1 |
| `strftime("%Z")` | `PDT` |
| offset via `localtime_r` − `gmtime_r` | **−240** (wrong) |
| offset via `mktime` round trip | **−420** (right) |

So `localtime_r` knows the zone is Pacific and says so, and then applies the
Eastern offset. `mktime`, in the same binary, follows `TZ` correctly — which
is why scheduled runs fired at the right wall-clock time all along while the
UI showed them three hours late.

The decisive test: setting the zone to `UTC0` at runtime did **not** move
`localtime_r`'s answer either. So the numeric rule it applies is frozen at
whatever the `on_boot` hook installed, and no amount of `setenv` + `tzset`
dislodges it. `tz_guard()` could never catch this: it compares the TZ *string*,
which was correct the whole time.

## Change

- **`irrigoto_tz_offset_min_at(t)`** — minutes east of UTC at an instant,
  solved by fixed point against `mktime`: guess the local fields, ask `mktime`
  which instant they name, correct by the error. Converges in two passes away
  from a DST transition, three at one, and bails out past ±14 h.
- **`irrigoto_localtime_r(t, out)`** — `localtime_r`'s contract exactly,
  implemented as `gmtime_r(t + offset)`. `tm_isdst` is taken from libc, which
  gets that part right, so a struct handed back to `mktime` or formatted with
  `%Z` still behaves.
- All 14 local-time call sites in `irrigoto.c` and `irrigoto.cpp` now use it:
  the schedule solver's day seeding and weekday test, `schedule_entry_for_run`,
  the rain-delay display, and the run-start/finish stamps.
- `tz_apply()` bounces through `UTC0` before applying the real string, so
  `tzset()` cannot skip the parse as a no-op. This did not fix the symptom on
  its own, but it removes one way the state can go stale.
- **`/api/system` now reports libc's actual state** — `tz_env`, `tz_abbr`,
  `tz_isdst` and `tz_offset_lt_min` (the old `localtime_r` derivation)
  alongside `tz_offset_min`. The two offsets disagreeing is the signature of
  this bug, and it is now visible in one request instead of needing an
  afternoon of bisecting. `/api/schedule`'s INFO line logs both for the same
  reason.

The two remaining direct `localtime_r` calls are those diagnostics, which
exist to compare against libc on purpose.

## Testing

Confirmed on the unit after b549, zone saved as `America/Los_Angeles`:

```
tz_offset_min     -420      device local 2026-09-26 18:20:15
tz_offset_lt_min  -240      host   local 2026-09-26 18:20:15
next run          2026-09-27 06:00   (entry says 06:00)
```

Device local time matches the host to the second, and the next-run timestamp
matches the entry's hour. `tz_offset_lt_min` is still −240 — libc is still
wrong, and nothing reads it any more.

Worth re-checking at the November DST transition, which is the one case where
the fixed point needs its third pass.
