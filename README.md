# irrigoto

ESP-IDF + ESPHome firmware for the OtO sprinkler. Provides zone-based
watering in [several spray modes](#watering-modes) (pulse, gentle, smooth,
serpentine, chase), pressure and throw calibration, scheduling with rain
delay, [winter sleep](#winter-sleep), OTA updates, a built-in web UI, and
integrates with Home Assistant via the native ESPHome API. A Lovelace heatmap
card visualizes per-zone watering depth.

## ⚠ Use at your own risk

This is hobbyist firmware that controls water-handling hardware. Before you
flash anything onto a device or follow any procedure here, understand that:

- **No warranty, no fitness for any purpose.** See `LICENSE`. The code, the
  schematics implied by the code, and the documentation are provided AS IS.
- **Water damage is real.** Misconfigured zones, stuck valves, bad calibration,
  or a bug in this firmware can leave water running. Do not deploy without a
  manual shutoff you can reach quickly, and do not run unattended until you
  have observed the device through several complete cycles.
- **Electrical safety is on you.** If you wire anything to mains, GFCI-protect
  it and have a licensed electrician confirm your work. Low-voltage parts of
  this project are not a substitute for that.
- **Plant and property loss.** Over- or under-watering can damage landscaping;
  pressurized leaks can damage structures. The author has no liability for
  any of this — by using the code you accept that risk.
- **Test in a safe configuration first.** Run the device on a bench with the
  outlet pointed somewhere harmless before connecting it to your plumbing.

If any of the above is not OK with you, do not use this code.

## Install via Home Assistant ESPHome Builder

The fastest path for most users. The ESPHome Builder add-on
([install instructions](https://esphome.io/guides/installing_esphome.html#installation-from-home-assistant-add-on-store))
will compile and flash the firmware entirely from the HA UI.

1. **Copy two files into `/config/esphome/`** on your HA host:
   - [`example.yaml`](example.yaml) — the device config (rename to anything
     you like, e.g. `irrigoto.yaml`)
   - [`irrigoto-partitions.csv`](esphome/irrigoto-partitions.csv) — the 8 MB
     OTA partition table; must sit next to the device yaml

   OtO units ship with either an **8 MB or a 4 MB flash chip** — the
   size varies between units with no known pattern, so always check
   yours (`python -m esptool flash_id` over the UART cable — see
   [`docs/new_device_flashing.md`](docs/new_device_flashing.md)). For a
   4 MB unit, copy
   [`irrigoto-partitions-4mb.csv`](esphome/irrigoto-partitions-4mb.csv)
   instead and uncomment the 4 MB substitutions block in your copy of
   `example.yaml` (instructions in the file).

2. **Add the secrets to `/config/esphome/secrets.yaml`** (create the file
   if it doesn't exist):
   ```yaml
   api_key:        "<openssl rand -base64 32>"  # ESPHome <-> HA encryption
   ota_password:   "<any string>"               # OTA upload password
   wifi_ssid:      "<your WiFi SSID>"
   wifi_password:  "<your WiFi password>"
   ap_password:    "<8+ chars>"                  # Fallback hotspot pwd
   ```

3. **Back up the stock OtO firmware first.** The first flash erases it,
   and your own backup is the only way to ever return the unit to
   stock — see the backup and restore steps in
   [`docs/new_device_flashing.md`](docs/new_device_flashing.md).

4. **Open the device yaml in ESPHome Builder and click Install.** The
   first flash needs a USB-UART adapter (Builder prompts for the port);
   subsequent flashes go over WiFi automatically via OTA.

`example.yaml` tracks the `main` branch by default. To pin to a specific
firmware build, edit both `ref: main` lines and point them at a release tag
like `ref: v392`. Each published release is tagged `v<build>`; tags are
immutable, while `main` is a flattened snapshot replaced on each release.

After the device comes online, set up the Home Assistant side — see
[Home Assistant integration](#home-assistant-integration) below for the
template sensors, automations, schedule and rain-delay sync, and per-zone
heatmap.

## Local development (compile from a clone)

For modifying the firmware itself rather than just deploying a release.

### Prerequisites

- Windows 10/11 build host (Linux/macOS work too with the obvious shell
  substitutions)
- ESPHome installed in its own Python venv. Install from the pinned
  `requirements.txt`, not a bare `pip install esphome` — the latter
  re-resolves to whatever is newest, which has shipped regressions that
  only surface at runtime (e.g. a 2026.5.x post-OTA HA-reconnect crash):
  ```powershell
  python -m venv C:\esphome-env
  C:\esphome-env\Scripts\pip install -r requirements.txt
  ```
- For the initial flash, a USB-UART adapter connected to the device at
  J2 (GPIO16=RxD, GPIO17=TxD, GND). Subsequent flashes use ESPHome OTA
  over WiFi automatically.
- GPIO0 accessible to pull low during reset for bootloader entry.

### Project structure

```
irrigoto/
├── example.yaml                       <- end-user device wrapper (type: git)
├── esphome/
│   ├── irrigoto.yaml                  <- dev wrapper (type: local, 8 MB flash)
│   ├── irrigoto-4mb.yaml              <- dev wrapper for 4 MB-flash units
│   ├── irrigoto-core.yaml             <- canonical config, shared by all
│   ├── irrigoto-partitions.csv        <- custom OTA partition table (8 MB)
│   └── irrigoto-partitions-4mb.csv    <- same layout scaled to 4 MB flash
├── components/
│   └── irrigoto/                      <- ESPHome external component
│       ├── irrigoto.c                 <- main firmware
│       ├── irrigoto.cpp               <- C++ ESPHome wrapper
│       ├── irrigoto.h
│       ├── irrigoto_api.h             <- C ↔ C++ interface
│       ├── i2c_bus.c/.h               <- I2C driver
│       ├── storage.c/.h               <- LittleFS persistence
│       ├── *_html.h                   <- generated web-UI payloads (component
│       │                                 root so every ESPHome version copies
│       │                                 them into the build tree)
│       ├── html/                      <- web UI HTML fragments
│       │   ├── *.html                 <- editable sources (regen.py rebuilds
│       │   │                             the ../*_html.h payloads)
│       │   └── regen.py
│       └── *.py                       <- Python ESPHome entity definitions
├── homeassistant/
│   ├── packages/                      <- HA package YAML (template sensors,
│   │                                     automations, shell_commands)
│   ├── dashboards/                    <- HA dashboard YAML
│   ├── cards/                         <- HA Lovelace card YAML
│   └── lovelace/
│       └── irrigoto-heatmap-card.js   <- custom heatmap card
└── LICENSE
```

### Build & flash

Create `esphome/secrets.yaml` with the same keys listed in the Builder
section above (this file is gitignored — never commit it).

Then compile and flash from plain PowerShell (not the ESP-IDF prompt):

```powershell
C:\esphome-env\Scripts\esphome compile esphome\irrigoto.yaml
C:\esphome-env\Scripts\esphome run     esphome\irrigoto.yaml
```

`esphome run` compiles if needed and then OTA-pushes to the device
(resolved by mDNS). For the very first flash to a brand-new device,
ESPHome falls back to USB and prompts for a serial port.

For units with a **4 MB flash chip**, use
`esphome\irrigoto-4mb.yaml` in the commands above instead — it is the
same config with the 4 MB flash size and partition table substituted
in.

Before a unit's very first flash, follow
[`docs/new_device_flashing.md`](docs/new_device_flashing.md): it covers
telling which flash chip the unit has, and **backing up the stock OtO
firmware** — the flash erases it, and your backup is the only way back
to stock.

The dev wrapper uses `external_components: type: local` pointing at
the sibling `components/` directory, so local edits compile without a
git push. End-user `example.yaml` uses `type: git` against this repo.
Both share `irrigoto-core.yaml` via the `packages:` mechanism, so adding
a new HA service or sensor only needs to be edited in one place.

Once flashed, the device announces itself to Home Assistant via the
native ESPHome API. See [Home Assistant integration](#home-assistant-integration)
to load the template sensors, automations, and heatmap dashboard.

## Watering modes

A mode is a watering *style* — how the stream is swept over the zone and how
the requested depth is delivered. Pick one per run (web UI water modal, HA
dashboard, or a schedule entry). Depth is in eighths of an inch.

| Mode | Web digit | What it does |
| ---- | --------- | ------------ |
| **Pulse 1/8"** | `1` | One pass, ~13 min. Ring-by-ring sweep, valve stepped per ring. The simplest mode and the default. |
| **Pulse 1/4"** | `2` | One pass at double depth, ~26 min. |
| **Pulse 1/8" ×2** | `3` | Two passes of 1/8", ~26 min. Two lighter applications instead of one heavy one — less runoff on slopes. |
| **Gentle 1/8"** | `5` | 5 passes, seed-safe. Low pressure held by closed-loop control so the stream doesn't dig in. For new seed and bare soil. |
| **Gentle 1/4"** | `6` | 10 passes, seed-safe. Same idea, double depth. |
| **Smooth 1/8"** | `7` | Multipass, open-loop. Aggregates delivery across passes and re-aims at what's still short, rather than re-running the whole zone. |
| **Serpentine 1/8"** | `8` | Multipass continuous glide. The nozzle never stops — a boustrophedon sweep with the valve fed forward from the supply trend, which avoids the pressure hunting the stepped modes can show on a well/pressure-tank supply. |
| **Chase** | `c` | Play mode, 1–10 min. Sweeps for a dog to chase. Not a watering mode — no depth accounting. |
| **Demo** | `d` | Max-speed sweep for demonstrations. |

Pulse digit `4` (1/4" over two passes) is accepted by `POST /zone/water` but
isn't offered in the UI. When a run supplies an explicit depth — as the web UI,
HA and the scheduler all do — the depth argument wins and pulse simply repeats
the 1/8" pass that many times, so the digit's built-in depth only matters to a
hand-rolled request that omits it.

Schedule entries and the HA select use their own numbering — `0` Pulse,
`1` Gentle, `2` Smooth, `3` Serpentine — which the firmware translates to the
web digit. If you add a mode, `docs/adding_a_watering_mode.md` lists every
place a mode list is hardcoded.

## Apply solution (pump dosing)

Units that ship with the three peristaltic solution pumps can dose a watering
run from one of three bottles. The stock OtO firmware has no pump code; this
is greenfield (Build 534). It is configured per schedule entry and per manual
run, not per zone, so two entries on the same zone can dose differently.

- **Schedule entry** — the "Apply solution" block on each entry: bottle(s)
  (multi-select rotates 1, 2, 3, … across runs), pump speed (Low / Medium /
  Full = 60 / 80 / 100 % PWM), when (every run, or every Nth run of that
  entry), and optional pulsing (on / off seconds). The block shows an estimated
  mL per run.
- **Manual run** — the Water modal on the landing page has the same block minus
  the scheduling parts, one bottle only. Settings are remembered in the browser,
  not on the device, and are sent as `solution_*` params on `POST /zone/water`
  (absent = no dose, so HA's water-now service is unaffected).
- **Bottle calibration** (`/bottle_cal`, linked from the landing page's
  Calibration card) — mL/s per bottle per speed. Only used for the estimate; the
  device cannot measure flow. Run a pump into a cup with the outlet line off,
  weigh what was consumed (1 g ≈ 1 mL), enter the rate.

Runtime: the pump starts 10 s after water is confirmed flowing and stops when
the run ends, on every exit path (complete, fault, cancel, sleep). One pump at a
time; a new request kills the running one. The drive line is also dropped
whenever the 9 V motor rail goes down, so nothing can outlive a rail-off.

Storage: schedule entries widened from 20 to 32 bytes (schema 4, migrated on
first boot). Calibration and the per-entry run counters live in their own NVS
namespace (`solution`), so HA schedule pushes never touch them. The legacy
`text=` schedule save accepts 7 fields (solution block kept) or 15 fields
(solution block replaced).

Code lives in `components/irrigoto/pump.c` (hardware only) and `solution.c`
(dosing logic); `irrigoto.c` carries only the hooks and HTTP handlers.

### Pump hardware (found by probing, 2026-09-20)

Nothing in the stock firmware or the OtO pin summary documents the pumps; the
following was established by pulsing lines while watching the pumps, with
`PCur` (GPIO34) as the electrical witness.

| Line | Role |
| ---- | ---- |
| **GPIO16** | Pump drive, active-high. Needs the 9 V motor rail (`GPIO18`); reads 0 mA with it off. |
| **GPIO21** | Selects pump 2 when held high before GPIO16 rises. |
| **GPIO19** | Selects pump 3 when held high before GPIO16 rises. |
| *(neither)* | GPIO16 alone drives pump 1. |
| **GPIO34 / ADC1_CH6 (`PCur`)** | Pump current sense — wired on this board despite the pin summary saying otherwise. INA4180A3 chain: 142 mV zero offset, `I(mA) = (mV − 142) × 0.2`. Reads 60–300 mA per pump; brushed-motor ripple makes single samples swing ±50 %. |

Physical positions: pump 1 = GPIO16 alone, pump 2 = GPIO21 + GPIO16, pump 3 =
GPIO19 + GPIO16 (owner-confirmed; the selector is raised ~20 ms before the
drive, and the driver keeps that order). All three pump bottle → outlet as
wired; there is no reverse line. Selection is fully independent — no hardware
modification needed.

Ruled out: the SX1501 expander bits P3–P7 (silent, and bits 4–7 don't even
latch — the part is a 4-I/O SX1501, not the SX1502 the detector assumes), and
GPIO 2, 5, 12, 14, 15, 33.

**Speed control** works only with PWM at **20 kHz**. Under roller load the
motor cannot restart after each off-period at lower frequencies (1–5 kHz
stalls and hums, 200 Hz is marginal); unloaded tests suggested the opposite and
should be ignored. Measured floor at 20 kHz, loaded: **60 % duty** turns
reliably, 50 % twitches, 40 % and below stall. Average PCur while turning is
~270 mA at 100 %, ~215 at 80 %, ~160 at 60 %. Hence the three UI speeds.
Stall detection (flat PCur trace while driving) is possible but not implemented
— every measurement so far was with dry tubing.

**Feed line and bottles.** The clear feed line from each pump to its feed cap
measures 3/16″ OD with a ~0.03″ wall (≈ 1/8″ ID, 3.2 × 4.8 mm). The bottle sits
opening-up under a screw-on feed cap, so a dip tube to the bottom of the bottle
is required; the stock caps on this unit have none (it was most likely part of
OtO's proprietary bottle, which was discontinued in January 2025). Rigid
aquarium airline tubing (3/16″ OD × 1/8″ ID) pushed into the cap is a direct
fit. The cap needs a vent, or the pump pulls a vacuum. Jog each bottle from
`/bottle_cal` until the line is primed before relying on a scheduled dose.

Back-flow prevention on the plumbing side is required before injecting into a
hose supply.

## Winter sleep

Off-season hibernation. The device closes and pressure-verifies the valve,
then deep-sleeps with **no wake source armed** — no RTC timer, nothing. Only a
power cycle revives it, and because the Hall sensor interrupts power on this
hardware, a magnet swipe at the unit is the intended wake gesture. Nothing has
to be opened or unplugged.

```
POST /api/winter?on=1     # winterize now (409 if a run is active)
POST /api/winter?on=0     # cancel, resume normal operation
GET  /api/winter          # {"winter":bool,"left_s":N,"window_s":300}
```

On each wake the unit boots normally — WiFi, Home Assistant and the web UI all
come up — and stays awake for **5 minutes** so you can cancel, then goes back
to sleep. Scheduled watering is suppressed for as long as winter sleep is set,
so a wake can't fire a run into a drained system.

Battery is gated by the normal boot check: below 3.6 V the unit returns to
sleep without powering the motors, so a winter wake on a flat pack can't
strand it awake.

To cancel, use either:

- the **device web UI** — a banner with a countdown and a "Resume normal
  operation" button appears at the top of the landing page whenever the unit
  is winterized. This works with Home Assistant down, and is the escape hatch
  to reach for if anything else fails.
- the **Home Assistant** dashboard — Winter sleep card on the Device tab.
  Both its buttons need the device awake; press one while it's asleep and you
  get a notification saying so rather than a silent no-op.

## Home Assistant integration

The HA-side config — template sensors, automations, schedule and rain-delay
sync, and the per-zone heatmap dashboard — is generated per-fleet from a
manifest. The
files under `homeassistant/packages/` and `homeassistant/dashboards/` are
**templates** with `<<DEV_*>>` placeholders and will NOT load as-is; run the
generator first.

1. Describe your devices. Copy the example manifest and edit it:
   ```bash
   cp homeassistant/devices.example.yaml homeassistant/devices.yaml
   ```
   One entry per device — `slug` (the ESPHome node name, e.g.
   `irrigoto-ab12cd`), a friendly `name`, and the device `url`.

2. Generate the personalized config:
   ```bash
   python tools/ha-regen.py
   ```
   This writes `homeassistant/generated/packages/*.yaml` and
   `homeassistant/generated/dashboards/irrigoto.yaml`.

3. Copy the generated files into Home Assistant:
   - `homeassistant/generated/packages/*.yaml` → your HA `config/packages/`
     (with `homeassistant: packages: !include_dir_named packages` enabled)
   - `homeassistant/generated/dashboards/irrigoto.yaml` → a dashboard
   - register `homeassistant/lovelace/irrigoto-heatmap-card.js` as a
     Lovelace resource for the per-zone watering heatmap

Re-run `python tools/ha-regen.py` whenever you add or rename a device.

## Entering bootloader mode (USB flash only)

To flash the ESP32 over USB:
1. Hold GPIO0 LOW (connect to GND)
2. Press and release RESET (EN pin)
3. Release GPIO0
4. Run the flash command

OTA flashes don't need this — the device handles them in firmware.

## Hardware reference

A full pin-by-pin summary of the OtO control board (ESP32 GPIO
assignments, I2C bus, motor drivers, ADC channels, RGB LED register
values, J2 flash header, WROOM module pinout) lives at
[`docs/oto_pin_summary.md`](docs/oto_pin_summary.md). Refer to it
when modifying the firmware's GPIO usage or wiring new sensors.

## Expected I2C devices

| Address | Device | Function |
|---------|--------|---------|
| 0x18 | MPRLS | Water pressure |
| 0x20 | TCA6408A or SX1502 | GPIO expander / LEDs (part varies between units; firmware auto-detects since Build 483) |
| 0x36 | AS5600 | Nozzle position |
| 0x40 | AS5600L | Valve position |

## Battery voltage calibration

`VBATT_DIVIDER_RATIO` in `components/irrigoto/irrigoto.c` defaults to 2.0.
Measure the two resistors of the VBattRaw divider near J5 and set the
correct ratio before trusting voltage readings.

## Contributing

Contributions are welcome, but note this repo uses an unusual snapshot-release
model — `main` is published as flattened per-build snapshots, and accepted
changes are re-applied on a private development branch rather than merged
directly. See [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a PR.

## License

Apache License 2.0. See [`LICENSE`](LICENSE) for the full text, including
the warranty disclaimer and limitation of liability.
