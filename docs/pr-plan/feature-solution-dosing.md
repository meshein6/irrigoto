# Solution dosing: three-bottle pump dosing on watering runs

**Branch:** `feature/solution-dosing` · **Type:** feature · **Status:** done,
running in the fork's `combined`, verified on hardware.

## Summary

Some OtO units have three small peristaltic pumps fitted that feed from three
bottles into the water stream. The stock OtO firmware has no pump code, OtO
discontinued its solutions program in January 2025, and irrigoto's pin summary
lists the pump lines as "unpopulated". This change identifies the pump
hardware by probing, adds a pump driver and a dosing module, and lets a
watering run apply a solution (fertilizer, wetting agent, etc.) from one of
the three bottles, configured per schedule entry or per manual run.

Units without pumps are unaffected: nothing doses unless a run asks for it.

## Hardware findings (established by probing)

Nothing upstream or in the OtO pin summary documented the pumps. Lines were
found by pulsing candidate GPIOs and expander bits with the 9 V motor rail up,
watching the pumps and using `PCur` (GPIO34) as the electrical witness.

| Line | Role |
|---|---|
| **GPIO16** | Pump drive, active-high. Needs the 9 V motor rail (`GPIO18`); reads 0 mA with the rail off. |
| **GPIO21** | Selects pump 2 when held high before GPIO16 rises. |
| **GPIO19** | Selects pump 3 when held high before GPIO16 rises. |
| *(neither)* | GPIO16 alone drives pump 1. |
| **GPIO34 / ADC1_CH6 (`PCur`)** | Pump current sense. **It is wired**, although the pin summary said it wasn't. INA4180A3 chain: 142 mV zero offset, `I(mA) = (mV − 142) × 0.2`. 60–300 mA per pump; brushed-motor ripple swings single samples ±50 %, so average. |

- The selector is raised ~20 ms before the drive and held for the run. Only
  one pump can run at a time, since they share the drive line. All three pump
  bottle → outlet as wired, and there is no reverse line.
- **Ruled out:** GPIO 2, 5, 12, 14, 15, 33, and the LED expander's spare bits.
- **Side finding (possible upstream bug):** on the probed unit, the LED
  expander at 0x20 only latched bits 0–3. Writes to bits 4–7 don't stick,
  which matches a 4-I/O **SX1501**, not the SX1502 that
  `led_expander_detect()` assumes. The LEDs (bits 0–2) work either way, so
  it's harmless today, but it's worth knowing if anything ever uses P3–P7.
  This change also reports the detected part as `"led_expander"` in
  `/api/status`, so it can be checked on a running unit. Before, it was only
  logged seconds into boot, before the API was up.

### Speed control

- Speed is PWM duty on GPIO16 **at 20 kHz** (LEDC, timer 3 / channel 7, clear
  of the MCPWM motor path). 100 % drives the pin as a plain level.
- Under roller load the motor can't restart after each off-period at lower
  frequencies: 1–5 kHz stalls and hums, and 200 Hz is marginal. An early
  *unloaded* frequency sweep suggested the opposite (that 20 kHz switched badly
  and full-on + 1 kHz was better). **Ignore that result.** Loaded testing is
  what counts.
- Measured floor at 20 kHz, loaded: **60 % turns reliably**, 50 % twitches,
  40 % and below stall. Average PCur while turning: ~270 mA at 100 %, ~215 at
  80 %, ~160 at 60 %. Hence the three UI speeds **Full / Medium / Low =
  100 / 80 / 60 %**.

### Plumbing

