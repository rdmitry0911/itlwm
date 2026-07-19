#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"

fail() {
    echo "Tahoe ASSOCIATION_STATUS BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_ASSOCIATION_STATUS        50  // req_type' "$header" ||
    fail 'selector no longer resolves to 50'
assoc_layout=$(sed -n '/struct apple80211_assoc_status_data/,/^};/p' "$header")
printf '%s\n' "$assoc_layout" | grep -Fq 'u_int32_t    version;' ||
    fail 'legacy association-status version field disappeared'
printf '%s\n' "$assoc_layout" | grep -Fq 'u_int32_t    status;' ||
    fail 'legacy association-status status field disappeared'
grep -Fq 'sizeof(struct apple80211_assoc_status_data) == 8' "$header" ||
    fail 'legacy association-status carrier is no longer asserted as 8 bytes'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_ASSOCIATION_STATUS' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] &&
    [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] ||
    fail 'BSD association-status gate or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] ||
    fail 'BSD association-status gate runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] &&
    [ "$fence_line" -lt "$tahoe_endif_line" ] ||
    fail 'BSD association-status fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" |
    sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' ||
    fail 'BSD association-status fence is not GET-only'
printf '%s\n' "$fence" |
    grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'BSD association-status fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD association-status fence inspects the nested carrier'
fi

producer=$(sed -n '/getASSOCIATION_STATUS(struct apple80211_assoc_status_data \*hv)/,/^}/p' "$source")
printf '%s\n' "$producer" | grep -Fq 'memset(hv, 0, sizeof(*hv));' ||
    fail 'local legacy association-status producer disappeared'
printf '%s\n' "$producer" | grep -Fq 'hv->version = APPLE80211_VERSION;' ||
    fail 'local legacy association-status version publication disappeared'
printf '%s\n' "$producer" | grep -Fq 'hv->status =' ||
    fail 'local legacy association-status status publication disappeared'

dispatcher=$(sed -n '/case APPLE80211_IOC_ASSOCIATION_STATUS:/,/case APPLE80211_IOC_COUNTRY_CODE:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'cmd == SIOCGA80211' ||
    fail 'kernel-owned local GET branch disappeared'
printf '%s\n' "$dispatcher" |
    grep -Fq 'return (cmd == SIOCGA80211) ? getASSOCIATION_STATUS((apple80211_assoc_status_data *)req->req_data)' ||
    fail 'local GET case shape changed'
printf '%s\n' "$dispatcher" | grep -Fq ': kIOReturnUnsupported;' ||
    fail 'selector SET no longer returns unsupported to the BSD bridge'

ret_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'if (ret != kIOReturnUnsupported)' | cut -d: -f1)
fallback_line=$(printf '%s\n' "$bsd_bridge" |
    /usr/bin/awk -v after="$ret_line" 'NR > after &&
        /return super::processBSDCommand\(interface, cmd, data\);/ {
            print NR; exit
        }')
[ -n "$ret_line" ] && [ -n "$fallback_line" ] ||
    fail 'BSD unsupported fallthrough is missing'
[ "$ret_line" -lt "$fallback_line" ] ||
    fail 'BSD unsupported result no longer reaches family fallback'

if grep -Fq 'ASSOCIATION_STATUS' "$routes"; then
    fail 'controller-side Tahoe route unexpectedly owns association status'
fi

printf 'Tahoe ASSOCIATION_STATUS BSD carrier fence: PASS\n'
