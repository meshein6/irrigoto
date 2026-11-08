#!/usr/bin/env bash
#
# push.sh — build and OTA-push irrigoto, without disturbing stored state or
# the user's power settings.
#
# OTA writes only to the inactive app partition (ota_0 / ota_1). The LittleFS
# partition at 0x370000 holds zones, pressure/throw calibration and schedules,
# and is never touched by an OTA — calibration survives. The only things that
# wipe it are a full-flash erase over USB, or a change to the partition table.
#
# Before flashing it snapshots the device's calibration and zone state to
# ./cal-backups/, so a bad build is recoverable without redoing the walk.
#
#   Usage:  ./push.sh                 # finds the unit over mDNS
#           HOST=192.168.86.45 ./push.sh    # or an explicit ip/name
#           YAML=esphome/irrigoto.yaml ./push.sh
#
# WHY THIS SCRIPT EXISTS RATHER THAN A BARE `esphome run`
#
# The unit deep-sleeps on an inactivity timer, so a hand-run OTA loses the
# race and dies with "No route to host" partway through. It has to be held
# awake for the compile+upload and then put back.
#
# "Put back" is the part that bit us: /api/auto_sleep and the System settings
# modal's always_on are INVERSES of each other --
#
#     /api/auto_sleep on=1        -> auto-sleep ENABLED   (device sleeps)
#     /api/system     always_on=1 -> auto-sleep DISABLED  (device stays up)
#
# so a flash routine that ends with a blanket `on=1` silently overrides a
# deliberate "Always on" every single time. It is invisible in any one flash
# and obvious over ten. This script therefore READS the current state first
# and restores exactly that, whatever it was.
set -euo pipefail

VENV="${VENV:-$HOME/esphome-env}"
YAML="${YAML:-esphome/irrigoto-4mb.yaml}"
HOST="${HOST:-}"          # empty = find the unit over mDNS (see below)
BACKUP_DIR="${BACKUP_DIR:-cal-backups}"

ESPHOME="$VENV/bin/esphome"
STAMP="$(date +%Y%m%d-%H%M%S)"
WAIT_SECS="${WAIT_SECS:-600}"     # how long to wait for a sleeping unit

[[ -x "$ESPHOME" ]] || { echo "esphome not found at $ESPHOME" >&2; exit 1; }
[[ -f "$YAML"    ]] || { echo "config not found: $YAML" >&2; exit 1; }

api() { curl -s -m 8 "http://$HOST/$1" 2>/dev/null; }

# ── Finding the unit ─────────────────────────────────────────────────────────
# HOST used to be a hardcoded IP, and DHCP moved the unit out from under it:
# the script still said 192.168.86.21 long after the unit had become .45, and
# by then another host held .21 -- it answered pings but not HTTP, so the
# failure read as "the unit is ignoring its own API" rather than "wrong
# address". An IP is the wrong thing to pin.
#
# So: discover it. ESPHome advertises _esphomelib._tcp over mDNS, and the
# unit's name carries its MAC suffix (irrigoto4-1ca25c), which is why plain
# "irrigoto4.local" does NOT resolve -- the instance name has to be browsed
# for, not guessed. An explicit HOST=<ip-or-name> still wins and skips this.
#
# Discovery lives INSIDE the wait loop below, not ahead of it: a sleeping unit
# advertises nothing at all, so browsing once up front would abort on exactly
# the case this script was written to handle. Each pass browses afresh until
# WAIT_SECS runs out.
BROWSE_SECS="${BROWSE_SECS:-5}"

responds() { curl -s -m 8 "http://$1/api/status" 2>/dev/null | grep -q fw_build; }

# Instance names of irrigoto units currently advertising, as .local hostnames.
discover_hosts() {
  local out pid
  out="$(mktemp -t irrigoto-mdns)"
  # dns-sd never exits on its own; let it browse, then kill it.
  dns-sd -B _esphomelib._tcp >"$out" 2>/dev/null &
  pid=$!
  sleep "$BROWSE_SECS"
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  awk '/ Add / && $NF ~ /^irrigoto/ { print $NF ".local" }' "$out" | sort -u
  rm -f "$out"
}

# ── Wait for the unit ────────────────────────────────────────────────────────
if [[ -n "$HOST" ]]; then
  HOST_GIVEN=1
  echo "==> Waiting for $HOST (up to ${WAIT_SECS}s — it may be asleep)"
else
  HOST_GIVEN=0
  echo "==> Looking for an irrigoto unit (up to ${WAIT_SECS}s — it may be asleep)"
fi
deadline=$(( $(date +%s) + WAIT_SECS ))
until [[ -n "$HOST" ]] && responds "$HOST"; do
  (( $(date +%s) < deadline )) || {
    echo "    ${HOST:-no unit} never came up; aborting" >&2
    (( HOST_GIVEN )) || \
      echo "    (mDNS doesn't cross subnets; if it's on another one, pass HOST=<ip>)" >&2
    exit 1
  }
  if [[ -n "$HOST" ]]; then
    sleep 10            # named unit, just not up yet
  else
    for cand in $(discover_hosts); do
      echo "    found $cand"
      HOST="$cand"
      responds "$HOST" && break
      HOST=""           # advertised but not answering: some other esphome node
    done
    [[ -n "$HOST" ]] || sleep 5
  fi
done
echo "==> Unit: $HOST"
echo "    up: $(api "api/status")"

# ── Remember the power setting BEFORE we touch it ────────────────────────────
# {"auto_sleep":true} means auto-sleep is on, i.e. NOT always-on.
WAS_AUTO_SLEEP="$(api "api/auto_sleep" | grep -o 'true\|false' | head -1)"
WAS_AUTO_SLEEP="${WAS_AUTO_SLEEP:-true}"
echo "==> auto_sleep was: $WAS_AUTO_SLEEP  (restored verbatim at the end)"

restore_power() {
  local want=0
  [[ "$WAS_AUTO_SLEEP" == "true" ]] && want=1
  echo "==> Restoring auto_sleep=$WAS_AUTO_SLEEP"
  api "api/auto_sleep" >/dev/null || true
  curl -s -m 8 -X POST -d "on=$want" "http://$HOST/api/auto_sleep" >/dev/null || true
}
trap restore_power EXIT   # also runs if the build or upload fails

# ── Hold it awake for the compile + upload ───────────────────────────────────
curl -s -m 8 -X POST -d 'on=0' "http://$HOST/api/auto_sleep" >/dev/null || true

# ── Snapshot recoverable state ───────────────────────────────────────────────
mkdir -p "$BACKUP_DIR"
for p in all cal zones status system; do
  api "api/$p" > "$BACKUP_DIR/${STAMP}_api-$p.json" || true
done
echo "==> Backed up device state to $BACKUP_DIR/${STAMP}_*"

# ── Build + OTA ──────────────────────────────────────────────────────────────
echo "==> Building and uploading $YAML -> $HOST"
"$ESPHOME" run "$YAML" --device "$HOST" --no-logs

# ── Confirm what actually landed ─────────────────────────────────────────────
sleep 12
echo "==> Now running: $(api "api/status")"
echo "==> Calibration: $(api "api/cal" | head -c 60)..."
echo "Done."
