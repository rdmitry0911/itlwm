#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
header="$root/include/Airport/apple80211_ioctl.h"
routes="$root/AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
v1="$root/AirportItlwm/AirportSTAIOCTL.cpp"
virtual="$root/AirportItlwm/AirportVirtualIOCTL.cpp"
v2="$root/AirportItlwm/AirportItlwmV2.cpp"
raw="$root/docs/reference/artifacts/skywalk-last-rx-pkt-data-set-public-fixed-stub-bootkc-25c56/raw.txt"
manifest="$(dirname -- "$raw")/SHA256SUMS.txt"

fail() {
    echo "LAST_RX_PKT_DATA SET fixed-stub contract: $1" >&2
    exit 1
}

[ -f "$raw" ] && [ -f "$manifest" ] || fail 'reference artifact is missing'
raw_digest=$(shasum -a 256 "$raw" | awk '{ print $1 }')
grep -Fqx "$raw_digest  raw.txt" "$manifest" || fail 'reference manifest mismatches raw artifact'
for marker in \
    'outer_sha256=eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d' \
    'outer_lc_uuid=F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5' \
    'mh_kext_lc_uuid=8FB4B7F0-D656-3539-B8D6-C1327A50377C' \
    'symbol=__Z29apple80211setLAST_RX_PKT_DATAP23IO80211SkywalkInterfacePv' \
    'nlist_index=7684' \
    'symbol_vmaddr=0xffffff80021c3c5e' \
    'symbol_vmaddr_end=0xffffff80021c3c69' \
    'symbol_fileoff=0x20c3c5e' \
    'symbol_fileoff_end=0x20c3c69' \
    'symbol_bytes=0x0b' \
    'next_symbol=__Z23apple80211setRADIO_INFOP23IO80211SkywalkInterfacePv' \
    'next_symbol_vmaddr=0xffffff80021c3c69' \
    'raw=55 48 89 e5 b8 0e 28 82 e0 5d c3' \
    'body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576'; do
    grep -Fqx "$marker" "$raw" || fail "reference marker missing: $marker"
done

grep -Eq '^#define[[:space:]]+APPLE80211_IOC_LAST_RX_PKT_DATA[[:space:]]+53([[:space:]]|$)' "$header" || \
    fail 'selector no longer resolves to 53'

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

carrier_null_line=$(printf '%s\n' "$dispatcher" | awk '
/^    if \(req->req_data == NULL\)$/ { candidate = NR; next }
candidate && /^        return kIOReturnUnsupported;$/ { print candidate; exit }
')
last_rx_case_line=$(printf '%s\n' "$dispatcher" | grep -Fnm1 'case APPLE80211_IOC_LAST_RX_PKT_DATA:' | cut -d: -f1)
[ -n "$carrier_null_line" ] && [ -n "$last_rx_case_line" ] || \
    fail 'canonical dispatcher boundary is missing'
[ "$carrier_null_line" -lt "$last_rx_case_line" ] || \
    fail 'SET route escaped the existing carrier-null guard'

case_count=$(printf '%s\n' "$dispatcher" | grep -Fc 'case APPLE80211_IOC_LAST_RX_PKT_DATA:' || true)
[ "$case_count" -eq 1 ] || fail 'selector case is not unique'
awk '
/#if __IO80211_TARGET >= __MAC_26_0/ { guarded = 1; next }
/#endif/ { guarded = 0; next }
guarded && /case APPLE80211_IOC_LAST_RX_PKT_DATA:/ { found = 1; exit }
END { exit !found }
' "$source" || fail 'selector case escaped the Tahoe target guard'

last_rx=$(printf '%s\n' "$dispatcher" | \
    sed -n '/case APPLE80211_IOC_LAST_RX_PKT_DATA:/,/case APPLE80211_IOC_RADIO_INFO:/p')
printf '%s\n' "$last_rx" | grep -Fq 'if (cmd == SIOCGA80211 || cmd == SIOCSA80211)' || \
    fail 'GET and canonical SET directions are no longer paired'
printf '%s\n' "$last_rx" | grep -Fq 'return static_cast<IOReturn>(0xe082280e);' || \
    fail 'fixed raw status changed'
printf '%s\n' "$last_rx" | grep -Fq 'return kIOReturnUnsupported;' || \
    fail 'unknown command fallback disappeared'
if printf '%s\n' "$last_rx" | grep -Eq 'req->(req_data|req_len|req_val)'; then
    fail 'fixed stub now inspects the carrier'
fi
if printf '%s\n' "$last_rx" | grep -Fq 'return kIOReturnSuccess;'; then
    fail 'fixed stub became a success acknowledgement'
fi

if grep -Eq '(get|set)LAST_RX_PKT_DATA[[:space:]]*\(' "$source"; then
    fail 'a local LAST_RX_PKT_DATA producer was introduced'
fi
if grep -Eq 'APPLE80211_IOC_LAST_RX_PKT_DATA|kIocLastRxPktData|case[[:space:]]+53' "$v1" "$virtual" "$v2" "$routes"; then
    fail 'an alternate V1, Virtual, V2, or controller route was introduced'
fi
grep -Fq 'default:' "$routes" && grep -Fq 'return false;' "$routes" || \
    fail 'controller route false-default disappeared'

bsd_bridge=$(sed -n '/processBSDCommand(ifnet_t interface, UInt cmd, void \*data)/,/processApple80211Ioctl(UInt cmd, apple80211req \*req)/p' "$source")
for marker in \
    'isApple80211SetIoctl(cmd)' \
    'isApple80211GetIoctl(cmd) ? SIOCGA80211 : SIOCSA80211' \
    'if (ret != kIOReturnUnsupported)'; do
    printf '%s\n' "$bsd_bridge" | grep -Fq "$marker" || \
        fail "BSD normalization marker missing: $marker"
done

python3 "$root/scripts/skywalk_public_last_rx_pkt_data_get_fixed_stub_alignment_report.py" --check >/dev/null
printf 'Tahoe LAST_RX_PKT_DATA SET fixed-stub contract: PASS\n'
