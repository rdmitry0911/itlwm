#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TRACE="$SCRIPT_DIR/tahoe_wcl_multibssid_aggregate_trace_25C56.d"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$TRACE" ] || fail "missing aggregate WCL trace"

rg -F 'WCL_CANDIDATE_COUNT_OFF = 0x218' "$TRACE" >/dev/null ||
    fail "missing recovered candidate-count offset"
rg -F 'WCL_FIRST_CANDIDATE_OFF = 0x220' "$TRACE" >/dev/null ||
    fail "missing recovered first-candidate offset"
rg -F 'same_join_distinct_selected = 1' "$TRACE" >/dev/null ||
    fail "distinct-carrier comparison is not fail closed"
rg -F 'active_join_ordinal == 0' "$TRACE" >/dev/null ||
    fail "orphan carriers are not separated from a WCL join"
rg -F 'candidate_multiple++' "$TRACE" >/dev/null ||
    fail "candidate cardinality must remain categorical"
rg -F 'dtrace:::END' "$TRACE" >/dev/null ||
    fail "aggregate output must be emitted only at trace end"

if rg -n 'bssid=|paired=|channel=|%02x|%04x|%06x|%08x|%llx|copyout|system\(|scanForNetworks|networksetup|IORegistryEntrySet|ioctl' "$TRACE" >/dev/null; then
    fail "trace can render an identity or mutate wireless state"
fi

printf 'PASS: Tahoe WCL multi-BSS aggregate trace contract\n'
