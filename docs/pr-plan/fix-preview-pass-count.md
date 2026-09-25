# Fix: the preview's pass selector doesn't match what the run does

**Branch:** `fix/preview-pass-count` · **Type:** fix · **Status:** planned.
Reported after watching a real run against the preview (2026-09-25).

## Problem

The shared path preview lets you step through "Pass 1..4". That 4 is a
number I picked, not anything the firmware does:

```js
ov.passIdx = (ov.passIdx + 1) % 4;      // path.js, openPreview's pass button
```

The firmware's pass counts are per mode, and none of them is 4
(`phase_water_zone`, ~line 11771):

| Mode | passes | Notes |
| --- | --- | --- |
| Pulse | `depth8` | one 1/8″ pass per eighth of target depth |
| Gentle | `GENTLE_MAX_PASSES` = 20 | a cap; adaptive termination usually stops at 5-12 |
| Smooth | 30 | a cap; exits when every ring meets target |
| Serpentine / Sections | 30 | same, `out_to_in` alternates per pass |

So the preview both offers passes that will never run (Pulse at 1/8″ has
exactly one) and hides passes that will (Gentle routinely does 8+).

Worse for Serpentine and Sections, the *direction* alternates by pass
(`out_to_in = !(pass % 2)`), so a wrong pass index draws the sweep going the
wrong way — which is exactly the thing the preview exists to show.

## Change

- Pass the real cap in: `openPreview({..., passes: N})`, computed by the
  caller from mode and depth. Hide the pass control entirely when `N == 1`
  (Pulse at 1/8″), rather than showing a stepper that does nothing.
- For the adaptive modes, label it honestly. The count is a *cap*, not a
  plan — a run usually ends early once the rings meet target. Something like
  "Pass 3 of up to 30" is truthful; "Pass 3 of 30" is not.
- Pulse is the only mode where the pass count is known exactly up front, and
  there every pass is identical, so the selector can be dropped for it.

## Testing

Compare against `/zone/last_log` for a completed run of each mode: the pass
count the log reports should be inside the range the preview implied, and the
drawn direction for pass *n* should match the log's `out->in` / `in->out` for
that pass.
