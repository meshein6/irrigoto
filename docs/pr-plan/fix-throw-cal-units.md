# Fix: say mm / feet on the throw calibration steps

**Branch:** `fix/throw-cal-units` · **Type:** fix · **Status:** built, merged into `combined` and flashed to hardware (b535). One
page, text only.

## Summary

The Pressure → Throw calibration asks the user to "measure the distance" twice
but never says in what unit. The firmware expects **millimetres**, or **feet
with an `f` suffix**, and the serial console says so. The web page doesn't,
so the easiest mistake is entering feet without the `f`, which saves a
throw ~300× too short.

## Where (`components/irrigoto/html/cal.html`)

- Step 1 label ("Valve is open a little… measure the distance to the outer edge
  of this short spray ellipse") and Step 2 label: no unit.
- Placeholders `e.g. 1200 or 4.0f` and `e.g. 6200 or 20.3f`: the only hint,
  and they don't explain what `f` means.
- Results table header `Throw`: no unit.

The firmware parses the value in `irrigoto.c` (~2821–2882): a plain number is
mm, and a trailing `f`/`F` means feet × 304.8. It's the same behaviour as the
console prompt "Enter distance in mm, or feet with 'f' suffix (e.g. 25.5f)".

## Change

- Both step labels: add "Enter the distance in **millimetres**, or in feet with
  an `f` on the end (e.g. `13.1f`)."
- A visible `mm` unit label beside each input.
- Table header → `Throw (mm)`.
- Optional: a live conversion under the input ("= 4.0 m / 13.1 ft"), so typos
  show before saving.

## Testing

Enter `4000` and `13.1f`; both save ~4000 mm. Headers regenerated with
`regen.py`.
