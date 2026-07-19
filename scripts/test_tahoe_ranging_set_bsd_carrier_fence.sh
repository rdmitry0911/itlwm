#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
evidence="$root/docs/reference/CR-617-tahoe-bsd-ranging-set-carrier-fence-20260719.md"
artifact="$root/docs/reference/artifacts/tahoe-ranging-authenticate-bsd-set-route-bootkc-current/raw.txt"

fail() {
    echo "Tahoe RANGING_AUTHENTICATE BSD SET carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_RANGING_AUTHENTICATE 243' "$header" || fail 'selector no longer resolves to 243'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" || fail '25C56 BootKC identity is missing'
grep -Fq 'external_route_raw_sha256=a21f852cd45428122be6a8012e5e85f251f5f374d4f3e5d96808826476a09315' "$artifact" || fail 'external BSD router bytes are not pinned'
grep -Fq 'selector=0xf3' "$artifact" || fail 'family ranging selector evidence is missing'
grep -Fq 'set_table_index=0xf2' "$artifact" || fail 'family SET-table index evidence is missing'
grep -Fq 'set_table_entry_raw_len=0x8' "$artifact" || fail 'family zero-entry length evidence is missing'
grep -Fq 'set_table_entry_raw_sha256=af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc' "$artifact" || fail 'family zero-entry bytes are not pinned'
grep -Fq 'set_table_entry_value=0x0' "$artifact" || fail 'family SET table no longer proves an absent handler'
grep -Fq 'set_absent_handler_status=0x66' "$artifact" || fail 'family absent-handler status is missing'
grep -Fq 'typed_leaf_raw_sha256=faf6cb82800f73effda67533c9e03b592395e59ee83f5bb115f0c42a9e2c8a92' "$artifact" || fail 'typed leaf bytes are not pinned'
grep -Fq 'before any nested carrier' "$evidence" || fail 'evidence no longer explains the raw BSD boundary'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'req->req_type == APPLE80211_IOC_RANGING_AUTHENTICATE' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" | grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] && [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] || fail 'BSD SET fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] || fail 'BSD SET fence runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] && [ "$fence_line" -lt "$tahoe_endif_line" ] || fail 'BSD SET fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" | sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211SetIoctl(cmd) &&' || fail 'BSD ranging fence is not SET-only'
printf '%s\n' "$fence" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' || fail 'BSD ranging fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD ranging fence inspects the nested carrier'
fi

dispatcher=$(sed -n '/case APPLE80211_IOC_RANGING_AUTHENTICATE:/,/case APPLE80211_IOC_BTCOEX_PROFILE:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'return (cmd == SIOCSA80211) ? setRANGING_AUTHENTICATE((apple80211_ranging_authenticate_request_t *)req->req_data)' || fail 'typed local SET route disappeared'
printf '%s\n' "$dispatcher" | grep -Fq ': kIOReturnUnsupported;' || fail 'ranging GET fallback changed unexpectedly'

typed=$(sed -n '/setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t \*data)/,/^}/p' "$source")
printf '%s\n' "$typed" | grep -Fq 'runSetRangingAuthenticate(' || fail 'typed helper no longer reaches its commander'
printf '%s\n' "$typed" | grep -Fq 'static_cast<IOReturn>(0xe0000001)' || fail 'typed null-owner contract disappeared'
if grep -Eq 'kIoc.*Ranging|kIoc.*RANGING|APPLE80211_IOC_RANGING_AUTHENTICATE' "$routes"; then
    fail 'a Tahoe card-specific ranging route was added without fresh contract evidence'
fi

printf 'Tahoe RANGING_AUTHENTICATE BSD SET carrier fence: PASS\n'
