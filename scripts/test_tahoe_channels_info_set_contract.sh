#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"

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
target_line=$(line_of 'req->req_type == APPLE80211_IOC_CHANNELS_INFO)')
outer_null_line=$(printf '%s\n' "$dispatcher" | awk '
/if \(req->req_data == NULL\)/ { candidate = NR; next }
/return kIOReturnUnsupported;/ && candidate { print candidate; exit }
')
switch_line=$(printf '%s\n' "$dispatcher" | awk -v target="$target_line" '
NR > target && /switch \(req->req_type\)/ { print NR; exit }
')

[ -n "$status_line" ] && [ -n "$outer_null_line" ] &&
[ -n "$target_line" ] && [ -n "$switch_line" ] || {
    echo 'CHANNELS_INFO SET fixed-stub contract is missing' >&2
    exit 1
}

[ "$outer_null_line" -lt "$target_line" ] &&
[ "$target_line" -lt "$switch_line" ] || {
    echo 'CHANNELS_INFO SET no longer terminates after the normal carrier guard' >&2
    exit 1
}

status_context=$(printf '%s\n' "$dispatcher" |
    sed -n "$((status_line - 1)),$((status_line + 1))p")
printf '%s\n' "$status_context" |
    grep -Fq 'static_cast<IOReturn>(0xe082280e);' || {
    echo 'CHANNELS_INFO SET fixed status no longer resolves to 0xe082280e' >&2
    exit 1
}

context=$(printf '%s\n' "$dispatcher" |
    sed -n "$((target_line - 8)),$((target_line + 5))p")
printf '%s\n' "$context" | grep -Fq '#if __IO80211_TARGET >= __MAC_26_0' || {
    echo 'CHANNELS_INFO SET fixed stub escaped the Tahoe guard' >&2
    exit 1
}
printf '%s\n' "$context" | grep -Fq 'if (cmd == SIOCSA80211 &&' || {
    echo 'CHANNELS_INFO SET direction guard regressed' >&2
    exit 1
}
printf '%s\n' "$context" | grep -Fq 'return kApple80211NotVirtualInterface;' || {
    echo 'CHANNELS_INFO SET no longer returns the raw Tahoe fixed status' >&2
    exit 1
}
if printf '%s\n' "$context" | grep -Eq 'req->(req_data|req_len|req_val)'; then
    echo 'CHANNELS_INFO SET fixed stub now inspects the carrier' >&2
    exit 1
fi
if printf '%s\n' "$context" | grep -Fq 'SIOCGA80211'; then
    echo 'CHANNELS_INFO GET was folded into the Tahoe SET fixed stub' >&2
    exit 1
fi

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
for marker in 'isApple80211SetIoctl(cmd)' 'isApple80211GetIoctl(cmd) ? SIOCGA80211 : SIOCSA80211' 'if (ret != kIOReturnUnsupported)'; do
    printf '%s\n' "$bsd_bridge" | grep -Fq "$marker" || {
        echo "CHANNELS_INFO BSD ingress marker missing: $marker" >&2
        exit 1
    }
done

get_section=$(sed -n '/getCHANNELS_INFO(apple80211_channels_info \*data)/,/^}/p' "$source")
printf '%s\n' "$get_section" | grep -Fq 'return kIOReturnSuccess;' || {
    echo 'CHANNELS_INFO GET producer disappeared' >&2
    exit 1
}
if printf '%s\n' "$get_section" | grep -Fq 'kApple80211NotVirtualInterface'; then
    echo 'CHANNELS_INFO GET inherited the Tahoe SET fixed status' >&2
    exit 1
fi

printf 'Tahoe CHANNELS_INFO SET fixed-stub contract: PASS\n'