- Feed line from each pump to its feed cap: 3/16″ OD, ~0.03″ wall (≈ 1/8″ ID).
- The bottle sits opening-up under a screw-on feed cap, so a **dip tube** to
  the bottom of the bottle is required. The stock caps on the probed unit had
  none (it was probably part of OtO's discontinued proprietary bottle). Rigid
  aquarium airline tubing (3/16″ OD × 1/8″ ID) pushed into the cap fits
  directly.
- The cap needs a **vent**, or the pump pulls a vacuum.
- Prime each line with the jog on `/bottle_cal` before relying on a dose.
- **Back-flow prevention** on the plumbing side is required before injecting
  into a hose supply.

## What it does

- **Per schedule entry** (schedule page, "Apply solution" block):
  - bottle(s): selecting more than one bottle rotates 1 → 2 → 3 across runs
  - speed: Low / Medium / Full
  - when: every run, or every Nth run of that entry (N = 2–20)
  - optional pulsing: on/off seconds, 1–600 each
  - a live "~X mL per run" estimate
- **Per manual run** (landing page Water modal): the same block without the
  scheduling parts, one bottle only. It's hidden for Chase and Demo. The browser
  remembers the choice (`localStorage`) and sends it as `solution_*` params
  on `POST /zone/water`. If the params are absent, nothing is dosed, so HA's
  water-now service is unaffected.
- **Bottle calibration page** `/bottle_cal` (linked from the landing page's
  Calibration card):
  - mL/s per bottle per speed, either typed in or derived from a timed jog plus
    the measured volume consumed (1 g ≈ 1 mL)
  - a jog button for priming
  - live pump current while jogging
  - rates only feed the estimate; the device can't measure flow
- **Runtime:**
  - the pump starts **10 s after water is confirmed flowing**, then holds or
    pulses until the run ends
  - it stops on every exit path: complete, fault, cancel, sleep
  - `pump_stop()` is also called from `motor_rail_off()`, so the drive line
    can never outlive the 9 V rail
  - one pump at a time: a new request stops the running one and takes over

## Code changes

New modules, kept separate so they re-apply cleanly on upstream snapshots:

- **`components/irrigoto/pump.c/.h`**: hardware only. It never touches the rail
  or the ADC directly; the host passes those in through a `pump_hal_t` struct
  (`rail_on`, `rail_off`, `rail_is_on`, `pcur_mv`). API: `pump_init`,
  `pump_start(pump, speed%)`, `pump_stop`, `pump_running`, `pump_active`,
  `pump_speed`, `pump_elapsed_ms`, `pump_read_ma`, `pump_clamp_speed`.
- **`components/irrigoto/solution.c/.h`**: dosing logic.
  - Calibration (mL/s per bottle × speed) and per-entry run counters (every-Nth
    count, rotation pointer) live in their **own NVS namespace, `solution`**, so
    HA schedule pushes can never disturb them.
  - A small task on PRO_CPU runs the per-run state machine: armed → waiting for
    flow → 10 s delay → dosing → done.
  - Lifecycle hooks: `solution_arm_entry()` / `solution_arm_manual()` before a
    run, `solution_note_flow()` when flow is confirmed, and
    `solution_on_run_end()` on every exit path.
- **`irrigoto.c`**: hooks and HTTP handlers only.
  - The hooks are: init, rail-off, sleep, run end, run start (arm), and three
    flow-confirmed points:
    - the start-of-run supply check
    - first flow in the ring loop
    - first flow in the serpentine glide
  - `/api/all` carries a `"solution"` status object:
    `armed, phase, bottle, speed, pulse, pump_s, est_ml`.
  - The landing page's watering bar shows the dose next to the countdown.
- **`irrigoto_types.h`**: schedule entries widen **20 → 32 bytes, schema 3 → 4**.
  The new fields are:

  | Field | Values |
  |---|---|
  | `solution_enabled` | 0 or 1 |
  | `solution_bottles` | bitmask |
  | `solution_when` | every run / every Nth |
  | `solution_every_n` | 2–20 |
  | `solution_speed` | 60, 80 or 100 |
  | `solution_pulse` | 0 or 1 |
  | `solution_pulse_on_s`, `solution_pulse_off_s` | 1–600 |

  The old 644-byte schema-3 blob is migrated on first boot (`schedule_load_nvs`).
  **Zero means "default"** in every solution field, so HA's sync path (which
  never writes them) never needs to know about them.
- **Schedule text format** (`text=` save): accepts **7 fields** (unchanged; the
  entry keeps its existing solution settings) or **15 fields** (the solution
  block is replaced). Existing HA pushers keep working untouched.
- **`/api/schedule`**: adds the solution fields and a per-entry `est_min`. The
  duration estimate now scales a zone's run history by the target depth.
- **New endpoints** (`max_uri_handlers` 76 → 82, the `uris[]` static assert
  74 → 80):
  - `GET /bottle_cal`: page
  - `GET/POST /api/solution_cal`: `{"speeds":[100,80,60],"rates":[[…]×3]}`;
    POST `bottle=&speed=&rate=` (0 clears)
  - `GET/POST /api/pump_jog`: POST `pump=1..3&speed=60..100&s=1..120` or
    `stop=1`; GET returns `running, pump, speed, elapsed_ms, ma`
  - `GET /api/solution_est?zone=&mode=&depth=`: `{"est_min":n}`
- **Pages:** `html/bottle_cal.html` (new), `landing.html` (Water modal block,
  status in the watering bar), `schedule.html` (entry block). All headers
  regenerated with `regen.py`.
- **Docs:**
  - README gets an "Apply solution (pump dosing)" section, with the hardware
    findings above
  - `docs/oto_pin_summary.md` now lists GPIO16/19/21 and PCur as pump lines
- `.gitignore`: `.DS_Store` and `cal-backups/` (local per-unit snapshots).

The bench endpoints used for discovery (`/api/probe`, `/api/gpio_probe`,
`/api/pump_run`) were removed before this branch was finished. Their findings
are recorded above and in the README.

## Compatibility

- Units without pumps: no behaviour change unless a run requests a dose.
- The schedule NVS migrates automatically, forward only. Downgrading to
  firmware without this change would not read the 32-byte entries.
- HA packages need no changes. HA doesn't show or edit the solution block
  (possible follow-up).

## Testing done

- Built and flashed over the air on a pump-equipped unit (no rollback).
- All three pages driven end to end against a mock of the device API.
- Resources: RAM 95.6 %, flash 82.1 %.

## Known gaps / notes for the maintainer

- **Not implemented: stall / dry-run detection.** Every PCur measurement so
  far was with dry tubing, so the wet signature is unknown. A flat PCur trace
  while driving would be the signal.
- **RAM is at 95.6 %.** Worth checking against other features in the queue.
- **Schema number.** If upstream adds its own schedule schema 4 in the
  meantime, renumber this one when re-applying. The migration keys off the old
  blob size (644 B), so only the constant changes.
- Only the calibration estimate needs mL/s. The device can't meter volume.
- Follow-ups: `feature/solution-dosing-improvements` (calibration as a file,
  default rates, enable toggle), and bottle columns in `feature/run-history`.
