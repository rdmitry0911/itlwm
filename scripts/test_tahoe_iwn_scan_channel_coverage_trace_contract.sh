#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TRACE="$SCRIPT_DIR/tahoe_iwn_scan_channel_coverage_trace_25C56.d"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$TRACE" ] || fail "missing IWN scan channel coverage trace"

rg -F 'IWN_CMD_SCAN = 128' "$TRACE" >/dev/null ||
    fail "trace must bind only IWN scan commands"
rg -F 'fbt:com.zxystd.AirportItlwm:_ZN6ItlIwn15iwn_scan_submitEP9iwn_softctiybbbyjPbS2_:entry' "$TRACE" >/dev/null ||
    fail "trace must bind the current IWN scan-constructor entry ABI"
rg -F 'fbt:com.zxystd.AirportItlwm:_ZN6ItlIwn15iwn_scan_submitEP9iwn_softctiybbbyjPbS2_:return' "$TRACE" >/dev/null ||
    fail "trace must bind the current IWN scan-constructor return ABI"
if rg -F '_ZN6ItlIwn15iwn_scan_submitEP9iwn_softctiybbyjPbS2_' "$TRACE" >/dev/null; then
    fail "trace must not retain the pre-WCL-ownership scan-constructor ABI"
fi
rg -F '_Z19ieee80211_chan2ieee' "$TRACE" >/dev/null ||
    fail "trace must observe the IWN channel-vector construction call"
rg -F 'self->iwn_scan_had_command = 1' "$TRACE" >/dev/null ||
    fail "trace must require a firmware command-ring submission"
rg -F 'self->iwn_scan_had_command == 0' "$TRACE" >/dev/null ||
    fail "trace must exclude post-command channel housekeeping"
rg -F 'channel_9_present = 1' "$TRACE" >/dev/null ||
    fail "trace must observe channel 9 membership"
rg -F 'channel_13_present = 1' "$TRACE" >/dev/null ||
    fail "trace must observe channel 13 membership"
rg -F 'channel_149_present = 1' "$TRACE" >/dev/null ||
    fail "trace must observe channel 149 membership"
rg -F 'channel_153_present = 1' "$TRACE" >/dev/null ||
    fail "trace must observe channel 153 membership"
rg -F 'channel_177_present = 1' "$TRACE" >/dev/null ||
    fail "trace must observe channel 177 membership"
rg -F 'requested_9_13_153_covered' "$TRACE" >/dev/null ||
    fail "trace must publish the observed host-channel coverage aggregate"
rg -F 'dtrace:::END' "$TRACE" >/dev/null ||
    fail "aggregate output must be emitted only at trace end"

if rg -n 'bssid|ssid|macaddr|%02x|%04x|%06x|%08x|%llx|copyout|system\(|scanForNetworks|networksetup|IORegistryEntrySet|ioctl' "$TRACE" >/dev/null; then
    fail "trace can render an identity or mutate wireless state"
fi

printf 'PASS: Tahoe IWN scan channel coverage trace contract\n'
