#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"

require_source() {
  if ! grep -F -q -- "$1" "$source"; then
    echo "missing Tahoe supported-channel contract: $1" >&2
    exit 1
  fi
}

extract_body() {
  awk -v signature="$1" '
    index($0, signature) { inside = 1 }
    inside { print }
    inside && /^}/ { exit }
  ' "$source"
}

require_source 'static constexpr IOReturn kApple80211ErrInvalidArgumentRaw = 0x16;'

route=$(sed -n '/case APPLE80211_IOC_SUPPORTED_CHANNELS:/,/case APPLE80211_IOC_COUNTRY_CHANNELS:/p' "$source")
printf '%s\n' "$route" | grep -F -q 'getSUPPORTED_CHANNELS((apple80211_sup_channel_data *)req->req_data)'
printf '%s\n' "$route" | grep -F -q 'getHW_SUPPORTED_CHANNELS((apple80211_sup_channel_data *)req->req_data)'

supported=$(extract_body 'getSUPPORTED_CHANNELS(struct apple80211_sup_channel_data *ad)')
hardware=$(extract_body 'getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *data)')

for body in "$supported" "$hardware"; do
  printf '%s\n' "$body" | grep -F -q 'return kApple80211ErrInvalidArgumentRaw;'
  printf '%s\n' "$body" | grep -F -q 'return kIOReturnUnsupported;'
  if printf '%s\n' "$body" | grep -Eq 'ic_channels|supported_channels|num_channels|for \('; then
    echo 'supported-channel quarantine must not mutate or enumerate the public carrier' >&2
    exit 1
  fi
done

if printf '%s\n' "$hardware" | grep -F -q 'getSUPPORTED_CHANNELS(data)'; then
  echo 'hardware-supported channel selector must retain its own owner boundary' >&2
  exit 1
fi
