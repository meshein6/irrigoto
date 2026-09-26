# System settings modal: network, wake period and timezone from the device page

**Branch:** `feature/wifi-power-settings` · **Type:** feature · **Status:**
built. The modal was tested in a browser against a mock API; it still needs an
on-device build and test.

## Summary

A **System settings** button in the landing page's Device card opens a modal
where you can:

1. See the WiFi name (SSID) and password the device is using, and change them.
   The device saves the new network and reboots to join it.
2. Set the **wake period in seconds**:
   - **Always on:** never sleeps.
   - **300 s / 300 s:** the default cycle.
   - **Custom:** any awake / sleep combination from 30 to 3600 seconds each.
3. Pick the **timezone** schedule times are interpreted in. Choose a zone from
   the list, enter a custom POSIX TZ string, or go back to the firmware
   default. If the phone's zone is different from the device's, the modal
   offers a one-tap **Use it**.

Until now, the network was fixed at compile time (`!secret wifi_ssid`) and
could only be changed by reflashing or through the fallback hotspot. The wake
cycle could only be set from Home Assistant, and only in whole minutes for the
awake part. The timezone could only be changed by editing the
`device_timezone` / `device_posix_tz` substitutions and reflashing (default
US Eastern).

## How the wake period works (unchanged behaviour, now editable)

- **Awake** (`pm_inact_s`): the device sleeps after this many seconds with no
  activity. Activity means a web page request (an open page counts until 30 s
  after its last request), a command, or motor use. The timer starts at boot.
- **Sleep** (`pm_dur_s`): the length of each deep-sleep nap.
- These always override the cycle, as before:
  - Watering, calibration, a frame sweep and OTA hold the device awake.
  - A scheduled run less than 120 s away blocks sleep.
  - Naps are shortened to wake 60 s before the next scheduled run.
- **Always on** is the existing auto-sleep switch (`pm_disable`) turned off.
  The awake and sleep values are kept, so switching back to a cycle restores
  them.

## How the timezone works

- The system clock stays UTC. Schedules are interpreted through libc's `TZ`
  (a POSIX rule string), which the `on_boot` hook sets from
  `device_posix_tz`. That compiled value is now only the **default**.
- A zone saved in the modal is stored in NVS (`tz_posix`, plus the IANA name
  as a label in `tz_name`). `irrigoto_init()` applies it right after the
  `on_boot` hook, so it takes effect before the schedule task starts.
- Saving applies immediately, with no reboot. The armed scheduled run is a UTC
  epoch computed under the old zone, so it is disarmed and re-armed from the
  schedule table at the next sleep.
- **Firmware default** erases the saved keys and restores the compiled
  `device_posix_tz`.
- If anything else rewrites `TZ` (for example a future ESPHome time component
  that applies its own compiled zone), the idle task puts the saved zone back
  and logs a warning.
- The list has 31 common zones. Their POSIX rules were checked against tzdata
  offsets for January and July. Any other zone can be entered as a custom
  POSIX string.
- The schedule page and the landing page already take `tz_offset_min` from the
  device, so their times follow the new zone with no change.

## Code changes

- **`irrigoto.c`**
  - `irrigoto_get_inactivity_s()` / `irrigoto_set_inactivity_s()`: a
    seconds-resolution version of the inactivity setting. It uses the same NVS
    key (`pm_inact_s`) and the same 30–3600 s range the boot loader already
    accepts. The minutes accessors used by Home Assistant are unchanged.
  - Timezone: `tz_init()` (called from `irrigoto_init()`), `tz_set()`,
    `tz_guard()` (called from `esphome_idle_task`), and `tz_posix_valid()`,
    a sanity check that rejects junk. newlib would otherwise silently fall
    back to UTC.
  - New handler `api_system_handler`, registered for `GET` and `POST
    /api/system` (`max_uri_handlers` 76 → 78, `uris[]` static assert
    74 → 76):
    - `GET` → `{"ssid","password","connected","rssi","always_on","awake_s",
      "sleep_s","tz","tz_name","tz_default","tz_saved","now","tz_offset_min"}`.
      The SSID and password come from `esp_wifi_get_config(WIFI_IF_STA)`, and
      `connected`/`rssi` from `esp_wifi_sta_get_ap_info()`. Strings are
      JSON-escaped.
    - `POST always_on=0|1&awake_s=&sleep_s=` (any subset) sets the wake period
      through the existing setters and returns the new state.
    - `POST tz=<POSIX>&tz_name=<IANA>` saves and applies a zone. An empty `tz`
      returns to the firmware default. An invalid string gets a 400.
    - `POST ssid=&password=` checks the lengths (SSID required; password empty
      for an open network, otherwise 8–63 characters), replies, then saves and
      reboots.
