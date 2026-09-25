# Fix: rain delay reports success when it wasn't applied; status shown far from the buttons

**Branch:** `fix/rain-delay-feedback` · **Type:** fix · **Status:** planned,
low priority. The buttons work in normal use; these are edge cases found while
checking.

## Summary

Two small problems on the device's Schedule page rain delay, plus one
hardening idea for the HA sync.

## 1. Success reported when nothing was set

- `POST /api/schedule/delay hours=N` → `irrigoto_schedule_set_delay_hours()`
  **returns without doing anything** if the device clock isn't set
  (`time(NULL) < 1700000000`; it logs "time not synced — ignoring").
- `api_schedule_delay_handler()` still answers `{"ok":true}`. The page then
  says "Rain delay set for 6 h." while the label stays **off**.
- The Schedule page pushes the phone's clock on load (`/api/time`,
  fire-and-forget), so this only happens when a button is tapped before that
  lands, or when the time push failed.

**Change:**
- `irrigoto_schedule_set_delay_hours()` returns `bool`, and the handler
  returns `{"ok":false,"error":"device time not set"}` when it's false.
- Better still, accept an optional `epoch=` with the request and set the clock
  first (same rules as `/api/time`). `setDelay()` in `schedule.html` always
  sends it.

## 2. Status message in the wrong place

- `setDelay()` writes its result to `#save-status` (bottom of the page, next to
  Save), not to `#delay-status` under the rain delay buttons. On a phone, a tap
  looks like nothing happened.

**Change:** write to `#delay-status`. Disable the buttons and show "Setting…"
while the request is in flight.

## 3. (Optional) HA sync revert on clock skew

- The HA `sync_rain_delay` automation (upstream b524) pushes only if HA's
  stamp is newer than the device's (`ha_lm > dev_lm`). Otherwise it pulls the
  device's value into HA.
- If the device clock (set from a phone, no NTP) runs ahead of HA's, a fresh HA
  change can be overwritten by the device's older value.
- **Change:** have HA push its clock to the device on wake (`/api/time`), so
  both stamps come from the same clock.

## Testing

- With the clock unset: tapping 6 h shows an error, or sets the clock and
  succeeds if `epoch=` is sent.
- The status appears under the buttons.
