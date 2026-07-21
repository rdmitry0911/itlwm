#!/bin/sh
# Static Tahoe BSD-ingress fence for local no-backend nested SET carriers.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"

fail() {
    echo "Tahoe no-backend BSD SET carrier fence: $1" >&2
    exit 1
}

grep -Eq '^#define[[:space:]]+APPLE80211_IOC_IE[[:space:]]+85$' "$header" ||
    fail 'IE selector no longer resolves to 85'
grep -Eq '^#define[[:space:]]+APPLE80211_IOC_WOW_TEST[[:space:]]+88$' "$header" ||
    fail 'WOW_TEST selector no longer resolves to 88'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
ie_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_IE' | cut -d: -f1)
wow_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'req->req_type == APPLE80211_IOC_WOW_TEST' | cut -d: -f1)
dispatch_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 'processApple80211Ioctl(normalizedCmd, req)' | cut -d: -f1)
tahoe_guard_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 '#if __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
tahoe_endif_line=$(printf '%s\n' "$bsd_bridge" |
    grep -Fnm1 '#endif // __IO80211_TARGET >= __MAC_26_0' | cut -d: -f1)
[ -n "$ie_line" ] && [ -n "$wow_line" ] && [ -n "$dispatch_line" ] &&
    [ -n "$tahoe_guard_line" ] && [ -n "$tahoe_endif_line" ] ||
    fail 'BSD SET fence or local dispatcher is missing'
[ "$ie_line" -lt "$wow_line" ] && [ "$wow_line" -lt "$dispatch_line" ] ||
    fail 'no-backend fence is not before the local dispatcher'
[ "$tahoe_guard_line" -lt "$ie_line" ] &&
    [ "$wow_line" -lt "$tahoe_endif_line" ] ||
    fail 'no-backend fence is not Tahoe-only'

fence=$(printf '%s\n' "$bsd_bridge" |
    sed -n "$((ie_line - 2)),$((wow_line + 20))p" | sed '/^#endif/,$d')
printf '%s\n' "$fence" | grep -Fq 'if (isApple80211SetIoctl(cmd) &&' ||
    fail 'no-backend fence is not SET-only'
printf '%s\n' "$fence" |
    grep -Fq 'req->req_type == APPLE80211_IOC_IE ||' ||
    fail 'IE is absent from the no-backend fence'
printf '%s\n' "$fence" |
    grep -Fq 'req->req_type == APPLE80211_IOC_WOW_TEST' ||
    fail 'WOW_TEST is absent from the no-backend fence'
printf '%s\n' "$fence" |
    grep -Fq 'return super::processBSDCommand(interface, cmd, data);' ||
    fail 'no-backend fence does not delegate to IO80211Family'
if printf '%s\n' "$fence" | grep -Eq 'req->req_(data|len|val)'; then
    fail 'no-backend fence inspects a nested carrier'
fi
for selector in APPLE80211_IOC_ASSOCIATE APPLE80211_IOC_CIPHER_KEY APPLE80211_IOC_SCAN_REQ; do
    if printf '%s\n' "$fence" | grep -Fq "$selector"; then
        fail "out-of-scope selector entered no-backend fence: $selector"
    fi
done

ie_dispatch=$(sed -n '/case APPLE80211_IOC_IE:/,/case APPLE80211_IOC_OFFLOAD_ARP:/p' "$source")
printf '%s\n' "$ie_dispatch" |
    grep -Fq 'return (cmd == SIOCSA80211) ? setIE((apple80211_ie_data *)req->req_data)' ||
    fail 'kernel-owned IE SET route disappeared'
printf '%s\n' "$ie_dispatch" | grep -Fq ': kIOReturnUnsupported;' ||
    fail 'IE GET fallback changed unexpectedly'

wow_dispatch=$(sed -n '/case APPLE80211_IOC_WOW_TEST:/,/case APPLE80211_IOC_TRAP_CRASHTRACER_MINI_DUMP:/p' "$source")
printf '%s\n' "$wow_dispatch" |
    grep -Fq 'return (cmd == SIOCSA80211) ? setWOW_TEST((apple80211_wow_test_data *)req->req_data)' ||
    fail 'kernel-owned WOW_TEST SET route disappeared'
printf '%s\n' "$wow_dispatch" | grep -Fq ': kIOReturnUnsupported;' ||
    fail 'WOW_TEST GET fallback changed unexpectedly'

ie_setter=$(sed -n '/setIE(apple80211_ie_data \*data)/,/setOFFLOAD_TCPKA_ENABLE/p' "$source")
printf '%s\n' "$ie_setter" |
    grep -Fq 'TahoePayloadBuilders::buildIE(data, &payload)' ||
    fail 'kernel-owned IE helper lost validation'
printf '%s\n' "$ie_setter" | grep -Fq 'return kIOReturnUnsupported;' ||
    fail 'kernel-owned IE helper no longer fails closed without a backend'

wow_setter=$(sed -n '/setWOW_TEST(apple80211_wow_test_data \*data)/,/setHT_CAPABILITY/p' "$source")
printf '%s\n' "$wow_setter" |
    grep -Fq 'uint32_t mode = *reinterpret_cast<uint32_t *>(raw + 4);' ||
    fail 'kernel-owned WOW_TEST helper no longer reads its typed carrier'
printf '%s\n' "$wow_setter" | grep -Fq 'return kIOReturnUnsupported;' ||
    fail 'kernel-owned WOW_TEST helper no longer fails closed without a backend'

printf 'Tahoe no-backend BSD SET carrier fence: PASS\n'
