# Fix: Zone Setup distance control — responsiveness, and the jog burst

**Branch:** `fix/zone-setup-responsiveness-and-jog` · **Type:** fix · **Status:**
FIXED in b548-b552, confirmed on hardware. Reported over a hands-on session
2026-09-26: *"the zone edit buttons for changing the distance arrow up and
down are super buggy"*, *"it will burst really high for a moment and come
back"*, *"I can still get to 27.6, but the floor is 0"*.

Five separate defects presented as one unusable control. They are listed in
the order they bite, not the order they were found.

## 1. The Water button opened at full bore

`water_toggle` set `s_web_valve_deg = VALVE_OPEN_DEG` unconditionally, so the
stream came on at maximum whatever the arrows said, and turning the water off
parked the setpoint at `VALVE_CLOSED_DEG`, discarding it.

It now opens at the dialed setpoint. Only a session that has never touched
the arrows (the `< 0` sentinel) opens wide, which is what the readout is
previewing in that state. Water-off no longer resets the setpoint, so the
amount survives a toggle and the readout keeps showing what the button will
deliver.

## 2. The burst — a snap-through excursion on an interactive control

Any target inside the hydrodynamic zone routed `valve_goto_ex` to
`valve_goto_jog`, which on an **opening** move first drives the valve to
`VALVE_FRICTION_HI + 2`, dwells 300 ms, and only then jogs closed onto the
target. Measured on this unit (frame offset 73.61) that zone is 343.6-359.6
deg — roughly **5.9 to 23.3 ft** of throw — and the excursion target of 361.6
deg is about **24.6 ft**. It fired on every upward press of the d-pad, and
again every time the water was switched on at a dialed amount inside the
zone. Down-presses skip it, which is why only increasing was affected.

`valve_goto_interactive()` keeps the pulsed, encoder-checked stepping and
drops only the excursion. `valve_goto_jog_ex(..., allow_overshoot)` carries
the flag; `valve_goto_jog()` is now a wrapper passing `true`, so every
watering and calibration caller is bit-for-bit unchanged.

**It is deliberately not conditional on `supply_regulated`.** The owner asked
whether this was a well-water accommodation. It is not: the resistance being
compensated is water pressure on the partially-open ball face, which grows
*with* supply pressure, so a regulated municipal line needs the pulsed
approach at least as much as a well does. What makes the excursion wrong here
is that this is an interactive control — the user is watching the stream and
correcting by eye, so a few tenths of a foot of position error costs nothing
and a 24 ft excursion across the yard costs everything.

## 3. Steps were in valve degrees, so the button did different things

The d-pad stepped the valve by a fixed angle. The throw curve on this unit
runs **58 mm/deg at the bottom and 552 mm/deg around 13 ft**, so one press
moved the stream 0.2 ft down low and 1.8 ft in the middle, and the whole
usable range was only 36 deg — 18 presses end to end at the old maximum step.
That is the "really pulsy when increasing fast".

`/zone/act` now accepts `&ft=`, converts through the pressure cal
(`cal_valve_deg_to_throw_mm` -> target -> `cal_throw_to_valve_deg`) and falls
back to the angular step when no cal is loaded. A tap is 0.25 ft; a hold
opens out to 1 ft. `&deg=` still works, so a stale cached page degrades
rather than breaks.

## 4. Both end stops were wrong, and neither came from the throw calibration

- **Floor read 0 ft.** The readout tested `VALVE_CAL_START_DEG` — "pressure
  begins rising here", 334.97 deg on this unit — while the throw cal's lowest
  *measured* point is valve 333.28 = 494 mm = **1.62 ft**. Anything dialed
  into that 1.7 deg sliver displayed as zero, which is not a distance and is
  not what the calibration says. `zone_display_floor_deg()` takes whichever is
  lower, so a measured distance is shown as a distance. Applied in the same
  three places the old test appeared: the readout, the perimeter-point stamp,
  and the HA throw accessor.
- **`at_min` fired ~32 deg early**, because it used `VALVE_CAL_START_DEG`
  while `pres_dn` clamped at `VALVE_CLOSED_DEG`; and because water-off parked
  the setpoint at the clamp, the down arrow was solid orange the whole time
  the water was off. The d-pad now clamps at `cal_get_min_valve_deg()` and
  `at_min` means that.
- **`at_max` never fired at all**: it tested `VALVE_OPEN_DEG` (381.6) while
  the cal ends at 369.6, so the up arrow stayed unlit sitting at the real
  maximum. It now tests the calibrated maximum, symmetric with `at_min`.

## 5. Jumpiness — a genuine response race