- **`irrigoto.cpp`**: `irrigoto_wifi_save_sta()` bridge →
  `global_wifi_component->save_wifi_sta(ssid, password)`. That's ESPHome's own
  saved-network store, the one the captive portal writes. `WiFiComponent` loads
  it at boot in preference to the compiled credentials.
- **`irrigoto_api.h`**: declarations for the seconds accessors.
- **`html/landing.html`**:
  - the System settings button and modal, with Network, Wake period and
    Timezone sections
  - SSID field, and a password field with show/hide
  - connection status
  - wake period presets (Always on / 300 s / 300 s / Custom)
  - two number fields labelled **seconds**, with a line showing the resulting
    awake share
  - timezone picker, custom POSIX field, current device time and offset, and
    the phone-zone shortcut
  - a confirm dialog before saving a network
  - `landing_html.h` regenerated with `regen.py`
- **`esphome/irrigoto-core.yaml`**: comment noting that `device_posix_tz` is
  now the default, overridable from the web UI.

## Safety

- **Wrong network details:** after the reboot the device can't join, and
  ESPHome's fallback hotspot (`<device> Fallback`, captive portal) comes up
  as it does today. The details can be re-entered there. The confirm dialog
  says so.
- **Saved network overrides the compiled one** from then on, including after
  the "Reconnect to network" reboot. To go back to the compiled network, save
  it again in the modal (or reflash).
- **The password is shown to anyone who can reach the device's web UI** on
  the LAN. That matches the rest of this unauthenticated UI (which can already
  run the valve). The endpoint sends no CORS header, so other websites can't
  read it from a visitor's browser.
- **Wrong timezone:** schedules fire at the wrong wall-clock time until it's
  corrected. The modal shows the device's current local time next to the
  picker so a mistake is visible straight away.

## Compatibility

- Nothing changes until someone uses the modal. A device with no saved zone
  keeps using `device_posix_tz`.
- Home Assistant's "Inactivity Timeout" entity still works in minutes. A
  sub-minute value set here shows as the whole-minute part there (e.g. 90 s
  shows as 1 min).
- ESPHome's own `time:` component keeps its compiled `device_timezone`. That
  only affects ESPHome-side formatting, not the schedule executor.

## Testing

Done:
- Browser test against a mock API, at phone width:
  - load shows the current SSID and password (masked, with show/hide) and the
    connection status
  - Always on, the 300/300 preset and a custom 90 s / 20 s (clamped to 30 s)
    each post the right values
  - a password under 8 characters is refused
  - saving a network posts the SSID and password
  - timezone: the firmware default is shown, and **Use it** picks the phone's
    zone (America/Chicago). A list zone, a custom string and the firmware
    default each post the right `tz`/`tz_name`.
  - no JS errors, and no horizontal scroll
- The new C block compiles with host gcc against stubs. `tz_posix_valid()`
  accepts every rule in the list plus `<+0530>-5:30`, and rejects `EST`,
  `5EST` and strings with spaces.

Still to do on a device:
- The firmware build compiles.
- Always on: the device stays up past the awake time.
- Custom 60 s / 60 s: the log shows sleep after 60 s idle and a 60 s nap.
- Change the network to a second SSID: the device reboots and joins it.
- Enter a wrong password: the fallback hotspot appears. Fix the details there,
  and the device rejoins.
- Pick another zone: `/api/schedule` `tz_offset_min` changes, the next run
  time shifts accordingly, and the setting survives a reboot and a deep-sleep
  wake. Check the log for no repeated "TZ was changed ... elsewhere" warnings
  after HA time syncs.
