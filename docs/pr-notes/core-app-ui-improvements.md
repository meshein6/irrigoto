# Core app UI improvements — PR notes

Working notes to turn into a PR. Scope: web UI served by the core firmware
(`components/irrigoto/html/*.html`, embedded via the matching `*_html.h`
headers) and the storage it reads and writes (`components/irrigoto/storage.c`).

> **Bottle feature found:** it's the fork branch **`feature/solution-dosing`** (builds
> 528–534, based on upstream `main`): `pump.c/.h`, `solution.c/.h`,
> `html/bottle_cal.html` (`/bottle_cal`), "Apply solution" on the manual water
> modal and on schedule entries, three bottles × speeds 100/80/60 %.
> Items 2–4 (and the bottle columns in item 11) are **improvements to that branch**,
> not changes to upstream main. Do them on `feature/solution-dosing-improvements` cut from
> `feature/solution-dosing`, or fold them straight into `feature/solution-dosing`
> before it's offered upstream.
---

## Branches (fork: meshein6/irrigoto)

| Branch | What | Rule |
|---|---|---|
| `main` | Exact copy of upstream (rob-farrellrobotics/irrigoto) | Never commit. "Sync fork" after each upstream release. |
| `feature/<name>` | One standalone change, cut from `main` | Only that change. This is what gets offered upstream. |
| `combined` | `main` + every `feature/*` not yet upstream + `plan` | What the device runs (`ref: combined`). Rebuild from `main` rather than patching. |
| `plan` | This file | Planning only. Never offered upstream. |

Feature branch names for the PR plan:

| PR | Branch |
|---|---|
| 1 | `feature/pulse-ring-swing-fix` |
| 2 | `feature/supply-regulated` |
| 3 | `feature/manual-mode-depth` |
| 4 | `feature/throw-cal-units` |
| 5 | `feature/ring-order` |
| 6 | `feature/zone-map-zoom` |
| 7 | `feature/mode-path-preview` |
| 8 | `feature/run-history` |
| 9 | `feature/solution-dosing-improvements` (cut from `feature/solution-dosing`) |
| 10 | `feature/rain-delay-hardening` |
| — | `feature/solution-dosing` (existing dosing work) |

---

## PR plan

The items below are split into separate PRs, listed in suggested order. Each can be reviewed and
flashed on its own. Items refer to the numbered sections further down.

| # | PR title | Items | Depends on | Size |
|---|---|---|---|---|
| 1 | **Fix open-valve swing at ring change in pulse/gentle/smooth** | 8 | — | S (firmware) |
| 2 | **Add "Supply regulated" setting to skip the start-of-run pressure check** | 5 | — | M (firmware + Device card) |
| 3 | **Manual watering: pick mode and depth separately, with mode tooltips** | 7, 9 (tooltips) | — | S (landing.html; schedule.html tooltip) |
| 4 | **Clarify throw calibration units (mm / feet)** | 1 | — | XS (cal.html) |
| 5 | **Per-zone ring order: auto / one ring at a time / section by section** | 10 | 1 (shares the ring-loop changes) | L (firmware + Zone Setup) |
| 6 | **Zone Setup map: zoom to zone** | 6 | — | S (zone_setup.html) |
| 7 | **Path preview per mode for manual runs and schedule entries** | 9 (preview) | 3, 5, 6 | L (shared JS, both pages, fix drawPath direction) |
| 8 | **Run history log (logs/runs.csv) + History card** | 11 | 2 (logs the setting); bottle columns wait on 9 | M |
| 9 | **Solution dosing improvements: cal file, default rates, enable toggle** | 2, 3, 4 | `feature/solution-dosing` (build on it, not on main) | M |
| 10 | *(optional)* **Rain delay hardening: clock-unset error + inline status** | 12 | — | S |

Notes:
- PR 1 goes first. It is a real bug (spraying at the previous ring's throw
  during the swing back) and PR 5 changes the same loop.
- PRs 2, 3, 4 and 6 are independent and can be done in parallel.
- PR 8 can ship without bottle columns, then add them in PR 9 (or add them in
  PR 9 if bottles land first).
- Each PR: edit only `html/*.html`, then run `python components/irrigoto/html/regen.py`
  and commit both files (`regen.py --check` must pass). Firmware PRs must compile
  (`python -m esphome compile esphome/irrigoto.yaml`).
- **Don't bump the build number.** Per CONTRIBUTING.md, upstream `main` is a snapshot
  branch. The maintainer re-applies accepted changes on their private dev branch
  and ships them in a numbered release, so the PR will likely be *closed with a
  reference to the release*, not merged.

### What `feature/solution-dosing` does today vs items 2–4
| Item | Today on `feature/solution-dosing` | Change |
|---|---|---|
| 2 cal as a file | Rates are an NVS blob: `solution_init()` → `nvs_load(NVS_KEY_CAL, &s_cal…)`, `solution_set_rate()` → `nvs_store` (`solution.c` ~195–214) | Save/load `/lfs/cal/bottle.json` (`{"rates":[[full,med,low]×3]}`, same shape as `solution_cal_json()`); on first boot, copy the NVS blob into the file once, then stop using the NVS key. Runtime state (`NVS_KEY_RT`) can stay in NVS. |
| 3 default rates | Unset rate = 0 → UI shows "calibrate bottle N" (`landing.html` ~564) | Built-in defaults Full 0.30 / Medium 0.23 / Low 0.17 mL/s for every bottle when the file is missing; UI fields prefilled. |
| 4 enable toggle | Always visible: Bottle cal button (landing ~224), Apply-solution block in modal (~306) and schedule entries | Device-card switch "Bottles" (NVS, default **off**); hides the Bottle cal link, modal block, schedule block; firmware ignores `solution_enabled` on runs/entries when off. |
| 11 run log | — | `bottle`, `speed`, `ml` columns from the solution run state. |