The 900 ms poll, every d-pad step and the post-release settle fetch all called
`applyState` directly, so whichever reply landed last won. During a hold those
overlap constantly and an older `/zone/state` routinely overwrote a newer
`/zone/act`, rewinding the readout: *"doesn't respond right away and is
jumpy"*. Requests are issued in order, so each is stamped and any reply older
than what is already drawn is dropped. The poll also stands down while a hold
is running.

Two related defects fixed alongside:

- `/zone/act` and `/zone/state` ignored their mutex take result and gave the
  mutex back regardless. `s_zone_mutex` is a real mutex, so the give from a
  task that never owned it silently failed — and on a timeout the handler fell
  through and drove the valve motor and I2C bus alongside the actual holder,
  which is what the 60 ms hold-repeat provoked. `/zone/act` now refuses with
  `{"busy":true}` and the UI skips that step; `/zone/state` gives only if it
  took.
- A hold-repeat request still in flight when the button was released and
  re-pressed resumed into the *new* hold, leaving two loops sharing `_hStep`
  and `_hTimer` so release only ever cancelled one. A generation token retires
  stale loops.

## 6. The Water button did not say which state it was in

It signalled on/off with a green glow only, which is hard to read outdoors and
is the one control where guessing wrong means water on the ground. It now
reads "Water ON" / "Water OFF", and the off state is visibly dim rather than
merely un-glowing.

## Testing

Confirmed against the unit at b551/b552:

```
floor     1.62 ft  at_min=True  at_max=False
ceiling  27.64 ft  at_min=False at_max=True
one down 26.64 ft  at_min=False at_max=False     <- orange clears
up from the floor:  1.87 2.12 2.37 2.62 2.87 3.12   (exactly 0.25 ft/step)
up in 1 ft steps:   2.87 ... 26.87 27.64          (26 even steps, then the cap)
```

Still to check by hand, since they need eyes on the stream: that turning the
water on at a dialed amount inside 5.9-23.3 ft no longer bursts, and that a
direct opening move through the hydrodynamic zone settles without a visible
stall-kick now that the approach-from-above is gone. The kick, if it appears,
scales with supply pressure — so it is most likely to show on a high-pressure
regulated line, and the tell is the stream hunting briefly rather than
jumping far and returning.

## 7. The model error underneath all of it (b555)

Everything above treated the valve ANGLE as the stored setpoint and re-derived
the distance from it. That is what kept generating new symptoms:

- Dry, the derivation uses the valve->throw curve. Wet, the stream obeys
  pressure and `cal_pressure_to_throw_mm` is a *different* curve. They agree
  only while supply pressure still matches what it was at calibration, so the
  number on screen and the stream on the lawn disagreed — reported as *"when I
  turn it on it will jump back like 2 feet from whatever I set"*.
- A distance step is not a fixed angular step. At 552 mm/deg a 0.25 ft press
  is 0.14 deg, which sat inside the 1.0 deg positioning tolerance introduced
  alongside ft-stepping — so with the water on, presses did nothing at all.
- An interim fix stepped from the MEASURED throw to match the measured
  readout, which fed a loop: measured -> valve angle -> more open -> higher
  measured, running to the top in a few presses.
- Another interim fix "corrected" at water-on with
  `cal_throw_to_valve_deg(cal_valve_deg_to_throw_mm(V))` — the exact inverse
  of how V was chosen, so it returned the angle already held and corrected
  nothing. It looked right and was a no-op.

The dialed **distance** (`s_web_target_mm`) is the setpoint now. Each press
moves it by exactly the step and the readout shows it, so it never drifts on
its own. The valve angle is only how the setpoint is pursued: a press forces
at least `ZONE_WEB_MIN_MOVE_DEG` (0.35) of valve motion so a sub-degree
distance step still moves the hardware, with `ZONE_WEB_MOVE_TOL_DEG` (0.25)
beneath it so the move is not swallowed by the tolerance. Where the curve is
steep enough that the forced minimum exceeds the requested step, the real
increment is coarser than asked — that is the hardware's resolution, not a
defect, and it is why the control is specified in distance rather than angle.

At water-on the loop is closed on the measurement: error in mm converted to
degrees through the local slope of the throw curve, at most 4 deg per pass and
at most two passes, never a hunting loop. It logs `want X ft, measured Y ft --
valve A -> B (slope N mm/deg, pass n)`, so a persistent gap is visible and
diagnosable — and a large one means the pressure calibration has drifted from
current supply, which wants a recalibration rather than a bigger correction.

Verified dry at b555, including the steep region where presses used to do
nothing:

```
0.25 ft steps: 1.87 2.12 2.37 2.62 2.87 3.12   deltas 0.25 x5
1.0  ft steps: 4.12 5.12 6.12 7.12 8.12 9.12   deltas 1.00 x5
at 21+ ft:     21.37 21.62 21.87 22.12 22.37   deltas 0.25 x4
```

The wet path — that a dialed distance is actually delivered once the water is
on — still needs eyes on the stream.
