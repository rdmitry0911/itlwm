#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
evidence="$root/docs/reference/CR-615-tahoe-bsd-nss-carrier-fence-20260719.md"
artifact="$root/docs/reference/artifacts/tahoe-nss-bsd-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe NSS BSD carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_NSS  353' "$header" || fail 'selector no longer resolves to 353'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" || fail '25C56 BootKC identity is missing'
grep -Fq 'public_req_len=0x4' "$artifact" || fail 'family four-byte public carrier evidence is missing'
grep -Fq 'legacy_get_ioctl=0xc02869c9' "$artifact" || fail 'family legacy GET transport evidence is missing'
grep -Fq 'selector=0x161' "$artifact" || fail 'family NSS selector evidence is missing'
grep -Fq 'legacy_carrier_len=0x8' "$artifact" || fail 'family eight-byte internal carrier evidence is missing'
grep -Fq 'fallback_condition=(handled & 1)==0 || transport_status==0xe082280f' "$artifact" || fail 'family fallback condition is missing'
grep -Fq 'selector_gate=0x161' "$artifact" || fail 'public NSS selector-gate evidence is missing'
grep -Fq 'interface_vtable_tail_slot=0xfe0' "$artifact" || fail 'public NSS interface-route evidence is missing'
grep -Fq 'private_raw_sha256=1cca15b420506b344c49cfcad0bc987e75003d1df222c2c3295bc40936501812' "$artifact" || fail 'private NSS wrapper bytes are not pinned'
grep -Fq 'public_raw_sha256=7601b0d15612283cc449ad6e270f0d63055841aeb7acbff27dad429ac78d6bf1' "$artifact" || fail 'public NSS leaf bytes are not pinned'
grep -Fq 'public four-byte carrier' "$evidence" || fail 'evidence no longer explains the public carrier boundary'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'req->req_type == APPLE80211_IOC_NSS' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] && [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] || fail 'BSD GET fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] || fail 'BSD GET fence runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] && [ "$fence_line" -lt "$tahoe_endif_line" ] || fail 'BSD GET fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" | sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211GetIoctl(cmd) &&' || fail 'BSD NSS fence is not GET-only'
printf '%s\n' "$fence" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' || fail 'BSD NSS fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD NSS fence inspects the nested carrier'
fi

producer=$(sed -n '/getNSS(struct apple80211_nss_data \*data)/,/^}/p' "$source")
printf '%s\n' "$producer" | grep -Fq 'memset(data, 0, sizeof(*data));' || fail 'local producer no longer clears its complete carrier'
printf '%s\n' "$producer" | grep -Fq 'data->version = APPLE80211_VERSION;' || fail 'local producer no longer writes its version'
printf '%s\n' "$producer" | grep -Fq 'data->nss = fHalService->getDriverInfo()->getTxNSS();' || fail 'local producer no longer writes Tx NSS'
printf '%s\n' "$producer" | grep -Fq 'return kIOReturnSuccess;' || fail 'local producer success contract disappeared'

dispatcher=$(sed -n '/case APPLE80211_IOC_NSS:/,/case APPLE80211_IOC_BSS_BLACKLIST:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'return (cmd == SIOCGA80211) ? getNSS((apple80211_nss_data *)req->req_data)' || fail 'local GET route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq ': kIOReturnUnsupported;' || fail 'NSS SET fallthrough changed unexpectedly'
if grep -Eq 'kIocNss|kIocNSS|APPLE80211_IOC_NSS' "$routes"; then
    fail 'a Tahoe card-specific NSS route was added without fresh contract evidence'
fi

printf 'Tahoe NSS BSD carrier fence: PASS\n'
