#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
controller="$root/AirportItlwm/AirportItlwmV2.cpp"
evidence="$root/docs/reference/CR-614-tahoe-bsd-max-nss-for-ap-carrier-fence-20260719.md"
artifact="$root/docs/reference/artifacts/tahoe-max-nss-for-ap-bsd-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe MAX_NSS_FOR_AP BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_MAX_NSS_FOR_AP 259' "$header" || fail 'selector no longer resolves to 259'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" || fail '25C56 BootKC identity is missing'
grep -Fq 'public_req_len=0x4' "$artifact" || fail 'family four-byte public carrier evidence is missing'
grep -Fq 'legacy_get_ioctl=0xc02869c9' "$artifact" || fail 'family legacy GET transport evidence is missing'
grep -Fq 'selector=0x103' "$artifact" || fail 'family MAX_NSS selector evidence is missing'
grep -Fq 'legacy_carrier_len=0x8' "$artifact" || fail 'family eight-byte internal carrier evidence is missing'
grep -Fq 'fallback_condition=(handled & 1)==0 || transport_status==0xe082280f' "$artifact" || fail 'family fallback condition is missing'
grep -Fq 'selector_gate=0x103' "$artifact" || fail 'public MAX_NSS selector-gate evidence is missing'
grep -Fq 'interface_vtable_tail_slot=0xf98' "$artifact" || fail 'public MAX_NSS interface-route evidence is missing'
grep -Fq 'private_raw_sha256=8590df699d99ff22ccbb94aaa11fa7cc2dfd114e70e0e5bacb57263e202b6c5d' "$artifact" || fail 'private MAX_NSS wrapper bytes are not pinned'
grep -Fq 'public_raw_sha256=9f8e32761d20854ad79c4d31aea3aecee079a4892ae5f47a2b5182523dc8b97f' "$artifact" || fail 'public MAX_NSS leaf bytes are not pinned'
grep -Fq 'public four-byte carrier' "$evidence" || fail 'evidence no longer explains the public carrier boundary'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'req->req_type == APPLE80211_IOC_MAX_NSS_FOR_AP' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] && [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] || fail 'BSD GET fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] || fail 'BSD GET fence runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] && [ "$fence_line" -lt "$tahoe_endif_line" ] || fail 'BSD GET fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" | sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' || fail 'BSD MAX_NSS fence is not GET-only'
printf '%s\n' "$fence" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' || fail 'BSD MAX_NSS fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD MAX_NSS fence inspects the nested carrier'
fi

producer=$(sed -n '/getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data \*data)/,/^}/p' "$source")
printf '%s\n' "$producer" | grep -Fq 'if (data == nullptr)' || fail 'kernel-owned producer lost its null guard'
printf '%s\n' "$producer" | grep -Fq 'memset(raw, 0, 8);' || fail 'kernel-owned producer no longer clears its eight-byte carrier'
printf '%s\n' "$producer" | grep -Fq '*reinterpret_cast<uint32_t *>(raw + 4)' || fail 'kernel-owned producer no longer writes its trailing NSS dword'
printf '%s\n' "$producer" | grep -Fq 'return kIOReturnSuccess;' || fail 'kernel-owned producer success contract disappeared'

dispatcher=$(sed -n '/case APPLE80211_IOC_MAX_NSS_FOR_AP:/,/case APPLE80211_IOC_BTCOEX_2G_CHAIN_DISABLE:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'return (cmd == SIOCGA80211) ? getMAX_NSS_FOR_AP((apple80211_btcoex_max_nss_for_ap_data *)req->req_data)' || fail 'kernel-owned local GET route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq ': kIOReturnUnsupported;' || fail 'MAX_NSS SET fallthrough changed unexpectedly'

grep -Fq 'kIocMaxNssForAp = 259,' "$routes" || fail 'controller selector-259 route disappeared'
route_group=$(sed -n '/case kIocSsid:/,/case kIocBtCoexFlags:/p' "$routes")
printf '%s\n' "$route_group" | grep -Fq 'case kIocMaxNssForAp:' || fail 'controller selector-259 route is no longer retained'
printf '%s\n' "$route_group" | grep -Fq 'return !isSet;' || fail 'controller selector-259 route is no longer GET-only'
grep -Fq 'return sky->processApple80211Ioctl(cmd, req);' "$controller" || fail 'kernel-owned card-specific bridge no longer bypasses BSD ingress'

printf 'Tahoe MAX_NSS_FOR_AP BSD carrier fence: PASS\n'
