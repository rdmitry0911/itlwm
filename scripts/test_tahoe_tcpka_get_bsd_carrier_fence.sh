#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
evidence="$root/docs/reference/CR-616-tahoe-bsd-tcpka-get-carrier-fence-20260719.md"
artifact="$root/docs/reference/artifacts/tahoe-tcpka-get-bsd-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe TCPKA GET BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE 265' "$header" || fail 'selector no longer resolves to 265'
grep -Fq 'static_assert(sizeof(struct apple80211_offload_tcpka_enable_t) == 0x08,' "$header" || fail 'local TCPKA carrier is no longer eight bytes'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" || fail '25C56 BootKC identity is missing'
grep -Fq 'public_req_len=0x4' "$artifact" || fail 'family four-byte public carrier evidence is missing'
grep -Fq 'legacy_get_ioctl=0xc02869c9' "$artifact" || fail 'family legacy GET transport evidence is missing'
grep -Fq 'selector=0x109' "$artifact" || fail 'family TCPKA selector evidence is missing'
grep -Fq 'legacy_carrier_len=0x8' "$artifact" || fail 'family eight-byte internal carrier evidence is missing'
grep -Fq 'fallback_condition=(handled & 1)==0 || transport_status==0xe082280f' "$artifact" || fail 'family fallback condition is missing'
grep -Fq 'selector_gate=0x109' "$artifact" || fail 'public TCPKA selector-gate evidence is missing'
grep -Fq 'interface_vtable_tail_slot=0xfb0' "$artifact" || fail 'public TCPKA interface-route evidence is missing'
grep -Fq 'private_raw_sha256=8ab32c44e739e82479c32a7519157a99c677f21a386bf889bc9b257d48236472' "$artifact" || fail 'private TCPKA wrapper bytes are not pinned'
grep -Fq 'public_raw_sha256=0918caad78b3ea58064376e5a66651f825307b08cb88bce4395cf7871d4bf520' "$artifact" || fail 'public TCPKA leaf bytes are not pinned'
grep -Fq 'public four-byte carrier' "$evidence" || fail 'evidence no longer explains the public carrier boundary'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'req->req_type == APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] && [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] || fail 'BSD GET fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] || fail 'BSD GET fence runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] && [ "$fence_line" -lt "$tahoe_endif_line" ] || fail 'BSD GET fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" | sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' || fail 'BSD TCPKA fence is not GET-only'
printf '%s\n' "$fence" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' || fail 'BSD TCPKA fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD TCPKA fence inspects the nested carrier'
fi

producer=$(sed -n '/getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t \*data)/,/^}/p' "$source")
printf '%s\n' "$producer" | grep -Fq 'memset(data, 0, sizeof(*data));' || fail 'local producer no longer clears its complete carrier'
printf '%s\n' "$producer" | grep -Fq 'data->version = APPLE80211_VERSION;' || fail 'local producer no longer writes its version'
printf '%s\n' "$producer" | grep -Fq 'data->enabled = cachedTcpkaOffloadEnabled ? 1U : 0U;' || fail 'local producer no longer writes its enable dword'
printf '%s\n' "$producer" | grep -Fq 'return kIOReturnSuccess;' || fail 'local producer success contract disappeared'

dispatcher=$(sed -n '/case APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE:/,/case APPLE80211_IOC_OP_MODE:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'return getOFFLOAD_TCPKA_ENABLE((apple80211_offload_tcpka_enable_t *)req->req_data);' || fail 'local GET route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq 'return setOFFLOAD_TCPKA_ENABLE((apple80211_offload_tcpka_enable_t *)req->req_data);' || fail 'TCPKA SET route changed unexpectedly'
if grep -Eq 'kIoc.*Tcpka|kIoc.*TCPKA|APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE' "$routes"; then
    fail 'a Tahoe card-specific TCPKA route was added without fresh contract evidence'
fi

printf 'Tahoe TCPKA GET BSD carrier fence: PASS\n'
