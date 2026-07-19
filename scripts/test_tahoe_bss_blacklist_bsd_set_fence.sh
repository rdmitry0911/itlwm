#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
contract="$root/AirportItlwm/TahoeBssBlacklistContracts.hpp"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"

fail() {
    echo "Tahoe BSS_BLACKLIST BSD SET carrier fence: $1" >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_BSS_BLACKLIST 0x174' "$header" ||
    fail 'selector no longer resolves to 0x174'
grep -Fq 'static constexpr size_t kRequestLength = 1 + kMaxEntries * kBssidLength;' "$contract" ||
    fail '43-byte request carrier contract disappeared'
grep -Fq 'static constexpr size_t kMaxEntries = 7;' "$contract" ||
    fail 'seven-entry request carrier contract disappeared'
grep -Fq 'static constexpr size_t kBssidLength = 6;' "$contract" ||
    fail 'BSSID width contract disappeared'
grep -Fq 'static constexpr uint32_t kNoInterfaceStatus = 0x66;' "$contract" ||
    fail 'P0 no-interface status no longer resolves to 0x66'
grep -Fq 'static constexpr uint32_t kInvalidArgumentStatus = 0x16;' "$contract" ||
    fail 'P0 invalid-carrier status no longer resolves to 0x16'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
preflight_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'TahoeBssBlacklistContracts::routePreflightStatus' | cut -d: -f1)
preflight_fail_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'if (routeStatus != TahoeBssBlacklistContracts::kSuccessStatus)' | cut -d: -f1)
preflight_return_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'return static_cast<IOReturn>(routeStatus);' | cut -d: -f1)
set_fence_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'if (isApple80211SetIoctl(cmd))' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
[ -n "$preflight_line" ] && [ -n "$preflight_fail_line" ] &&
    [ -n "$preflight_return_line" ] && [ -n "$set_fence_line" ] &&
    [ -n "$dispatch_line" ] ||
    fail 'P0 preflight, SET fence, or dispatcher is missing'
[ "$preflight_line" -lt "$preflight_fail_line" ] &&
    [ "$preflight_fail_line" -lt "$preflight_return_line" ] &&
    [ "$preflight_return_line" -lt "$set_fence_line" ] &&
    [ "$set_fence_line" -lt "$dispatch_line" ] ||
    fail 'SET fence no longer runs after P0 preflight and before dispatch'

fence=$(printf '%s\n' "$bsd_bridge" |
    sed -n "$((set_fence_line - 1)),$((set_fence_line + 13))p")
printf '%s\n' "$fence" | grep -Fq '#if __IO80211_TARGET >= __MAC_26_0' ||
    fail 'SET fence is not Tahoe-only'
printf '%s\n' "$fence" | grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'SET fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'SET fence inspects the nested carrier after P0 preflight'
fi

dispatcher=$(sed -n '/processApple80211Ioctl(UInt cmd, apple80211req \*req)/,/case APPLE80211_IOC_CURRENT_NETWORK:/p' "$source")
printf '%s\n' "$dispatcher" | grep -Fq 'case APPLE80211_IOC_BSS_BLACKLIST:' ||
    fail 'kernel-owned local blacklist dispatcher disappeared'
printf '%s\n' "$dispatcher" | grep -Fq 'if (cmd == SIOCGA80211)' ||
    fail 'local GET path disappeared'
printf '%s\n' "$dispatcher" | grep -Fq 'if (cmd == SIOCSA80211)' ||
    fail 'kernel-owned local SET path disappeared'
printf '%s\n' "$dispatcher" |
    grep -Fq 'return getBSS_BLACKLIST((bss_blacklist *)req->req_data);' ||
    fail 'kernel-owned local GET producer disappeared'
printf '%s\n' "$dispatcher" |
    grep -Fq 'return setBSS_BLACKLIST((bss_blacklist *)req->req_data);' ||
    fail 'kernel-owned local SET producer disappeared'

if grep -Fq 'BSS_BLACKLIST' "$routes"; then
    fail 'controller-side Tahoe route unexpectedly owns blacklist selector'
fi

printf 'Tahoe BSS_BLACKLIST BSD SET carrier fence: PASS\n'
