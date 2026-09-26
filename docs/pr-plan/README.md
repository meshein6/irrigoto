# PR plan: fork changes proposed for upstream

Every change in the fork (`meshein6/irrigoto`) lives on its own `fix/` or
`feature/` branch, and each branch carries its own description at
`docs/pr-plan/<branch-name>.md`. The file says what the change is, why it's
needed, exactly what it touches, and how it was (or should be) tested. It can
be pasted as the body of an upstream issue or PR.

`combined` collects all of them when the branches are merged in. This index
exists only on `combined`.

Before opening an upstream PR, drop the branch's `docs/pr-plan/` file from it
(or leave it for the maintainer as a design note), and use its text as the PR
description.

## Branches

| Branch | What it is |
|---|---|
| `main` | Exact copy of upstream `rob-farrellrobotics/irrigoto` `main`. Never committed to. |
| `combined` | `main` + every `fix/*` and `feature/*` branch not yet upstream. This is what the fork's devices run. Changes arrive only by merging a fix/feature branch; the one exception is this index file. |
| `fix/<name>` | One bug fix, cut from `main`. |
| `feature/<name>` | One feature, cut from `main` (unless noted). |

- Fork-internal PRs: `fix/*` or `feature/*` → **`combined`**, never `main`.
- Upstream PRs: `fix/*` or `feature/*` → `rob-farrellrobotics/irrigoto` **`main`**.

## Versioning rules

Upstream `main` is a snapshot branch. The maintainer re-applies accepted
changes on a private branch and releases them under the next `FW_BUILD`
number (see CONTRIBUTING.md). So fork branches must not claim build numbers:

- **Never bump `FW_BUILD`** in `components/irrigoto/fw_version.h`. It stays at
  the upstream base the branch was cut from.
- **Never write fork build labels** (`bNNN`, "Build NNN") in code comments,
  docs, commit titles or UI text. Upstream's comments use `bNNN` to mean
  *upstream* builds, and a fork label would point at the wrong upstream build
  once the maintainer's numbering catches up. Refer to changes by feature name
  instead (e.g. "solution dosing").
- Existing upstream `bNNN` references in code (e.g. `b428`, `b466`) are
  upstream's own history and are cited as-is in these descriptions.
- Line numbers below are approximate for upstream build 527. Function names are
  the reliable anchor.

## Changes

| Branch | Type | Status | Description |
|---|---|---|---|
| `feature/solution-dosing` | feature | **done**, in `combined` | [Three-bottle pump dosing](feature-solution-dosing.md) |
| `feature/supply-regulated` | feature | planned | ["Regulated water supply": trust the pressure calibration during a run](feature-supply-regulated.md) |
| `feature/manual-mode-depth` | feature | planned | [Manual run: pick mode and depth separately, mode tooltips](feature-manual-mode-depth.md) |
| `fix/throw-cal-units` | fix | planned | [Say mm / feet on the throw calibration steps](fix-throw-cal-units.md) |
| `feature/ring-order` | feature | planned | [Per-zone ring order](feature-ring-order.md) |
| `feature/zone-map-zoom` | feature | planned | [Zoom the Zone Setup map to the zone](feature-zone-map-zoom.md) |
| `feature/mode-path-preview` | feature | planned | [Path preview per mode (manual + schedule)](feature-mode-path-preview.md) |
| `feature/run-history` | feature | planned | [Per-run history log file + History card](feature-run-history.md) |
| `feature/wifi-power-settings` | feature | **built**, in `combined`; needs on-device test | [WiFi & power modal: network + wake period in seconds](feature-wifi-power-settings.md) |

Dropped (not worth doing now / not an issue): `fix/pulse-ring-swing`,
`fix/rain-delay-feedback`, and `feature/solution-dosing-improvements` (folded
into `feature/solution-dosing`).

## Suggested order and dependencies

1. `feature/supply-regulated`, `fix/throw-cal-units`, `feature/zone-map-zoom`:
   independent of everything else.
2. `feature/manual-mode-depth`: after `feature/solution-dosing` if both go
   upstream, because both edit the landing page's Water modal.
3. `feature/ring-order`. Its "one ring at a time" option also removes the
   open-valve swing back between rings.
4. `feature/mode-path-preview`: after manual-mode-depth, ring-order and zoom.
5. `feature/run-history`: any time. Its bottle columns need solution dosing.

For larger or behaviour-changing work (solution dosing, ring order, supply
regulated), open an upstream issue first to check the maintainer wants it.

## Checks for every branch

- Edit only `components/irrigoto/html/*.html` for pages, then run
  `python components/irrigoto/html/regen.py` and commit the regenerated
  `*_html.h` too. `regen.py --check` must pass.
- Firmware must compile: `python -m esphome compile esphome/irrigoto.yaml`.
- Motion changes: rehearse dry first (serpentine `dry=1`, or valve held closed)
  before any wet run, as `docs/adding_a_watering_mode.md` asks.
- New settings default to today's behaviour.
