#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
evidence="$root/docs/reference/CR-618-tahoe-bsd-rate-set-get-carrier-fence-20260719.md"
artifact="$root/docs/reference/artifacts/tahoe-rate-set-get-bsd-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe RATE_SET GET BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_RATE_SET                  32  // req_type' "$header" || fail 'selector no longer resolves to 32'
grep -Fq 'static_assert(sizeof(apple80211_rate_set_data) == 0xbc,' "$source" || fail 'local RATE_SET carrier is no longer pinned to 0xbc'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" || fail '25C56 BootKC identity is missing'
grep -Fq 'public_req_len=0xbc' "$artifact" || fail 'family 0xbc public carrier evidence is missing'
grep -Fq 'legacy_get_ioctl=0xc02869c9' "$artifact" || fail 'family legacy GET transport evidence is missing'
grep -Fq 'selector=0x20' "$artifact" || fail 'family RATE_SET selector evidence is missing'
grep -Fq 'legacy_carrier_len=0xbc' "$artifact" || fail 'family 0xbc WCL carrier evidence is missing'
grep -Fq 'carrier_mode=direct_req_data_no_local_alloc_or_copyout' "$artifact" || fail 'family direct-carrier evidence is missing'
grep -Fq 'fallback_condition=(handled & 1)==0 || transport_status==0xe082280f' "$artifact" || fail 'family fallback condition is missing'
grep -Fq 'selector_gate=0x20' "$artifact" || fail 'public RATE_SET selector-gate evidence is missing'
grep -Fq 'safe_metacast=IO80211NoneProtocol::gMetaClass' "$artifact" || fail 'public RATE_SET protocol evidence is missing'
grep -Fq 'interface_vtable_tail_slot=0x308' "$artifact" || fail 'public RATE_SET interface-route evidence is missing'
grep -Fq 'private_raw_sha256=d52f3f5844f6959b0ef14819f781bfb09694b248ad2d5bf6d5bb5b09767196a9' "$artifact" || fail 'private RATE_SET wrapper bytes are not pinned'
grep -Fq 'public_raw_sha256=4d6178ac18c2a62b8c11c1b0c52bbb169d12689f6205380cc755fb0a62ba7d55' "$artifact" || fail 'public RATE_SET leaf bytes are not pinned'
grep -Fq 'same 0xbc-byte caller buffer' "$evidence" || fail 'evidence no longer explains the carrier boundary'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'req->req_type == APPLE80211_IOC_RATE_SET' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] && [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] || fail 'BSD GET fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] || fail 'BSD GET fence runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] && [ "$fence_line" -lt "$tahoe_endif_line" ] || fail 'BSD GET fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" | sed -n "$((fence_line - 1)),$((fence_line + 13))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' || fail 'BSD RATE_SET fence is not GET-only'
printf '%s\n' "$fence" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' || fail 'BSD RATE_SET fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD RATE_SET fence inspects the nested carrier'
fi

producer=$(sed -n '/getRATE_SET(struct apple80211_rate_set_data \*ad)/,/^}/p' "$source")
printf '%s\n' "$producer" | grep -Fq 'memset(ad, 0, sizeof(*ad));' || fail 'local producer no longer clears its complete carrier'
printf '%s\n' "$producer" | grep -Fq 'ad->version = APPLE80211_VERSION;' || fail 'local producer no longer writes its version'
printf '%s\n' "$producer" | grep -Fq 'ad->num_rates = ic->ic_bss->ni_rates.rs_nrates;' || fail 'local producer no longer writes its rate count'
printf '%s\n' "$producer" | grep -Fq 'return kIOReturnSuccess;' || fail 'local producer success contract disappeared'

dispatcher=$(sed -n '/case APPLE80211_IOC_RATE_SET:/,/case APPLE80211_IOC_ROAM_PROFILE:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'if (cmd == SIOCSA80211)' || fail 'Tahoe RATE_SET SET fixed-stub gate disappeared'
printf '%s\n' "$dispatcher" | grep -Fq 'return static_cast<IOReturn>(0xe082280e);' || fail 'Tahoe RATE_SET SET fixed-stub status changed'
printf '%s\n' "$dispatcher" | grep -Fq 'return (cmd == SIOCGA80211) ? getRATE_SET((apple80211_rate_set_data *)req->req_data)' || fail 'local GET route disappeared'
if grep -Eq 'kIoc.*RateSet|kIoc.*RATE_SET|APPLE80211_IOC_RATE_SET' "$routes"; then
    fail 'a Tahoe card-specific RATE_SET route was added without fresh contract evidence'
fi

printf 'Tahoe RATE_SET GET BSD carrier fence: PASS\n'
