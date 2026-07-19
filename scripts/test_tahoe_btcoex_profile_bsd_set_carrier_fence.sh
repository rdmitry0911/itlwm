#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
builders="$root/AirportItlwm/TahoePayloadBuilders.hpp"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
controller="$root/AirportItlwm/AirportItlwmV2.cpp"
evidence="$root/docs/reference/CR-613-tahoe-bsd-btcoex-profile-carrier-fence-20260719.md"
reference="$root/docs/reference/CR-479-btcoex-public-quarantine-20260713.md"
artifact="$root/docs/reference/artifacts/tahoe-btcoex-profile-bsd-set-carrier-bootkc-current/raw.txt"

fail() {
    echo "Tahoe BTCOEX_PROFILE BSD SET carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_BTCOEX_PROFILE 255' "$header" ||
    fail 'selector no longer resolves to 255'
grep -Fq 'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' "$artifact" ||
    fail '25C56 BootKC identity is missing'
grep -Fq 'private_req_len=0x38' "$artifact" ||
    fail 'family SET carrier length evidence is missing'
grep -Fq 'legacy_set_ioctl=0x802869c8' "$artifact" ||
    fail 'family legacy SET transport evidence is missing'
grep -Fq 'selector=0xff' "$artifact" ||
    fail 'family BTCOEX selector evidence is missing'
grep -Fq 'wcl_transport=IO80211Glue::sendIOUCToWcl' "$artifact" ||
    fail 'family WCL transport evidence is missing'
grep -Fq 'fallback_condition=(handled & 1)==0 || transport_status==0xe082280f' "$artifact" ||
    fail 'family fallback condition is missing'
grep -Fq 'selector_gate=0xff' "$artifact" ||
    fail 'public BTCOEX selector-gate evidence is missing'
grep -Fq 'interface_vtable_tail_slot=0x11c8' "$artifact" ||
    fail 'public BTCOEX interface-route evidence is missing'
grep -Fq 'private_raw_sha256=97df7690588f942c196ef0409de43f8abb29e0fc759c6f4c46aa66a044fc4fa0' "$artifact" ||
    fail 'private BTCOEX wrapper bytes are not pinned'
grep -Fq 'public_raw_sha256=e4279a2a82181b3243176a04ffd8f60b1181fa828b0699d1824f828754d1f945' "$artifact" ||
    fail 'public BTCOEX leaf bytes are not pinned'
grep -Fq '4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab' "$reference" ||
    fail '25C56 AppleBCMWLAN reference identity is missing'
grep -Fq 'setBTCOEX_PROFILE` at `0x1000186c8`' "$reference" ||
    fail 'Infra setter anchor is missing'
grep -Fq 'Core `setBTCOEX_PROFILE` is at `0x100124656`' "$reference" ||
    fail 'Core setter anchor is missing'
grep -Fq 'valid `0x38`-byte record' "$reference" ||
    fail 'reference profile carrier width is missing'
grep -Fq 'runIOVarSet("btc_profile")' "$reference" ||
    fail 'separate reference owner transport anchor is missing'
grep -Fq 'raw band `+0x03 >= 5`' "$evidence" ||
    fail 'local evidence lost raw band gate'
grep -Fq 'kernel-owned `apple80211req`' "$evidence" ||
    fail 'card-specific ownership boundary is missing'

builder=$(sed -n '/inline bool buildBtcoexProfile(/,/^}/p' "$builders")
printf '%s\n' "$builder" | grep -Fq 'payload->mode = *reinterpret_cast<const uint16_t *>(raw);' ||
    fail 'local profile builder no longer reads raw mode'
printf '%s\n' "$builder" | grep -Fq 'payload->band = raw[3];' ||
    fail 'local profile builder no longer reads raw band'
printf '%s\n' "$builder" | grep -Fq 'payload->profileIndex = raw[4];' ||
    fail 'local profile builder no longer reads raw profile index'
printf '%s\n' "$builder" |
    grep -Fq 'memcpy(payload->profileEntry, raw, sizeof(payload->profileEntry));' ||
    fail 'local profile builder no longer exposes its full-carrier copy'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
fence_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_BTCOEX_PROFILE' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$fence_line" ] && [ -n "$dispatch_line" ] &&
    [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] ||
    fail 'BSD SET fence or dispatcher is missing'
[ "$fence_line" -lt "$dispatch_line" ] ||
    fail 'BSD SET fence runs after the local dispatcher'
[ "$tahoe_guard_line" -lt "$fence_line" ] &&
    [ "$fence_line" -lt "$tahoe_endif_line" ] ||
    fail 'BSD SET fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" |
    sed -n "$((fence_line - 1)),$((fence_line + 12))p")
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211SetIoctl(cmd) &&' ||
    fail 'BSD BTCOEX_PROFILE fence is not SET-only'
printf '%s\n' "$fence" |
    grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'BSD BTCOEX_PROFILE fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'BSD BTCOEX_PROFILE fence inspects the nested carrier'
fi

dispatcher=$(sed -n '/case APPLE80211_IOC_BTCOEX_PROFILE:/,/case APPLE80211_IOC_BTCOEX_PROFILE_ACTIVE:/p' "$source")
printf '%s\n' "$dispatcher" |
    grep -Fq 'if (cmd == SIOCGA80211)' ||
    fail 'kernel-owned local GET route disappeared'
printf '%s\n' "$dispatcher" |
    grep -Fq 'return setBTCOEX_PROFILE((apple80211_btcoex_profile *)req->req_data);' ||
    fail 'kernel-owned local SET route disappeared'

setter=$(sed -n '/setBTCOEX_PROFILE(apple80211_btcoex_profile \*data)/,/^}/p' "$source")
printf '%s\n' "$setter" | grep -Fq 'if (data == nullptr || instance == nullptr)' ||
    fail 'kernel-owned setter lost its null-or-instance guard'
printf '%s\n' "$setter" | grep -Fq 'TahoePayloadBuilders::buildBtcoexProfile(data, &payload)' ||
    fail 'kernel-owned setter lost its payload validation'
printf '%s\n' "$setter" | grep -Fq 'payload.band >= 5 || payload.mode < 1 || payload.mode > 4 ||' ||
    fail 'kernel-owned setter lost its public field gates'
printf '%s\n' "$setter" | grep -Fq 'return kIOReturnUnsupported;' ||
    fail 'kernel-owned setter no longer fails closed without Intel backend'

grep -Fq 'kIocBtcoexProfile = 255,' "$routes" ||
    fail 'controller selector-255 route disappeared'
route_group=$(sed -n '/case kIocRoamProfile:/,/case kIocAssociate:/p' "$routes")
printf '%s\n' "$route_group" | grep -Fq 'case kIocBtcoexProfile:' ||
    fail 'controller selector-255 route is no longer retained'
printf '%s\n' "$route_group" | grep -Fq 'return true;' ||
    fail 'controller selector-255 route no longer reaches its local helper'
grep -Fq 'return sky->processApple80211Ioctl(cmd, req);' "$controller" ||
    fail 'kernel-owned card-specific bridge no longer bypasses BSD ingress'

printf 'Tahoe BTCOEX_PROFILE BSD SET carrier fence: PASS\n'
