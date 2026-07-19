#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
var="$root/include/Airport/apple80211_var.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"

fail() {
    echo "Tahoe RSN_IE BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_RSN_IE                    46  // req_type' "$header" ||
    fail 'RSN_IE selector no longer resolves to 46'
grep -Fqx '#define APPLE80211_MAX_RSN_IE_LEN      257      // 255 + type and length bytes' "$var" ||
    fail 'RSN IE carrier capacity no longer resolves to 257 bytes'
grep -Fq 'u_int8_t     ie[ APPLE80211_MAX_RSN_IE_LEN ];' "$header" ||
    fail 'RSN IE public carrier no longer contains the bounded IE array'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_RSN_IE' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] ||
    fail 'BSD RSN_IE gate or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] ||
    fail 'BSD RSN_IE gate runs after the local dispatcher'

fence=$(printf '%s\n' "$bsd_bridge" |
    sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' ||
    fail 'BSD RSN_IE fence is not GET-only'
printf '%s\n' "$fence" |
    grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'BSD RSN_IE fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD RSN_IE fence inspects the nested carrier'
fi

producer=$(sed -n '/getRSN_IE(struct apple80211_rsn_ie_data \*data)/,/^}/p' "$source")
printf '%s\n' "$producer" | grep -Fq 'memcpy(data->ie,' ||
    fail 'local RSN_IE producer disappeared'
printf '%s\n' "$producer" | grep -Fq 'data->len =' ||
    fail 'local RSN_IE length publication disappeared'

setter=$(sed -n '/setRSN_IE(struct apple80211_rsn_ie_data \*data)/,/^}/p' "$source")
printf '%s\n' "$setter" |
    grep -Fq 'TahoeAssociationContracts::kPublicSetRsnIeReturn' ||
    fail 'RSN_IE SET no longer preserves the public fixed no-op result'
if printf '%s\n' "$setter" | grep -Fq 'data->'; then
    fail 'RSN_IE SET unexpectedly reads or writes its carrier'
fi

grep -Fq 'kIocRsnIe = 46' "$routes" ||
    fail 'Tahoe card route lost RSN_IE selector identity'
route_case=$(sed -n '/case kIocRsnIe:/,/return isSet;/p' "$routes")
printf '%s\n' "$route_case" | grep -Fq 'case kIocRsnIe:' ||
    fail 'Tahoe card route lost the RSN_IE SET-only case'
printf '%s\n' "$route_case" | grep -Fq 'return isSet;' ||
    fail 'Tahoe card route no longer leaves RSN_IE GET outside the card path'

for marker in 'storeAssocRsnIeOverride' 'setAssocRSNIE'; do
    grep -Fq "$marker" "$source" ||
        fail "hidden association RSN handoff disappeared: $marker"
done

printf 'Tahoe RSN_IE BSD carrier fence: PASS\n'
