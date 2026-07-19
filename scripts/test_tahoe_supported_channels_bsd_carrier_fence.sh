#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
var="$root/include/Airport/apple80211_var.h"
net80211="$root/itl80211/openbsd/net80211/ieee80211_var.h"

fail() {
    echo "Tahoe SUPPORTED_CHANNELS BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_SUPPORTED_CHANNELS        27  // req_type' "$header" ||
    fail 'SUPPORTED_CHANNELS selector no longer resolves to 27'
grep -Fqx '#define APPLE80211_IOC_HW_SUPPORTED_CHANNELS 254' "$header" ||
    fail 'HW_SUPPORTED_CHANNELS selector no longer resolves to 254'
grep -Fqx '#define APPLE80211_MAX_CHANNELS        128' "$var" ||
    fail 'public channel-list capacity no longer resolves to 128'
grep -Eq '^#define[[:space:]]+IEEE80211_CHAN_MAX[[:space:]]+255([[:space:]]|$)' "$net80211" ||
    fail 'net80211 channel traversal bound no longer resolves to 255'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_SUPPORTED_CHANNELS' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] ||
    fail 'BSD channel-list gate or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] ||
    fail 'BSD channel-list gate runs after the local dispatcher'

fence=$(printf '%s\n' "$bsd_bridge" |
    sed -n "$((fence_line - 2)),$((fence_line + 13))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' ||
    fail 'BSD channel-list fence is not GET-only'
printf '%s\n' "$fence" | grep -Fq 'APPLE80211_IOC_HW_SUPPORTED_CHANNELS' ||
    fail 'HW_SUPPORTED_CHANNELS is not covered by the BSD fence'
printf '%s\n' "$fence" |
    grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'BSD channel-list fence does not delegate to IO80211Family'

producer=$(sed -n '/getSUPPORTED_CHANNELS(struct apple80211_sup_channel_data \*ad)/,/^}/p' "$source")
printf '%s\n' "$producer" |
    grep -Fq 'for (int i = 0; i < IEEE80211_CHAN_MAX; i++)' ||
    fail 'local channel-list producer no longer traverses net80211 slots'
printf '%s\n' "$producer" |
    grep -Fq 'ad->supported_channels[ad->num_channels]' ||
    fail 'local channel-list producer no longer indexes its output carrier'
if printf '%s\n' "$producer" | grep -Fq 'APPLE80211_MAX_CHANNELS'; then
    fail 'local channel-list producer unexpectedly gained an entry bound; reassess the fence'
fi

printf 'Tahoe SUPPORTED_CHANNELS BSD carrier fence: PASS\n'
