#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
v2="$root/AirportItlwm/AirportItlwmV2.cpp"

dispatcher=$(awk '
/processApple80211Ioctl\(UInt cmd, apple80211req \*req\)/ { inside = 1 }
inside {
    print
    line = $0
    opens = gsub(/\{/, "{", line)
    closes = gsub(/\}/, "}", line)
    depth += opens - closes
    if (opens > 0)
        opened = 1
    if (opened && depth == 0)
        exit
}
' "$source")

line_of() {
    printf '%s\n' "$dispatcher" | grep -Fnm1 "$1" | cut -d: -f1
}

status_line=$(line_of 'kApple80211NotVirtualInterface =')
target_line=$(line_of 'req->req_type == APPLE80211_IOC_COUNTRY_CHANNELS)')
outer_null_line=$(printf '%s\n' "$dispatcher" | awk '
/if \(req->req_data == NULL\)/ { candidate = NR; next }
/return kIOReturnUnsupported;/ && candidate { print candidate; exit }
')
switch_line=$(printf '%s\n' "$dispatcher" | awk -v target="$target_line" '
NR > target && /switch \(req->req_type\)/ { print NR; exit }
')
country_case_line=$(line_of 'case APPLE80211_IOC_COUNTRY_CHANNELS:')

[ -n "$status_line" ] && [ -n "$outer_null_line" ] &&
[ -n "$target_line" ] && [ -n "$switch_line" ] &&
[ -n "$country_case_line" ] || {
    echo 'COUNTRY_CHANNELS SET fixed-stub contract is missing' >&2
    exit 1
}

[ "$outer_null_line" -lt "$target_line" ] &&
[ "$target_line" -lt "$switch_line" ] &&
[ "$switch_line" -lt "$country_case_line" ] || {
    echo 'COUNTRY_CHANNELS SET no longer terminates before the GET dispatcher' >&2
    exit 1
}

[ "$(printf '%s\n' "$dispatcher" | grep -Fc 'req->req_type == APPLE80211_IOC_COUNTRY_CHANNELS)')" -eq 1 ] || {
    echo 'COUNTRY_CHANNELS SET predicate is no longer unique' >&2
    exit 1
}

grep -Fqx '#define APPLE80211_IOC_COUNTRY_CHANNELS 237' "$header" || {
    echo 'COUNTRY_CHANNELS selector no longer resolves to 237' >&2
    exit 1
}
if grep -Eq 'COUNTRY_CHANNELS|kIocCountryChannels|case[[:space:]]+237' "$routes"; then
    echo 'COUNTRY_CHANNELS unexpectedly gained a controller/card route' >&2
    exit 1
fi
if grep -Eq 'COUNTRY_CHANNELS|[cC]ountryChannels' "$v2"; then
    echo 'COUNTRY_CHANNELS unexpectedly gained a V2 route' >&2
    exit 1
fi
if grep -Fq 'setCOUNTRY_CHANNELS(' "$source"; then
    echo 'COUNTRY_CHANNELS unexpectedly gained a local SET producer' >&2
    exit 1
fi

status_context=$(printf '%s\n' "$dispatcher" |
    sed -n "$((status_line - 1)),$((status_line + 1))p")
printf '%s\n' "$status_context" |
    grep -Fq 'static_cast<IOReturn>(0xe082280e);' || {
    echo 'COUNTRY_CHANNELS SET fixed status no longer resolves to 0xe082280e' >&2
    exit 1
}

context=$(printf '%s\n' "$dispatcher" |
    sed -n "$((target_line - 8)),$((target_line + 5))p")
printf '%s\n' "$context" | grep -Fq '#if __IO80211_TARGET >= __MAC_26_0' || {
    echo 'COUNTRY_CHANNELS SET fixed stub escaped the Tahoe guard' >&2
    exit 1
}
printf '%s\n' "$context" | grep -Fq 'if (cmd == SIOCSA80211 &&' || {
    echo 'COUNTRY_CHANNELS SET direction guard regressed' >&2
    exit 1
}
printf '%s\n' "$context" | grep -Fq 'return kApple80211NotVirtualInterface;' || {
    echo 'COUNTRY_CHANNELS SET no longer returns the raw Tahoe fixed status' >&2
    exit 1
}
if printf '%s\n' "$context" | grep -Eq 'req->(req_data|req_len|req_val)'; then
    echo 'COUNTRY_CHANNELS SET fixed stub now inspects the carrier' >&2
    exit 1
fi
if printf '%s\n' "$context" | grep -Fq 'SIOCGA80211'; then
    echo 'COUNTRY_CHANNELS GET was folded into the Tahoe SET fixed stub' >&2
    exit 1
fi

country_case=$(printf '%s\n' "$dispatcher" |
    sed -n '/case APPLE80211_IOC_COUNTRY_CHANNELS:/,/case APPLE80211_IOC_LOCALE:/p')
printf '%s\n' "$country_case" |
    grep -Fq 'getCOUNTRY_CHANNELS((apple80211_country_channel_data *)req->req_data)' || {
    echo 'COUNTRY_CHANNELS GET route disappeared' >&2
    exit 1
}
if printf '%s\n' "$country_case" | grep -Eq 'SIOCSA80211|kApple80211NotVirtualInterface'; then
    echo 'COUNTRY_CHANNELS second dispatcher gained SET fixed-stub handling' >&2
    exit 1
fi

get_section=$(sed -n '/getCOUNTRY_CHANNELS(apple80211_country_channel_data \*data)/,/^}/p' "$source")
printf '%s\n' "$get_section" | grep -Fq 'memset(reinterpret_cast<uint8_t *>(data), 0, 0x12d8);' || {
    echo 'COUNTRY_CHANNELS GET zero-fill producer regressed' >&2
    exit 1
}
printf '%s\n' "$get_section" | grep -Fq 'return kIOReturnSuccess;' || {
    echo 'COUNTRY_CHANNELS GET success producer disappeared' >&2
    exit 1
}
if printf '%s\n' "$get_section" | grep -Fq 'kApple80211NotVirtualInterface'; then
    echo 'COUNTRY_CHANNELS GET inherited the Tahoe SET fixed status' >&2
    exit 1
fi

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
for marker in 'isApple80211SetIoctl(cmd)' 'isApple80211GetIoctl(cmd) ? SIOCGA80211 : SIOCSA80211' 'if (ret != kIOReturnUnsupported)'; do
    printf '%s\n' "$bsd_bridge" | grep -Fq "$marker" || {
        echo "COUNTRY_CHANNELS BSD ingress marker missing: $marker" >&2
        exit 1
    }
done

printf 'Tahoe COUNTRY_CHANNELS SET fixed-stub contract: PASS\n'