**Before offering `feature/solution-dosing` upstream:**
- Its commits bump `FW_BUILD` to 534 and use build labels 528–534, which will
  **clash with the maintainer's own build numbers** (upstream is at 527 and will keep
  counting). For the upstream PR, drop the `fw_version.h` change and refer to
  features by name, not "b534".
- Bench/probe endpoints (`/api/probe`, `/api/gpio_probe`, `/api/pump_run`, builds
  529–533) were for finding the pump lines. Either keep them as a separate
  "hardware probe" PR or leave them out, so the dosing PR stays focused.
- It changes `landing.html` (manual modal) and `schedule.html` (entries). **PR 3**
  (mode+depth picker) and **PR 7** (path preview) change the same modal and cards,
  so expect merge conflicts in `combined`. Do PR 3 after dosing, or rebase it on top.
- `docs/oto_pin_summary.md` already updated on the branch. Good, matches the
  "document the pump pins" advice.

### Already in the repo (checked against `docs/adding_a_watering_mode.md`)
- **Manual mode + depth (PR 3):** already in **Home Assistant**: the quick-water card
  has separate `water_mode` (Pulse/Gentle/Smooth/Serpentine) and `water_depth`
  (1/8″…1″) selects → `script.<dev>_water_now`. Missing only in the device's own
  landing.html modal (and HA has no Demo/Chase). PR 3 is "bring the device UI up to HA".
- **Serpentine hidden params:** `POST /zone/water` takes `speed=` (3..20 °/s, overrides the
  flow-solved speed) and `dry=1` (full motion, valve held closed). No UI or HA exposes
  them. `dry=1` is useful for testing ring order (PR 5) and checking the
  path preview (PR 7) without water. A slower `speed=` may help serpentine's accuracy.
- **Mode descriptions for tooltips:** README "Watering modes" table (~line 185) already
  has a one-liner per mode. Reuse it so the UI and docs match.
- **Touchpoint checklist:** any PR that changes mode lists (3, 7) must hit every site
  in that doc (landing.html, schedule.html `MODE_LABELS` + select, HA packages, the
  three encodings). PR 3 changes only the landing modal's *presentation*, not
  the encodings, so the schedule/HA sites stay unchanged.
