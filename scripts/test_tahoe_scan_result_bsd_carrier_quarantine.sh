#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
v2="$root/AirportItlwm/AirportItlwmV2.cpp"

fail() {
    echo "Tahoe SCAN_RESULT BSD carrier quarantine: $1" >&2
    exit 1
}

grep -Eq '^#define[[:space:]]+APPLE80211_IOC_SCAN_RESULT[[:space:]]+11([[:space:]]|$)' "$header" ||
    fail 'selector no longer resolves to 11'

grep -Fq 'sizeof(struct apple80211_scan_result) == 0x8d8' \
    "$root/include/Airport/apple80211_var.h" ||
    fail 'Tahoe scan-result carrier size assertion disappeared'

dispatcher=$(awk '
/processApple80211Ioctl\(UInt cmd, apple80211req \*req\)/ { inside = 1 }
inside {
    print
    line = $0
    opens = gsub(/\{/, "{", line)
    closes = gsub(/\}/, "}", line)
    depth += opens - closes
    if (opens > 0)
        opened = 1
    if (opened && depth == 0)
        exit
}
' "$source")

case_count=$(printf '%s\n' "$dispatcher" | grep -Fc 'case APPLE80211_IOC_SCAN_RESULT:' || true)
[ "$case_count" -eq 1 ] || fail 'selector case is not unique'

scan_case=$(printf '%s\n' "$dispatcher" |
    sed -n '/case APPLE80211_IOC_SCAN_RESULT:/,/case APPLE80211_IOC_CHANNEL:/p')
tahoe_scan_case=$(printf '%s\n' "$scan_case" |
    sed -n '/#if __IO80211_TARGET >= __MAC_26_0/,/#else/p')
printf '%s\n' "$tahoe_scan_case" | grep -Fq '#if __IO80211_TARGET >= __MAC_26_0' ||
    fail 'Tahoe quarantine guard is missing'
printf '%s\n' "$tahoe_scan_case" | grep -Fq 'return kIOReturnUnsupported;' ||
    fail 'BSD scan case no longer falls through to the family'
if printf '%s\n' "$tahoe_scan_case" | grep -Eq 'getSCAN_RESULT\(|req->req_(data|len|val)'; then
    fail 'BSD scan case dereferences or inspects the nested carrier'
fi
printf '%s\n' "$scan_case" | grep -Fq '#else' ||
    fail 'pre-Tahoe SCAN_RESULT path was unexpectedly removed'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
current_network_gate_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_CURRENT_NETWORK' | cut -d: -f1)
bridge_dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
[ -n "$current_network_gate_line" ] && [ -n "$bridge_dispatch_line" ] ||
    fail 'CURRENT_NETWORK BSD gate is missing'
[ "$current_network_gate_line" -lt "$bridge_dispatch_line" ] ||
    fail 'CURRENT_NETWORK gate runs after the local dispatcher'
printf '%s\n' "$bsd_bridge" | grep -Fq '#if __IO80211_TARGET >= __MAC_26_0' ||
    fail 'CURRENT_NETWORK Tahoe guard is missing'
printf '%s\n' "$bsd_bridge" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'CURRENT_NETWORK BSD gate no longer delegates to IO80211Family'
printf '%s\n' "$bsd_bridge" | grep -Fq 'if (ret != kIOReturnUnsupported)' ||
    fail 'BSD bridge no longer recognizes local fallthrough'
printf '%s\n' "$bsd_bridge" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'BSD bridge no longer delegates unsupported requests to IO80211Family'

if grep -Fq 'APPLE80211_IOC_SCAN_RESULT' "$routes"; then
    fail 'controller-side Tahoe route unexpectedly owns SCAN_RESULT'
fi
grep -Fq 'kIocCurrentNetwork = 103' "$routes" ||
    fail 'controller current-network route disappeared'
grep -Fq 'case kIocCurrentNetwork:' "$routes" ||
    fail 'controller current-network route lost its GET admission'

grep -Fq 'APPLE80211_M_WCL_SCAN_RESULT' "$v2" ||
    fail 'WCL scan-result publication path disappeared'

printf 'Tahoe SCAN_RESULT BSD carrier quarantine: PASS\n'
