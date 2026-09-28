# The schedule does not run without Home Assistant

## What happens

A unit with no Home Assistant silently never waters on schedule.

The only autonomous time source is the `homeassistant` time platform. On a
standalone unit the clock comes solely from a browser POSTing `/api/time` on
page load -- so it is correct whenever someone is looking at the device, which
is exactly when it does not matter. Restart it unattended and it comes up with
`time(NULL)` at zero. Every schedule path then bails on the same guard:

```c
if (now < 1700000000) goto sleep_poll;   // clock-based path needs time
```

No firing. No catch-up. And no schedule-aware sleep either, because
`irrigoto_sleep_now_with_reason()` is behind the same `now > 1700000000`
check -- so the device does not even shorten its nap to wake for the run.

Observed on a standalone unit: a 06:00 daily entry did not run, and was
recorded as **MISSED at 08:36 the next morning** -- the moment the page was
opened and the browser handed the clock back. The miss record was correct; the
device simply had no way to know 06:00 had happened until someone visited.

## Why it was not obvious

Two things hid it.

The **comment** said what was intended rather than what was true:
*"Cloud-free: no NTP, no internet"*, describing `/api/time` as "the standalone
/ AP-mode time source". A browser that has to be opened is not a time source
for an unattended schedule, and treating it as one is what left the gap.

The **failure was silent**. The guard above is a bare `goto`. A unit with no
time source never watered and logged nothing at all about why. The first
evidence available to an owner is a dry lawn.

## The fix

**SNTP**, in `esphome/irrigoto-core.yaml`, which both `irrigoto.yaml` and
`irrigoto-4mb.yaml` include:

```yaml
time:
  - platform: homeassistant
    id: ha_time
  - platform: sntp
    id: sntp_time
    servers: [0.pool.ntp.org, 1.pool.ntp.org, 2.pool.ntp.org]
```

Both platforms set the same system clock, so this costs nothing where HA is
present -- whichever syncs first wins. Where there is no internet either
(AP-mode setup, isolated VLAN) SNTP never syncs and `/api/time` remains the
fallback, exactly as before. Nothing is taken away.

Verified on hardware: after a fresh flash with **no** `/api/time` posted, the
device's clock came up within **2 seconds** of the host.

**And say so when it is suspended.** The bare `goto` now warns, rate-limited
to once every five minutes, and only when there are entries actually being
held up:

```
W irrigoto: Schedule SUSPENDED: clock not set (1 entry waiting).
            Needs SNTP, Home Assistant, or a browser visit to set it.
```

A unit with no schedule is not waiting on anything, so it stays quiet.

## Cost

About **1.2 KB of DRAM** for the SNTP component, measured. On `main` that
lands at **95.9%** used -- roughly 7.4 KB free. That is tight, and worth
knowing before stacking anything else on top; b547 ("free 8.5 KB of DRAM") is
not on `main` and would more than pay for this.

## Not fixed here

`/api/system` reports two different timezone offsets on the affected unit --
`tz_offset_min: -420` (solved through `mktime`) against `tz_offset_lt_min:
-240` (libc `localtime_r`). libc is applying a stale numeric zone while
labelling it correctly. That is a separate defect with its own branch,
`fix/timezone`; it does not stop the schedule firing, it shifts what the times
mean.
