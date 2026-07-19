#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
var="$root/include/Airport/apple80211_var.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
evidence="$root/docs/reference/artifacts/tahoe-ap-ie-list-bsd-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe AP_IE_LIST BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_AP_IE_LIST                48  // req_type' "$header" ||
    fail 'selector no longer resolves to 48'
grep -Fqx '#define APPLE80211_NETWORK_DATA_MAX_IE_LEN 1024' "$var" ||
    fail 'embedded AP IE capacity no longer resolves to 1024 bytes'
ap_ie_layout=$(sed -n '/struct apple80211_ap_ie_data/,/^};/p' "$header")
printf '%s\n' "$ap_ie_layout" | grep -Fq 'u_int32_t    version;' ||
    fail 'AP IE version field disappeared'
printf '%s\n' "$ap_ie_layout" | grep -Fq 'u_int32_t    len;' ||
    fail 'AP IE length field disappeared'
printf '%s\n' "$ap_ie_layout" |
    grep -Fq 'u_int8_t     ie_data[APPLE80211_NETWORK_DATA_MAX_IE_LEN];' ||
    fail 'Tahoe AP IE carrier no longer embeds its fixed output array'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$evidence" ||
    fail '25C56 evidence identity is missing'
grep -Fq 'public_req_len=0x808' "$evidence" ||
    fail 'family AP IE carrier length evidence is missing'
grep -Fq 'legacy_get_ioctl=0xc02869c9' "$evidence" ||
    fail 'family legacy GET transport evidence is missing'
grep -Fq 'selector=0x30' "$evidence" ||
    fail 'family AP IE selector evidence is missing'
grep -Fq 'selector_gate=0x30' "$evidence" ||
    fail 'public AP IE selector-gate evidence is missing'
grep -Fq 'interface_vtable_tail_slot=0x408' "$evidence" ||
    fail 'public AP IE interface-route evidence is missing'
grep -Fq 'private_wrapper_raw_sha256=7ee859b4cdf88b4d92128131da530fe93de3c7470e342c2685c27712e1fedfcc' "$evidence" ||
    fail 'private AP IE wrapper bytes are not pinned'
grep -Fq 'public_leaf_raw_sha256=f66632c2b64624af3c9131803f9fb63ff08da517661bef083657735e6c5b238a' "$evidence" ||
    fail 'public AP IE leaf bytes are not pinned'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_AP_IE_LIST' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] &&
    [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] ||
    fail 'BSD AP_IE_LIST fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] ||
    fail 'BSD AP_IE_LIST fence runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] &&
    [ "$fence_line" -lt "$tahoe_endif_line" ] ||
    fail 'BSD AP_IE_LIST fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" |
    sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' ||
    fail 'BSD AP_IE_LIST fence is not GET-only'
printf '%s\n' "$fence" |
    grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'BSD AP_IE_LIST fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD AP_IE_LIST fence inspects the nested carrier'
fi

producer=$(sed -n '/getAP_IE_LIST(struct apple80211_ap_ie_data \*data)/,/^}/p' "$source")
printf '%s\n' "$producer" | grep -Fq 'data->version = APPLE80211_VERSION;' ||
    fail 'kernel-owned AP IE producer lost version publication'
printf '%s\n' "$producer" | grep -Fq 'data->len = 0;' ||
    fail 'kernel-owned AP IE producer lost zero-length initialization'
printf '%s\n' "$producer" | grep -Fq 'memcpy(data->ie_data,' ||
    fail 'kernel-owned AP IE producer lost its bounded copy'

dispatcher=$(sed -n '/case APPLE80211_IOC_AP_IE_LIST:/,/case APPLE80211_IOC_BT_COEX_FLAGS:/p' "$source")
printf '%s\n' "$dispatcher" |
    grep -Fq 'return (cmd == SIOCGA80211) ? getAP_IE_LIST((apple80211_ap_ie_data *)req->req_data)' ||
    fail 'kernel-owned local GET route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq ': kIOReturnUnsupported;' ||
    fail 'AP IE LIST SET fallthrough changed unexpectedly'

if grep -Fq 'AP_IE_LIST' "$routes"; then
    fail 'controller-side Tahoe route unexpectedly owns AP_IE_LIST'
fi

printf 'Tahoe AP_IE_LIST BSD carrier fence: PASS\n'
