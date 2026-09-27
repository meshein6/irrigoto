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
| `fix/<name>` | One bug fix, cut from `main` (unless noted). |
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
  instead (e.g. "solution dosing"). Work done straight on `combined` broke this
  rule; the branches split out of it still carry some fork labels in comments,
  and their descriptions say so.
- Existing upstream `bNNN` references in code (e.g. `b428`, `b466`) are
  upstream's own history and are cited as-is in these descriptions.
- Line numbers below are approximate for upstream build 527. Function names are
  the reliable anchor.

## Changes

| Branch | Type | Status | Description |
|---|---|---|---|
| `feature/solution-dosing` | feature | **done**, in `combined` | [Three-bottle pump dosing](feature-solution-dosing.md) |
| `feature/supply-regulated` | feature | built, in `combined` | ["Regulated water supply": trust the pressure calibration during a run](feature-supply-regulated.md) |
| `fix/throw-cal-units` | fix | built, in `combined` | [Say mm / feet on the throw calibration steps](fix-throw-cal-units.md) |
| `feature/watering-path` | feature | built, in `combined`; wet test and standalone compile outstanding | [Watering path: mode/depth/passes pickers, path preview, Sections mode, zone coverage, run plan](feature-watering-path.md) |
| `feature/run-history` | feature | built, in `combined` | [Per-run history log file + History card](feature-run-history.md) |
| `feature/wifi-power-settings` | feature | built, in `combined`; needs on-device test | [System settings modal: network + wake period in seconds](feature-system-settings.md) |
| `fix/timezone` | fix | **fixed**, in `combined`, confirmed on hardware. Cut from `feature/wifi-power-settings` | [Set the timezone on the device, and make local time follow it](fix-timezone.md) |
| `fix/zone-setup-responsiveness-and-jog` | fix | **fixed**, in `combined`, confirmed on hardware. Branch is currently cut from `combined`, not `main` | [Zone Setup distance control: responsiveness and the jog burst](fix-zone-setup-responsiveness-and-jog.md) |
| *(no branch yet)* | fix | **fixed**, in `combined` (part of the Regulated water supply work) | [Stream over/undershoot on a distance change](fix-stream-settling-overshoot.md) |

`feature/watering-path` replaces `feature/manual-mode-depth`,
`feature/zone-map-zoom`, `feature/mode-path-preview`, `fix/preview-pass-count`,
`feature/ring-order` (abandoned, superseded by the Sections mode) and
`feature/coverage-and-passes`, and carries the open `depth-by-speed` and
`slow-sweep-floor` proposals in its "Open work" section.

`fix/timezone` replaces `fix/localtime-ignores-saved-timezone` and the
timezone part of `feature/wifi-power-settings`.

Dropped (not worth doing now / not an issue): `fix/pulse-ring-swing`,
`fix/rain-delay-feedback`, and `feature/solution-dosing-improvements` (folded
into `feature/solution-dosing`).

## Suggested order and dependencies

1. `feature/supply-regulated`, `fix/throw-cal-units`: independent of
   everything else.
2. `feature/watering-path`: after `feature/solution-dosing` if both go
   upstream, because both edit the landing page's Water modal and the
   schedule page.
3. `feature/wifi-power-settings`, then `fix/timezone` on top of it.
4. `feature/run-history`: any time. Its bottle columns need solution dosing.
5. `fix/zone-setup-responsiveness-and-jog`: needs re-cutting from `main` first.

For larger or behaviour-changing work (solution dosing, the watering path,
supply regulated), open an upstream issue first to check the maintainer wants
it.

## Checks for every branch

- Edit only `components/irrigoto/html/*.html` (and `html/*.js`) for pages, then
  run `python components/irrigoto/html/regen.py` and commit the regenerated
  `*_html.h` / `*_js.h` too. `regen.py --check` must pass.
- Firmware must compile: `python -m esphome compile esphome/irrigoto.yaml`.
- Motion changes: rehearse dry first (serpentine `dry=1`, or valve held closed)
  before any wet run, as `docs/adding_a_watering_mode.md` asks.
- New settings default to today's behaviour.
