# WiFi & power modal: change the network and the wake period from the device page

**Branch:** `feature/wifi-power-settings` · **Type:** feature · **Status:**
built. The modal was tested in a browser against a mock API; it still needs an
on-device build and test.

## Summary

A **WiFi & power** button in the landing page's Device card opens a modal
where you can:

1. See the WiFi name (SSID) and password the device is using, and change them.
   The device saves the new network and reboots to join it.
2. Set the **wake period in seconds**:
   - **Always on:** never sleeps.
   - **300 s / 300 s:** the default cycle.
   - **Custom:** any awake / sleep combination from 30 to 3600 seconds each.

Until now, the network was fixed at compile time (`!secret wifi_ssid`) and
could only be changed by reflashing or through the fallback hotspot. The wake
cycle could only be set from Home Assistant, and only in whole minutes for the
awake part.

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

## Code changes

- **`irrigoto.c`**
  - `irrigoto_get_inactivity_s()` / `irrigoto_set_inactivity_s()`: a
    seconds-resolution version of the inactivity setting. It uses the same NVS
    key (`pm_inact_s`) and the same 30–3600 s range the boot loader already
    accepts. The minutes accessors used by Home Assistant are unchanged.
  - New handler `api_wifi_power_handler`, registered for `GET` and `POST
    /api/wifi_power` (`max_uri_handlers` 76 → 78, `uris[]` static assert
    74 → 76):
    - `GET` → `{"ssid","password","connected","rssi","always_on","awake_s","sleep_s"}`.
      The SSID and password come from `esp_wifi_get_config(WIFI_IF_STA)`, and
      `connected`/`rssi` from `esp_wifi_sta_get_ap_info()`. Strings are
      JSON-escaped.
    - `POST always_on=0|1&awake_s=&sleep_s=` (any subset) sets the wake period
      through the existing setters and returns the new state.
    - `POST ssid=&password=` checks the lengths (SSID required; password empty
      for an open network, otherwise 8–63 characters), replies, then saves and
      reboots.
- **`irrigoto.cpp`**: `irrigoto_wifi_save_sta()` bridge →
  `global_wifi_component->save_wifi_sta(ssid, password)`. That's ESPHome's own
  saved-network store, the one the captive portal writes. `WiFiComponent` loads
  it at boot in preference to the compiled credentials.
- **`irrigoto_api.h`**: declarations for the seconds accessors.
- **`html/landing.html`**:
  - the WiFi & power button and modal
  - SSID field, and a password field with show/hide
  - connection status
  - wake period presets (Always on / 300 s / 300 s / Custom)
  - two number fields labelled **seconds**, with a line showing the resulting
    awake share
  - a confirm dialog before saving a network
  - `landing_html.h` regenerated with `regen.py`

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
  read it through a visitor's browser.

## Compatibility

- Nothing changes until someone uses the modal.
- Home Assistant's "Inactivity Timeout" entity still works in minutes. A
  sub-minute value set here shows as the whole-minute part there (e.g. 90 s
  shows as 1 min).

## Testing

Done: browser test against a mock API, at phone width:
- load shows the current SSID and password (masked, with show/hide) and the
  connection status
- Always on, the 300/300 preset and a custom 90 s / 20 s (clamped to 30 s) each
  post the right values
- a password under 8 characters is refused
- saving a network posts the SSID and password

Still to do on a device:
- The firmware build compiles.
- Always on: the device stays up past the awake time.
- Custom 60 s / 60 s: the log shows sleep after 60 s idle and a 60 s nap.
- Change the network to a second SSID: the device reboots and joins it.
- Enter a wrong password: the fallback hotspot appears. Fix the details there,
  and the device rejoins.