- That doc's "bump `FW_BUILD`" step is the maintainer's own workflow. For fork PRs,
  follow CONTRIBUTING (don't bump).
- **Not present upstream:** bottles/pump dosing (lives on the fork's `feature/solution-dosing` branch), a supply-regulated switch,
  ring-order control, a per-run history file, zoom, or path preview per mode.

### Workflow (fork → upstream)
- One branch per PR, each cut from **upstream `main`** (not from this notes
  branch, and not stacked unless there's a real dependency, e.g. PR 7 on 3/5/6).
- Keep this `docs/pr-notes/` file out of the PRs. It lives only on this branch.
- Consider opening an upstream **issue** first for the larger or behaviour-changing
  ones (2, 5, 9) to check the maintainer wants them before building.
- PR description: the problem (with the field observation), the change, how it was
  tested on a device, and any default-behaviour change (e.g. "Supply regulated"
  defaults off).
- When a release includes your change, rebase or drop your branch. Your fork's `main`
  just fast-forwards to the new snapshot.

## 1. Say in the throw-calibration steps that distances are in mm

**Where:** `components/irrigoto/html/cal.html`, Pressure → Throw section
- Step 1 label (~line 121): "Go measure the distance to the outer edge of
  this short spray ellipse, then enter it below." Units are never mentioned.
- Step 2 label (~line 130): same problem.
- The placeholders `e.g. 1200 or 4.0f` and `e.g. 6200 or 20.3f` are the only
  hint. They're unclear: the `f` suffix meaning **feet** isn't explained.
- Table header `Throw` (~line 140) has no unit either.

**Firmware behavior (unchanged):** the value is POSTed as `throw_mm` to
`/cal/pressure/throw_low` and `/cal/pressure/throw`. A plain number means
millimetres, and a trailing `f`/`F` means feet, converted with ×304.8
(`irrigoto.c` ~2821–2882). The serial console already says this:
*"Enter distance in mm, or feet with 'f' suffix (e.g. 25.5f)."*

**Proposed change:**
- Add to both step labels: "Enter the distance in **millimetres** (mm), or
  in feet with an `f` suffix (e.g. `13.1f`)."
- Put a visible `mm` unit suffix next to the input, like the `mL/s` suffix in
  the flow-rate fields.
- Change the table header to `Throw (mm)`.
- Optional: show the converted value live under the input (e.g. "= 4.0 m /
  13.1 ft") so the user can catch typos before saving.

---

## 2. Store bottle calibration as a file in the filesystem

**Current layout:** all other calibration is JSON on LittleFS
(`storage.h` header comment, `storage.c` ~line 36):

```
/lfs/cal/pressure.json   -- pressure → throw
/lfs/cal/speed.json      -- nozzle speed
/lfs/schedule.json
/lfs/zones/…, /lfs/water/…
```

**Proposed change:**
- Add `/lfs/cal/bottle.json` (`#define BOTTLE_JSON CAL_DIR "/bottle.json"`).
- Add `storage_bottle_save()` / `storage_bottle_load()`, written like
  `storage_spd_save/load` (same `fopen` error logging, same `json_get_*`
  helpers).
- Suggested schema:
  ```json
  {
    "levels": [
      {"name": "Full",   "pct": 100, "ml_s": 0.30},
      {"name": "Medium", "pct": 80,  "ml_s": 0.23},
      {"name": "Low",    "pct": 60,  "ml_s": 0.17}
    ]
  }
  ```
- Result: the file shows up in the `/fs` Filesystem page, can be
  downloaded or backed up, and is kept across OTA like the other cal files.
- If bottle values are currently in NVS, migrate once on boot: if NVS has
  values and `bottle.json` doesn't exist, write the file and then erase the
  NVS keys.
- Update the file-layout comment at the top of `storage.h` and `storage.c`.

---

## 3. Prepopulate default bottle values

Defaults from the current UI (screenshot):

| Level  | % | Flow rate |
|--------|---|-----------|
| Full   | 100 % | 0.30 mL/s |
| Medium | 80 %  | 0.23 mL/s |
| Low    | 60 %  | 0.17 mL/s |

**Proposed change:**
- Put these defaults in firmware as a `static const` table. Don't
  hard-code them in the HTML.
- If `bottle.json` is missing, load the defaults, and write them to the file
  on first save (or on first boot) so the file always exists.
- The UI fills the inputs with the loaded values, so fields are never
  blank.
- Optional: add a "Reset to defaults" button in the bottle cal card.

---

## 4. Add an "Enable bottles" toggle under Device settings

**Where:** `components/irrigoto/html/landing.html`, Device card
(~lines 200–225). It already has these toggles: Detail log, Theme (light),
and Winter sleep.

**Proposed change:**
- Add a `dev-row` switch labelled **Bottles** (or "Enable bottles"), styled
  like the Theme switch (`theme-switch` / `theme-slider`).
- Persist it in NVS the way the theme is saved (`ui_theme` key →
  e.g. `bottles_en`). Or put it in a small settings JSON if item 2 moves
  settings to files.
- Expose it in the status JSON (next to `"detail_log"` ~`irrigoto.c:15727`),
  with a setter endpoint that follows `/api/detail_log?on=0|1`, e.g.
  `/api/bottles?on=0|1`.
- **When off:** hide every bottle feature, including the bottle section in
  `cal.html`, bottle fields and controls on the zone and schedule pages, and
  any bottle status on the landing page. Firmware should also skip bottle
  logic, not just hide the UI.
- Decide the default. Probably **off**, so users without bottles get a
  cleaner UI.

---

## 5. "Supply regulated" setting: skip the 12 s full-open pressure check

**Problem:** every run starts with a ~12 s supply check with the valve
**fully open** (`phase_water_zone`, `irrigoto.c` ~11648–11810, builds
361/363/365). The nozzle aims at the zone's farthest bearing and swings
±20°. On a small zone this sprays far outside it: a zone that reaches
~10 ft, on a unit whose full throw is ~27.6 ft, gets ~17 ft of overspray for 12 s.

**Why the check exists:** it was added for **well pump + pressure tank**
supplies. Those cycle (e.g. pump on at 45 PSI, off at 60), so today's pressure
can differ a lot from calibration day and from minute to minute. A
**regulated** supply (city water, or anything behind a pressure regulator) holds
steady, so the check adds nothing and costs 12 s of overspray.

**Decision:** one global device setting, **Supply regulated** (on/off).
Replaces the earlier "Full / Zone max / Off" idea.

### Setting text (UI, Device card, with ⓘ)
> **Supply regulated**
> Turn on if your water comes from city mains or goes through a pressure
> regulator. Pressure then stays about the same from run to run, so the
> sprinkler skips the 12-second full-power pressure check at the start of
> each run (which can spray past the edge of small zones) and trusts the
> pressure calibration.
> Leave off for a well pump or pressure tank, where pressure rises and falls
> as the pump cycles. The check measures today's supply so the run can adjust.
> If runs start landing short or long after turning this on, recalibrate
> pressure, or turn it back off.

### Firmware behaviour when regulated = on
- Skip the whole pre-check block (valve full-open + waggle + 120 samples), the same
  way `serpentine_dry`/demo already do (~11707 `if (!demo_mode && !serpentine_dry
  && …)` → add `&& !s_supply_regulated`).
- `pressure_scale = 1.0` → smooth pass 0 uses the calibration as-is (pass 1+
  per-ring correction still fixes any error; serpentine doesn't use the scale anyway since b428).
  The time estimate uses calibrated flow (a history estimate, if there is one, still wins).
- Skip the nozzle pre-aim (~11655–11687) too. It only exists to point the check.
- **No-supply detection must move:** today the b466 abort comes from the 12 s peak.
  Without it, a run with the water off would skip ring after ring as a
  "transient" low reading (`s_water_run_had_flow` is false, so the b468 water-loss streak never
  counts) and could run dry for a long time. **Fallback:** if no flow has been
  seen yet and the first ring's settle check reads below
  `WATER_MIN_FLOW_PSI` for ≥ N s (e.g. 10 s, ring 1 + one retry), set
  `WATER_STATUS_NO_SUPPLY` and abort, the same status HA already handles as MISSED.
- Optional, later: with a regulated supply, the pump-aware parts of smooth's
  scheduler (trigger-aware peak hunting, closed-valve dwells, b499/b502) and
  serpentine's supply-trend feed-forward (b522) have nothing to track. They can be
  bypassed for a simpler, more predictable order (pairs well with item 10).
- Log: `Supply check skipped (regulated)` in place of the `Supply:` line, so runs are
  easy to tell apart in `last_log`.

### Plumbing
- NVS key e.g. `supply_reg` (bool, default **off** so existing well users
  keep today's behaviour), loaded at boot next to the other settings
  (`ui_theme`, detail log, auto-sleep).
- `GET/POST /api/supply_regulated?on=0|1`, modelled on `/api/detail_log` /
  `/api/auto_sleep`. Include `"supply_regulated"` in the status JSON
  (~`irrigoto.c` 15727) and `/api/all`.
- landing.html Device card: a switch styled like Theme, plus the ⓘ text above.
- HA: switch entity in the device package (optional).
- Run history (item 11): add a `supply_regulated` column.

### Checklist
- [ ] NVS setting + API + status JSON
- [ ] Skip pre-aim + pre-check when on; `pressure_scale = 1`
- [ ] No-supply fallback on ring 1 when the check is skipped
- [ ] Device card switch + explanation tooltip
- [ ] Wet test on a regulated supply: no start-of-run overspray; depth/throw
      comparable to a run with the check; water turned off → run aborts NO_SUPPLY quickly

---

## 6. Zoom on the zone (Zone Setup map, Path view)

**Problem:** the map in `html/zone_setup.html` is always scaled to the full
calibrated throw: `_edScale() = act_max_throw + 914` (line ~345). A 10 ft
zone on a unit with ~28 ft of throw fills only the middle third, so the
Path view, point labels and depth map are tiny and crowded (see the
screenshot: points 1/7/8/9 overlap).

**Proposed change:**
- Add a zoom state: `let _zoom = 'full' | 'zone'` plus optionally a free
  pinch/scroll factor.
- `_edScale()` returns `zoneExtentMm × 1.15` (max `throw_mm` over `ST.points`,
  plus a margin) when zoomed, else the current value. Everything already
  draws through `_edScale()` (rings ~770, grid labels ~827/835, points
  ~843/897, path, heatmap, water trail, edit hit-test ~1086/1102), so
  most of the work is this one function.
- Controls:
  - a **Zoom** toggle button in the action row (next to Path / Depth), or
    auto-zoom when Path or Depth is on;
  - pinch-zoom on mobile and wheel-zoom on desktop, limited to
    [zone extent, full throw]. Optional: drag to pan when zoomed.
- The distance grid (5'/10'/15'…) needs a smaller step when zoomed (e.g. 1–2 ft)
  so the labels stay useful.
- Things that fall outside the zoomed view: the live throw line/cursor (still
  goes to full throw) should be cut off at the edge, with an arrow or
  "27.6 ft →" label so the user knows the spray is past the view.
- Edit mode: dragging points while zoomed must still map
  correctly. The hit-test uses `_edScale()`, so this should work, but clamp
  drags to full throw, not to the zoomed range.
- Keep the zoom state per viewer in `localStorage` (wrapped in try/catch).
- Optional: the same zoom on the landing page heatmap / last-run view.

---

## 7. Manual "Water Zone": choose mode and depth like a schedule entry

**Problem:** the landing page's Water Zone modal (`html/landing.html` ~234–266)
offers fixed presets: 1/8″, 1/4″, 1/8″×2, Gentle 1/8″, Gentle 1/4″, Smooth 1/8″,
Serpentine 1/8″, Demo, Chase. There's no way to do e.g. Smooth 1/2″ by
hand, even though a schedule entry (`html/schedule.html` ~430–445) lets you pick
**Mode** (Pulse / Gentle / Smooth / Serpentine) and **Depth** (1/8″ … 1″) separately.

**Firmware already supports it:** `POST /zone/water` takes `mode` and an
optional `depth` in eighths (1..8) (`irrigoto.c` ~15070–15076). Pulse runs N×1/8″
passes; Gentle/Smooth/Serpentine aim for `depth × 3.175 mm` with their adaptive
multipass. The HA script already sends it this way (`irrigoto_device.yaml` ~280–316).
**So this is a UI-only change.**

**Proposed change (landing.html modal):**
- Replace the preset grid with two selects (or segmented buttons):
  - **Mode:** Pulse · Gentle · Smooth · Serpentine · Demo · Chase
  - **Depth:** 1/8″ … 1″ (same `DEPTH_LABELS` as schedule.html)
- Demo → hide/disable Depth ("max speed, no depth").
- Chase → hide Depth, show the existing duration slider (`chase-row`).
- Map to firmware digits the way the schedule / HA script do:
  Pulse=1, Gentle=5, Smooth=7, Serpentine=8, Demo=`d`, Chase=`c`. Send
  `id=…&mode=<digit>&depth=<1..8>` (leave out depth for Demo/Chase).
- Estimated time: replace the hard-coded "~13 min / ~26 min" hints with the
  estimate for the chosen mode × depth (or remove them). The firmware sends back its own
  estimate once the run starts.
- Remember the last mode/depth for each viewer (`localStorage`, try/catch).
- Default: Smooth 1/8″ is probably a better default than Pulse (see item 8).
- Share the mode/depth option lists between schedule.html and landing.html
  (same labels and order) so the two UIs can't drift apart.

---

## 8. Pulse mode is jerky; surge at ring start (under investigation)

**Seen:** a manual 1/8″ run (Pulse) on a ~10 ft zone:
- first ~12 s at full throw → the supply check (item 5).
- afterwards the nozzle moves in **~10″ steps** (step, stop, wait). Pulse mode
  does this on purpose: `nozzle_sweep_pulse` (`irrigoto.c` ~7380) and
  `use_pulse_mode = !demo && !gentle && !smooth` (~12534).
- **near-full-strength stream for ~1 s at the start of most rows.** Apart from
  the supply check, no code path in pulse mode sends the valve to full open, so
  the cause isn't known yet. Candidates:
  1. hydraulic surge: the valve closes while the nozzle moves between arcs,
     the line builds up to static supply pressure, and reopening throws
     long until the pressure drops once water flows;
  2. wrong valve target at ring start (bad low-end cal point / overshoot);
  3. the settle-time trim / friction trim over-correcting on a bad reading.
  4. **(strong candidate) the swing back at ring change runs with the valve still open.** Pulse
     sweeps every ring CW (`cw = … : true`, ~12177). Rings go outer→inner. After
     a ring ends, the nozzle is at the arc's far end. The "First arc, ring > 0"
     branch (~12697) runs `nozzle_goto(seg_origin)` **without closing the
     valve**. The valve is only closed between arcs of the *same* ring
     (~12998, `!is_last_arc`). So the swing back sprays at the previous
     (farther) ring's throw, and the valve drops to the new target only after that.
     The comment there says "Reposition under valve-closed", but the code
     doesn't close it. Smooth/Gentle use the same direction for every ring in a pass
     (`pass % 2`), so they probably do this too. Fix: close the valve (or drop it to the
     new ring's valve deg first) before the swing back, or alternate direction per ring
     like serpentine does.

**Field result:** Serpentine (never stops, valve never closes between legs)
had **no bursts** but placed water less accurately. This fits candidate 1 (surge on
reopen after a closed-valve move), because serpentine is the only mode that
doesn't close and reopen the valve at every arc.

**Next steps:**
- [ ] Try the same zone in **Smooth 1/8″** (continuous glide), and check whether the
      stutter and surges go away
- [ ] Get `/zone/last_log` from a pulse run; check `Seat-from-closed`,
      `Friction trim`, `DPS adj`, `Pressure settled … target` lines to tell
      1 from 2/3
- [ ] If it's a surge: ramp the valve open at arc/ring start, or keep the valve
      open at a low setting instead of fully closed during short moves
- [ ] Consider making Smooth the default manual mode

---

## 9. Path preview per mode (manual + schedule entries) and mode tooltips

**Goal:** when choosing a mode, whether in the manual Water Zone modal (item 7)
or in each schedule entry, show a small preview of **the path that mode
will take** on the selected zone. Clicking it opens it full screen. Add an ⓘ
tooltip next to the Mode picker in both places that explains each mode.

### Preview (thumbnail → full screen)
- **Where:**
  - `landing.html` Water Zone modal, under the Mode/Depth pickers; redraws when
    zone or mode changes.
  - `schedule.html`: one thumbnail per entry card (redraws when that entry's
    zone/mode changes).
- **Tap/click** → full-screen overlay (fixed, 100vw/100vh, close ✕ + Esc +
  tap outside). In full screen: zoom (item 6), pass selector (Pass 1 / 2 / …),
  and optionally a "play" animation of the nozzle along the path.
- **What each mode draws** (matching what the firmware actually does):

  | Mode | Ring order | Sweep direction | Between rings | Other |
  |---|---|---|---|---|
  | Pulse | outer → inner every pass | always CW | swing back to arc start (currently valve **open**, see item 8) | dots for the step/stop points |
  | Gentle | outer → inner | same for all rings in a pass, flips each pass | swing back | low-pressure colour; 5/10 passes |
  | Smooth | chosen by the deficit/pump scheduler; can't be known in advance | per pass | swing back | draw rings with numbers shown as "order varies"; pass 1 shown as outer → inner |
  | Serpentine | pass 1 out → in, pass 2 in → out, alternating | flips every ring | wet turn along the boundary | turns drawn as part of the path; pass selector shows the direction flip |
  | Chase / Demo | n/a | n/a | n/a | text-only card, no path |

- Styling: rings coloured by order (gradient from first to last), arrowheads for
  direction, dashed lines for dry moves (valve closed), solid for wet. Zone
  polygon outline underneath.

**Rings are the same for every mode.** `phase_water_zone` builds one ring list
(~11381–11465) before it branches by mode: start at the zone's max throw and
step inward by `700 mm × (t / act_max_throw)` (min 80 mm), so rings get closer together toward
the centre. It adds direct-valve rings below the cal minimum, and for sprinkler-inside
zones it forces a 360° arc and adds inner rings. What differs per mode is
**order, direction, what happens between rings, and the ends of each arc**:
- Pulse/Smooth trim 7° off the end of CW arcs to account for nozzle inertia
  (`seg_deg -= 7`, ~12673); Gentle stops by encoder and doesn't trim.
- Serpentine has its own arc bounds (`serpentine_arc_bounds`, the same logic as
  smooth's) plus boundary-hugging wet turns between rings.
- Later passes differ: Pulse repeats every ring N× (one per 1/8″); Gentle, Smooth and
  Serpentine skip rings that are already satisfied, so later passes have fewer rings.
- (Correction to earlier chat: serpentine used to defer inner rings, but since b432
  it no longer does, ~10068.)

The preview can draw one shared ring set and change only the path overlay.

### Implementation
- **Reuse `drawPath()`** from `zone_setup.html` (~711). It already computes rings
  (`t -= max(700·t/actMax, 80)`, zone min/max, the inner-ring rule for
  sprinkler-inside zones) and point-in-zone clipping. Move it into a shared JS
  snippet (served e.g. at `/static/path.js`, or inlined into each page by the
  header build step) and give it a `mode` + `pass` parameter.
- **Existing mismatch to fix:** `drawPath()` alternates direction per ring
  (`cw = ri % 2 === 0`, ~771), which is only right for Serpentine. Pulse is
  always CW, and Gentle/Smooth flip per pass, not per ring.
- **Better long-term:** add a firmware endpoint, e.g. `GET
  /zone/plan?id=&mode=&depth=&pass=`, that returns the actual plan
  (rings, arcs, direction, wet/dry legs). Serpentine already builds this list in
  `serpentine_build_pass_plan()` (~9025). The preview can then never disagree
  with the firmware. Start with JS-only, and switch to the endpoint later.
- Data needed on landing/schedule pages: zone points (`/zone/state?id=` or the
  zones list) + `act_max_throw`. Cache it per zone.
- Performance: thumbnails are small canvases, so only redraw on change. The
  schedule page may have up to 32 entries, so draw lazily
  (IntersectionObserver).

### Mode tooltips (ⓘ next to Mode, both pages)
- One shared text table, used by both pages:
  - **Pulse:** steps ring by ring, outer to inner, stopping to fine-tune pressure at
    each step. Most precise placement, but jerky and slow.
  - **Gentle:** low pressure, many light passes. Safe for seed and bare soil.
  - **Smooth:** continuous sweep, then re-waters only the rings that are still short,
    timed to the pump cycle. Good default.
  - **Serpentine:** one continuous back-and-forth glide that never stops. Smoothest,
    no bursts, less precise near edges and close to the sprinkler.
  - **Chase:** play mode for dogs (1–10 min). Not tracked as watering.
  - **Demo:** max-speed sweep. Not tracked as watering.
- Tap to open on mobile (hover alone doesn't work on phones), click outside to
  close. Accessible: `button` with `aria-describedby`.
- Optional: show the tooltip text for the selected mode as a one-line caption under
  the picker, so it's visible without tapping.

---

## 10. Per-zone ring order: go one ring at a time and finish one section before the next

**Today:** there is **no per-zone ordering setting**. A zone file only holds
`name`, `num_points` and the perimeter points (`storage.c` ~300–410). The order comes
entirely from the mode (see item 9): Pulse/Gentle go outer→inner in sequence, Smooth's
scheduler picks by deficit and pump trend (it jumps around), Serpentine alternates
out→in / in→out. Within a ring, every separate arc ("lobe") is swept before
moving inward, with a valve-closed jump between lobes (~12998). For a rectangle
with the sprinkler inside, the outer rings split into 2–4 lobes (the far sides and
corners), so every outer ring jumps across the zone and back.

**Wanted:**
1. **Sequential**: one ring after the next, never jumping back and forth; alternate
   direction each ring (snake) so there's no dry swing back.
2. **Section-first**: finish one side (all its rings, outer→inner) before starting
   the next. Only cross into the next side once the rings are small enough that one
   continuous arc covers both sides ("it can pass back over").

**Algorithm for section-first (lobe tree):**
- The ring list is already built (shared across modes). For each ring, the arcs come
  from `water_find_arcs()` (≤ `WATER_MAX_ARCS_PER_RING` = 6, 10° sectors).
- Link the arcs of ring *k* to the arcs of ring *k+1* (next inward) where their bearing
  ranges overlap. That gives a tree: going inward, separate lobes **merge**
  where a smaller ring's arc spans both.
- Visit order = depth-first over that tree: take lobe A from its outermost ring inward
  (snake, alternating direction) until it reaches the merge ring, then go dry to
  lobe B and do it down to the same merge ring, then continue the merged
  rings as single continuous arcs to the centre.
- Jumps per pass = (number of lobes − 1), instead of (lobes − 1) × rings.
- Start with the lobe nearest the nozzle's current bearing. On alternate passes,
  run in reverse (centre → out, lobes in reverse) so pass 2 starts where pass 1
  ended.

**Per-zone setting** (`"ring_order"` in the zone JSON, loaded in
`storage_zone_load`, saved in `storage_zone_save`):
- `auto` (default, current per-mode behaviour)
- `sequential`: outer→inner, snake direction per ring
- `sections`: lobe tree as above (snake within each lobe)

**Mode interactions / trade-offs:**
- **Pulse / Gentle:** easy to apply. Snake direction also removes the open-valve
  swing back from item 8.
- **Smooth:** the order setting overrides the deficit/pump scheduler. Later passes still
  skip satisfied rings, but lose the pump-peak timing, which only matters on a
  well/tank supply. Keep `auto` as the default so current users don't see a change.
- **Serpentine:** already sequential. `sections` would change
  `serpentine_build_pass_plan()` (~9025) to emit legs in lobe-tree order, with the
  wet boundary-hug turns kept inside a lobe and a dry hop only between lobes.

**UI:**
- Zone Setup: a "Ring order" picker (Auto / One ring at a time / Section by section)
  with an ⓘ tooltip, saved with the zone.
- The path preview (item 9) should use it so the user sees the jumps drop.
- Optional: a "start side" choice (e.g. start from the left or right lobe).

**Checklist:**
- [ ] `ring_order` field in zone JSON + zone_perimeter_t + save/load
- [ ] Lobe-tree builder (shared helper) + ordering for pulse/gentle ring loop
- [ ] Smooth: bypass scheduler when `ring_order != auto`
- [ ] Serpentine: plan builder respects the order
- [ ] Zone Setup picker + tooltip; preview reflects order
- [ ] Wet test on a rectangle with the sprinkler inside: count the valve-closed jumps in the log

---

## 11. Run history log file (one line per run, incl. bottle)

**Why `/lfs/logs` is empty:** `storage_init` creates the folder (`storage.c` ~253/274)
and there's a `storage_log(line)` helper (~822) that appends to
`/lfs/logs/YYYYMMDD.log` and keeps 14 files. But **nothing calls
`storage_log()`**, so the folder always stays empty. Run history only exists
as the *last* run: `last_water_save_nvs()` (`irrigoto.c` ~3636, called at ~13642)
stores one `last_water_meta_t` in NVS, and `/lfs/water/last_log.txt` holds the last
run's full debug log.

**Proposed:** on every run completion (right next to `last_water_save_nvs()`),
append one row to **`/lfs/logs/runs.csv`**. Almost all fields are already
collected in `last_water_meta_t` for the HA completion event.

| Column | Source |
|---|---|
| `start_local`, `end_local` (ISO 8601, local tz) | run start tick → epoch; `finish_epoch` |
| `duration_min` | `duration_s` |
| `zone_id`, `zone_name` | `zone_id` + zone file name |
| `trigger` | manual (web) / schedule / HA — new field, set where the run is started |
| `mode`, `depth_in` | mode code → label; `target_depth_mm` → 1/8″ units |
| `status` | completed / cancelled / no_supply / water_loss / fault |
| `passes`, `rings`, `rings_supply_limited` | pass counter, `num_rings`, `rings_supply_limited` |
| `volume_l`, `avg_depth_mm`, `coverage_pct`, `score` | existing getters |
| `supply_psi_min/avg/max`, `supply_regulated` | existing + item 5 setting |
| `bottle`, `bottle_level`, `bottle_ml` | **new**, see below |
| `fw_build` | `FW_BUILD` |

- **Bottle fields:** which bottle (or empty), which level (Full/Medium/Low from item 3),
  and the amount applied in mL = level flow rate (mL/s) × seconds the bottle feed was
  actually open (accumulated during the run, not the whole run time). Empty when
  bottles are disabled (item 4) or not used. *Depends on where the bottle code
  lives (open question at the top).*
- **Time:** when the clock isn't synced (standalone/AP mode, no NTP), write
  `start_local` empty plus `uptime_s`, so rows still sort and don't show a
  1970 date.
- **Size:** at ~200 bytes/row, 1000 rows ≈ 200 KB. Cap at N rows or ~128 KB:
  when full, rotate to `runs.1.csv` (keep one old file), or trim the oldest rows. Check
  free space first (there's already a free-space guard in storage.c ~810).
- Write the header row when the file is created. Append with one `fopen("ab")` per run
  (runs are rare, so there's no flash-wear issue).
- Also: either use `storage_log()` for a few system events (boot, OTA, faults,
  schedule changes) or delete it and the unused `/lfs/logs/*.log` rotation,
  so the folder isn't confusing.

**UI:**
- The file shows up in `/fs` under `logs/`, where it can be downloaded.
- New **History** card/page on the landing page: the last 20 runs in a table (date,
  zone, mode/depth, duration, volume, bottle mL, status), plus "Download CSV".
  Serve it with `GET /api/runs?limit=20` (read the end of the file, not the
  whole thing).
- HA: optional REST sensor/command to fetch it; the completion event already
  carries the same fields.

**Checklist:**
- [ ] `run_log_append()` in storage.c + call at completion (incl. aborted runs)
- [ ] `trigger` source + start epoch captured at run start
- [ ] Bottle mL accumulator (needs bottle code)
- [ ] Rotation / size cap
- [ ] `/api/runs` + History card + download
- [ ] Use or remove the unused `storage_log()`

---

## 12. Rain delay buttons: hardening (optional, low priority)

**Status:** false alarm. The user confirmed the buttons work. The weak spots found while
checking are still worth fixing, but none of them caused a failure in the field.

### A. Device web UI (Schedule page, 6/12/24/48 h + custom)
`schedule.html` `setDelay()` (~625) → `POST /api/schedule/delay hours=N`
(`irrigoto.c` ~18259) → `irrigoto_schedule_set_delay_hours()` (~19850).
- **Silent failure when the device clock isn't set:** `set_delay_hours` returns
  without doing anything if `time(NULL) < 1700000000` ("time not synced — ignoring"), but the
  handler still answers `{"ok":true}`. The page shows "Rain delay set for 6 h."
  while the label stays **off**. The page does push the phone's clock on load
  (`/api/time`, fire-and-forget, ~23), but a tap before that finishes, or a
  failed time POST, hits this.
  **Fix:** make `set_delay_hours` return bool; the handler returns `ok:false,
  error:"device time not set"` (or sets the clock first when the request includes
  `epoch=`); `setDelay()` sends `epoch` with every request.
- **Feedback in the wrong place:** `setDelay()` writes its message to
  `#save-status` (the bottom of the page, next to Save), not `#delay-status` under
  the buttons. On a phone, a tap looks like it does nothing.
  **Fix:** write to `#delay-status`, and disable the buttons + show "Setting…" while
  the request is in flight.

### B. Home Assistant dashboard (Delay 6h/12h/24h/48h)
Buttons → `script.<dev>_set_rain_delay` → `input_text.<dev>_rain_delay =
"until,now"` → `sync_rain_delay` automation pushes `until=&lm=` when the device
is online (b524).
- **Stale package/dashboard:** these buttons call the b524 script. If the package
  isn't updated (script missing) or the dashboard is older (calls
  `esphome.<dev>_delay_schedule_hours` directly), taps fail silently while
  the device sleeps. Check: Developer Tools → Actions → `script.<dev>_set_rain_delay`
  exists.
- **Asleep = expected pending:** the Rain Delay tile should switch right away to
  "until …" with sync "pending push – device asleep". It reaches the device on its next
  wake. If the tile doesn't change at all, it's the stale-package case above.
- **Clock-skew revert (possible):** the automation pushes only if `ha_lm >
  dev_lm`. Otherwise it takes the *pull* branch and **overwrites HA's new
  request with the device's old value**. If the device clock (set from a
  phone, no NTP) is ahead of HA's clock by more than the time since the device's
  last delay change, every button press is reverted within seconds.
  Check the logbook for "rain delay pulled from device" right after a tap.
  **Fix:** only pull when `dev_lm > ha_lm` (never on a tie/unknown after a fresh
  local HA change); and/or have HA push its clock to the device on wake
  (`/api/time`) so both stamps come from the same clock.

**Checklist:**
- [ ] Firmware: `set_delay_hours` → bool; handler returns error when clock unset; accept `epoch`
- [ ] schedule.html: status in `#delay-status`, busy state, send epoch
- [ ] HA: pull-branch guard; clock push on wake

---

## PR checklist

- [ ] Find the bottle feature source (see open question at top)
- [ ] `cal.html`: mm/feet annotation, unit suffix, table header
- [ ] `storage.c/.h`: `bottle.json` save/load, defaults, NVS migration
- [ ] Bottle cal UI reads/writes via new storage; prefilled defaults
- [ ] `landing.html`: Enable-bottles toggle + API + NVS
- [ ] Hide bottle UI across pages when disabled
- [ ] "Supply regulated" setting (item 5): skip pre-check, no-supply fallback, Device card switch + explanation
- [ ] Zone Setup zoom: `_edScale()` zoom, button + pinch/wheel, grid step, clipped throw line
- [ ] Water Zone modal: Mode + Depth selects (Demo/Chase have no depth)
- [ ] Pulse surge investigation (item 8) — pending Smooth test + log
- [ ] Path preview: shared drawPath(mode, pass), thumbnails in modal + schedule entries, full-screen overlay
- [ ] Fix drawPath direction rule per mode
- [ ] (later) `/zone/plan` endpoint so the preview matches the firmware
- [ ] Mode ⓘ tooltips (shared text) on landing + schedule
- [ ] Per-zone ring order (item 10): auto / sequential / sections
- [ ] Run history `logs/runs.csv` + History card (item 11)
- [ ] (optional) Rain delay hardening (item 12)
- [ ] Regenerate `*_html.h` headers from the edited `html/*.html`
- [ ] `regen.py --check` passes; firmware compiles (no build-number bump, the maintainer does that)
- [ ] Test on device: fresh flash (no files) → defaults appear; toggle
      off → no bottle UI anywhere; `/fs` shows `cal/bottle.json`
- [ ] Wet test with Supply regulated on: no start-of-run overspray; water off → quick NO_SUPPLY abort
- [ ] Zoom: path/points/edit drag stay correct at zone zoom and full zoom
