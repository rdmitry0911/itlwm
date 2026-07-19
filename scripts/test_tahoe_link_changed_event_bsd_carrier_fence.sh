#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"

fail() {
    echo "Tahoe LINK_CHANGED_EVENT_DATA BSD carrier fence: $1" >&2
    exit 1
}

grep -Eq '^#define[[:space:]]+APPLE80211_IOC_LINK_CHANGED_EVENT_DATA[[:space:]]+156([[:space:]]|$)' "$header" ||
    fail 'selector no longer resolves to 156'
grep -Fq 'sizeof(struct apple80211_link_changed_event_data) == 0x20' "$header" ||
    fail 'link-changed carrier is no longer 32 bytes'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_LINK_CHANGED_EVENT_DATA' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] ||
    fail 'BSD link-event gate or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] ||
    fail 'BSD link-event gate runs after the local dispatcher'

fence=$(printf '%s\n' "$bsd_bridge" |
    sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' ||
    fail 'BSD link-event fence is not GET-only'
printf '%s\n' "$fence" |
    grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'BSD link-event fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD link-event fence inspects the nested carrier'
fi

producer=$(sed -n '/getLINK_CHANGED_EVENT_DATA(struct apple80211_link_changed_event_data \*ed)/,/^}/p' "$source")
printf '%s\n' "$producer" |
    grep -Fq 'bzero(ed, sizeof(apple80211_link_changed_event_data));' ||
    fail 'local 32-byte link-event producer disappeared'
printf '%s\n' "$producer" | grep -Fq 'memcpy(ed->last_assoc,' ||
    fail 'local link-event snapshot no longer copies association identity'

if grep -Fq 'LINK_CHANGED_EVENT_DATA' "$routes"; then
    fail 'controller-side Tahoe route unexpectedly owns link-event GET'
fi
grep -Fq 'APPLE80211_M_LINK_CHANGED' "$source" ||
    fail 'independent Tahoe link-change publisher disappeared'
grep -Fq 'setLinkStateInternal(IO80211LinkState state' "$source" ||
    fail 'link-state transition publisher disappeared'

printf 'Tahoe LINK_CHANGED_EVENT_DATA BSD carrier fence: PASS\n'
