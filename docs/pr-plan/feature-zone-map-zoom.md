# Zoom the Zone Setup map to the zone

**Branch:** `feature/zone-map-zoom` · **Type:** feature · **Status:** built, merged into `combined` and flashed to hardware (b535).
Page-only change (`zone_setup.html`).

## Summary

The Zone Setup map is always scaled to the unit's full calibrated throw, so a
small zone is a cluster in the middle with overlapping point labels. Add a zoom
that fits the map to the zone.

## Problem

- `_edScale()` returns `act_max_throw + 914` mm, which is always full throw.
- A ~10 ft zone on a ~28 ft unit fills about a third of the radius. Points
  near the sprinkler overlap, and the Path and Depth views are hard to read.

## Change

- A zoom state, `full` or `zone`, plus an optional free pinch/wheel factor.
- When zoomed, `_edScale()` returns the zone's extent (max `throw_mm` over the
  points) × 1.15. Almost everything draws through `_edScale()`, so most of the
  work is in this one function:
  - rings
  - distance grid labels
  - points
  - path
  - heatmap
  - water trail
  - Edit-mode hit test and drag
- Controls:
  - a **Zoom** button next to Path/Depth, or auto-zoom whenever Path or Depth
    is on
  - pinch on phones and wheel on desktop, limited to [zone extent, full throw]
  - optional drag-to-pan when zoomed
- Distance grid: a smaller step when zoomed (1–2 ft) so the labels stay useful.
- The live throw line and cursor still go to full throw. Clip them at the edge
  and label them (e.g. "27.6 ft →") so it's clear the spray is past the view.
- Edit mode: clamp drags to full throw, not to the zoomed range.
- Remember the zoom per browser (`localStorage`, in try/catch).

## Testing

Zoom in and out with Path/Depth on: everything lines up. Dragging a point
while zoomed stores the correct `throw_mm`. The throw line is clipped with its
label.
