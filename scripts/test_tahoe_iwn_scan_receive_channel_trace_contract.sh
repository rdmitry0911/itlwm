#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TRACE="$SCRIPT_DIR/tahoe_iwn_scan_receive_channel_trace_25C56.d"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$TRACE" ] || fail "missing IWN scan receive-channel trace"

rg -F 'IEEE80211_RXINFO_CHANNEL_OFF = 12' "$TRACE" >/dev/null ||
    fail "trace must use the recovered rxinfo channel offset"
rg -F '_Z25ieee80211_recv_probe_resp' "$TRACE" >/dev/null ||
    fail "trace must bind at net80211 beacon/probe-response ingress"
rg -F '_Z21ieee80211_setup_rates' "$TRACE" >/dev/null ||
    fail "trace must bind the parser-to-candidate boundary"
rg -F 'self->recv_probe_candidate = 1' "$TRACE" >/dev/null ||
    fail "trace must record candidate survival"
rg -F 'dropped_before_candidate++' "$TRACE" >/dev/null ||
    fail "trace must retain parser drops separately"
rg -F 'received_channel_9++' "$TRACE" >/dev/null ||
    fail "trace must observe channel 9 delivery"
rg -F 'received_channel_13++' "$TRACE" >/dev/null ||
    fail "trace must observe channel 13 delivery"
rg -F 'received_channel_149++' "$TRACE" >/dev/null ||
    fail "trace must observe channel 149 delivery"
rg -F 'received_channel_153++' "$TRACE" >/dev/null ||
    fail "trace must observe channel 153 delivery"
rg -F 'received_channel_177++' "$TRACE" >/dev/null ||
    fail "trace must observe channel 177 delivery"
rg -F 'dtrace:::END' "$TRACE" >/dev/null ||
    fail "aggregate output must be emitted only at trace end"

if rg -n 'bssid|ssid|macaddr|%02x|%04x|%06x|%08x|%llx|copyout|system\(|scanForNetworks|networksetup|IORegistryEntrySet|ioctl' "$TRACE" >/dev/null; then
    fail "trace can render an identity or mutate wireless state"
fi

printf 'PASS: Tahoe IWN scan receive-channel trace contract\n'
