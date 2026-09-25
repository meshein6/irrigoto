# Path preview per mode, for manual runs and schedule entries

**Branch:** `feature/mode-path-preview` · **Type:** feature · **Status:**
planned. Builds on `feature/manual-mode-depth`, `feature/ring-order` and
`feature/zone-map-zoom`.

## Summary

When choosing a mode (in the manual Water modal, or on a schedule entry), show
a small picture of **the path that mode will take** on that zone. Tap it for a
full-screen view. It shows ring order, sweep direction and the moves between
rings, so users can see how the modes differ before running one.

## What each mode draws

The rings are the same set for every mode. Only the path over them changes.

| Mode | Ring order | Direction | Between rings |
|---|---|---|---|
| Pulse | outer → inner | always CW | swing back to arc start |
| Gentle | outer → inner | one direction per pass, flips each pass | swing back |
| Smooth | chosen at run time by deficit/pump scheduler | per pass | swing back; drawn as "order varies" |
| Serpentine | pass 1 out → in, pass 2 in → out, alternating | flips every ring | wet turn along the boundary |
| Chase / Demo | — | — | text card, no path |

With `feature/ring-order`, a non-auto zone setting overrides the order.

Styling:
- rings coloured by visit order
- arrowheads for direction
- solid for wet, dashed for dry (valve closed)
- the zone outline underneath

## Change

- **Reuse `drawPath()`** from `zone_setup.html`, which already builds the
  rings (zone min/max, spacing rule, inner rings for sprinkler-inside zones)
  and clips to the polygon. Move it to a shared script used by
  `zone_setup.html`, `landing.html` and `schedule.html`, and add `mode` and
  `pass` parameters.
- **Fix an existing mismatch:** `drawPath()` alternates direction every ring
  (`cw = ri % 2 === 0`), which is only true for Serpentine. Pulse is always CW,
  and Gentle/Smooth flip per pass.
- Placement:
  - thumbnail under the Mode/Depth pickers in the Water modal
  - one thumbnail per schedule entry card, drawn lazily with
    IntersectionObserver since there can be up to 32 entries
- Full screen: overlay with close (✕, Esc, tap outside), zoom from
  `feature/zone-map-zoom`, a pass selector, and an optional play animation.
- Data: zone points (`/api/zone?id=`) and `act_max_throw`, cached per zone.
- **Later, for exact accuracy:** `GET /zone/plan?id=&mode=&depth=&pass=`
  returning the firmware's actual plan (rings, arcs, direction, wet/dry legs).
  `serpentine_build_pass_plan()` already builds this list for Serpentine.

## Testing

Compare each preview with a dry rehearsal of the same zone and mode (the
`last_log` ring/arc order, or serpentine `dry=1`).
