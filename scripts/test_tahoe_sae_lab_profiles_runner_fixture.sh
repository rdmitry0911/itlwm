#!/usr/bin/env bash
# Unit fixture for the four-epoch runner; never contacts a real radio or AP.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_sae_lab_profiles.sh"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/aiam-sae-lab-runner-fixture.XXXXXX")"
trap 'rm -rf "$TMP_ROOT"' EXIT

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

MOCK_CAPTURE="$TMP_ROOT/capture"
MOCK_NETWORKSETUP="$TMP_ROOT/networksetup"
MOCK_ROUTE="$TMP_ROOT/route"
NETWORK_LOG="$TMP_ROOT/networksetup.log"
ROUTE_COUNTER="$TMP_ROOT/route-counter"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'out=""' \
    'expected=""' \
    'while [ "$#" -gt 0 ]; do' \
    '    case "$1" in' \
    '        --out) out="$2"; shift 2 ;;' \
    '        --expect) expected="$2"; shift 2 ;;' \
    '        --) shift; break ;;' \
    '        *) shift ;;' \
    '    esac' \
    'done' \
    '[ -n "$out" ] && [ -n "$expected" ]' \
    '"$@" >/dev/null 2>&1' \
    'evidence="$(mktemp -d "$out/mock-$expected.XXXXXX")"' \
    'printf "trigger_exit=0\n" >"$evidence/manifest.txt"' \
    'printf "seq=7 applied=1 mode=0x35 block=0x0\n" >"$evidence/regdiag-control.txt"' \
    'printf "fixture snapshot\n" >"$evidence/post-snapshot.txt"' \
    'printf "fixture trace\n" >"$evidence/trace.txt"' \
    'printf "SAE/PMK layer capture verdict\nexpect=%s verdict=PASS trigger_exit=0\n" "$expected" >"$evidence/verdict.txt"' \
    'printf "CAPTURED: fixture SAE/PMK evidence\n"' \
    'printf "  evidence: %s\n" "$evidence"' \
    >"$MOCK_CAPTURE"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'if [ "$#" -gt 0 ] && [ "$1" = "-listpreferredwirelessnetworks" ]; then' \
    '    printf "Preferred networks on %s\\n" "$2"' \
    '    printf "\\t%s\\n" unit-wpa2-profile unit-pure-sae-profile unit-transition-profile' \
    '    exit 0' \
    'fi' \
    'printf "%s\n" "$*" >>"$MOCK_NETWORK_LOG"' \
    'exit 0' \
    >"$MOCK_NETWORKSETUP"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'count=0' \
    'if [ -f "$MOCK_ROUTE_COUNTER" ]; then count="$(cat "$MOCK_ROUTE_COUNTER")"; fi' \
    'count=$((count + 1))' \
    'printf "%s\n" "$count" >"$MOCK_ROUTE_COUNTER"' \
    'gateway=10.0.2.2' \
    'if [ "${MOCK_ROUTE_CHANGE_AT:-0}" = "$count" ]; then gateway=10.0.2.254; fi' \
    'printf "   gateway: %s\n interface: en0\n" "$gateway"' \
    >"$MOCK_ROUTE"

chmod +x "$MOCK_CAPTURE" "$MOCK_NETWORKSETUP" "$MOCK_ROUTE"

run_fixture() {
    local out_root="$1"
    local log_path="$2"
    local route_change_at="$3"
    AIAM_SAE_LAB_TEST_MODE=1 \
    AIAM_SAE_LAB_CAPTURE_TOOL="$MOCK_CAPTURE" \
    AIAM_SAE_LAB_NETWORKSETUP_TOOL="$MOCK_NETWORKSETUP" \
    AIAM_SAE_LAB_ROUTE_TOOL="$MOCK_ROUTE" \
    AIAM_SAE_CAPTURE_ROOT="$out_root" \
    MOCK_NETWORK_LOG="$NETWORK_LOG" \
    MOCK_ROUTE_COUNTER="$ROUTE_COUNTER" \
    MOCK_ROUTE_CHANGE_AT="$route_change_at" \
    bash "$RUNNER" \
        --interface en1 \
        --wpa2-psk-ssid unit-wpa2-profile \
        --pure-sae-ssid unit-pure-sae-profile \
        --sae-transition-ssid unit-transition-profile \
        --settle 1 \
        >"$log_path" 2>&1
}

run_preflight_fixture() {
    local out_root="$1"
    local log_path="$2"
    local transition_profile="$3"
    AIAM_SAE_LAB_TEST_MODE=1 AIAM_SAE_LAB_CAPTURE_TOOL="$MOCK_CAPTURE" AIAM_SAE_LAB_NETWORKSETUP_TOOL="$MOCK_NETWORKSETUP" AIAM_SAE_LAB_ROUTE_TOOL="$MOCK_ROUTE" AIAM_SAE_CAPTURE_ROOT="$out_root" MOCK_NETWORK_LOG="$NETWORK_LOG" MOCK_ROUTE_COUNTER="$ROUTE_COUNTER" MOCK_ROUTE_CHANGE_AT=0 bash "$RUNNER" --preflight --interface en1 --wpa2-psk-ssid unit-wpa2-profile --pure-sae-ssid unit-pure-sae-profile --sae-transition-ssid "$transition_profile" --out "$out_root" --settle 1 >"$log_path" 2>&1
}

GUARD_LOG="$TMP_ROOT/override-guard.log"
if AIAM_SAE_LAB_CAPTURE_TOOL="$MOCK_CAPTURE" \
   AIAM_SAE_LAB_NETWORKSETUP_TOOL="$MOCK_NETWORKSETUP" \
   AIAM_SAE_LAB_ROUTE_TOOL="$MOCK_ROUTE" \
   bash "$RUNNER" \
       --interface en1 \
       --wpa2-psk-ssid unit-wpa2-profile \
       --pure-sae-ssid unit-pure-sae-profile \
       --sae-transition-ssid unit-transition-profile \
       --out "$TMP_ROOT/guard-out" \
       --settle 1 \
       >"$GUARD_LOG" 2>&1; then
    fail 'runner accepted custom lab tools outside explicit test mode'
fi
grep -Fq 'custom lab tools are accepted only with AIAM_SAE_LAB_TEST_MODE=1' "$GUARD_LOG" ||
    fail 'runner did not explain the custom-tool test-mode guard'

: >"$NETWORK_LOG"
rm -f "$ROUTE_COUNTER"
PREFLIGHT_OUT="$TMP_ROOT/profile-preflight-out"
PREFLIGHT_LOG="$TMP_ROOT/profile-preflight.log"
run_preflight_fixture "$PREFLIGHT_OUT" "$PREFLIGHT_LOG" unit-transition-profile || {
    sed -n '1,160p' "$PREFLIGHT_LOG" >&2
    fail 'complete saved-profile preflight failed'
}
PREFLIGHT_ATTESTATION="$(sed -n 's/^  attestation: //p' "$PREFLIGHT_LOG" | tail -n 1)"
[ -f "$PREFLIGHT_ATTESTATION" ] || fail 'profile preflight did not emit attestation'
grep -Fxq 'overall=PROFILE_READINESS_PASS' "$PREFLIGHT_ATTESTATION" ||
    fail 'complete profile preflight lacks PASS'
grep -Fxq 'wpa2_profile_known=true' "$PREFLIGHT_ATTESTATION" ||
    fail 'complete profile preflight lacks WPA2 profile'
grep -Fxq 'pure_sae_profile_known=true' "$PREFLIGHT_ATTESTATION" ||
    fail 'complete profile preflight lacks pure-SAE profile'
grep -Fxq 'sae_transition_profile_known=true' "$PREFLIGHT_ATTESTATION" ||
    fail 'complete profile preflight lacks transition profile'
grep -Fxq 'credential_lookup_or_join=not-attempted' "$PREFLIGHT_ATTESTATION" ||
    fail 'profile preflight made a credential or join claim'
if grep -Fq 'unit-wpa2-profile\|unit-pure-sae-profile\|unit-transition-profile' "$PREFLIGHT_ATTESTATION"; then
    fail 'profile preflight leaked a profile identifier'
fi
[ ! -s "$NETWORK_LOG" ] || fail 'profile preflight issued a network join trigger'

: >"$NETWORK_LOG"
rm -f "$ROUTE_COUNTER"
INCOMPLETE_OUT="$TMP_ROOT/profile-preflight-incomplete-out"
INCOMPLETE_LOG="$TMP_ROOT/profile-preflight-incomplete.log"
if run_preflight_fixture "$INCOMPLETE_OUT" "$INCOMPLETE_LOG" unit-missing-profile; then
    sed -n '1,160p' "$INCOMPLETE_LOG" >&2
    fail 'profile preflight accepted a missing saved profile'
fi
INCOMPLETE_ATTESTATION="$(sed -n 's/^  attestation: //p' "$INCOMPLETE_LOG" | tail -n 1)"
[ -f "$INCOMPLETE_ATTESTATION" ] || fail 'incomplete profile preflight did not emit attestation'
grep -Fxq 'overall=PROFILE_READINESS_INCOMPLETE' "$INCOMPLETE_ATTESTATION" ||
    fail 'incomplete profile preflight lacks incomplete verdict'
grep -Fxq 'sae_transition_profile_known=false' "$INCOMPLETE_ATTESTATION" ||
    fail 'incomplete profile preflight did not identify the missing profile'
[ ! -s "$NETWORK_LOG" ] || fail 'incomplete profile preflight issued a network join trigger'

: >"$NETWORK_LOG"
rm -f "$ROUTE_COUNTER"
SUCCESS_OUT="$TMP_ROOT/success-out"
SUCCESS_LOG="$TMP_ROOT/success.log"
run_fixture "$SUCCESS_OUT" "$SUCCESS_LOG" 0 || {
    sed -n '1,200p' "$SUCCESS_LOG" >&2
    fail 'all-success runner fixture failed'
}
SUCCESS_ATTESTATION="$(sed -n 's/^  attestation: //p' "$SUCCESS_LOG" | tail -n 1)"
[ -f "$SUCCESS_ATTESTATION" ] || fail 'success fixture did not emit attestation'
grep -Fxq 'overall=PASS' "$SUCCESS_ATTESTATION" || fail 'success fixture lacks overall PASS'
[ "$(grep -Fc 'scenario_begin=' "$SUCCESS_ATTESTATION")" -eq 4 ] ||
    fail 'success fixture did not record four scenarios'
[ "$(grep -Fc 'verdict=PASS' "$SUCCESS_ATTESTATION")" -eq 4 ] ||
    fail 'success fixture did not record four PASS verdicts'
[ "$(grep -Fc 'default_route_preserved=true' "$SUCCESS_ATTESTATION")" -eq 4 ] ||
    fail 'success fixture did not record route preservation per scenario'
grep -Fq 'capture_trace_sha256=' "$SUCCESS_ATTESTATION" ||
    fail 'success fixture lacks trace hashes'
if grep -Fq '=missing' "$SUCCESS_ATTESTATION"; then
    fail 'success fixture attestation has a missing capture hash'
fi
if grep -Fq 'unit-wpa2-profile\|unit-pure-sae-profile\|unit-transition-profile' "$SUCCESS_ATTESTATION"; then
    fail 'success fixture leaked a profile identifier into the attestation'
fi
[ "$(wc -l <"$NETWORK_LOG")" -eq 4 ] ||
    fail 'runner did not issue four isolated networksetup triggers'

: >"$NETWORK_LOG"
rm -f "$ROUTE_COUNTER"
FAIL_OUT="$TMP_ROOT/route-change-out"
FAIL_LOG="$TMP_ROOT/route-change.log"
if run_fixture "$FAIL_OUT" "$FAIL_LOG" 3; then
    sed -n '1,240p' "$FAIL_LOG" >&2
    fail 'runner accepted a changed default route'
fi
FAIL_ATTESTATION="$(sed -n 's/^  attestation: //p' "$FAIL_LOG" | tail -n 1)"
[ -f "$FAIL_ATTESTATION" ] || fail 'route-change fixture did not emit attestation'
grep -Fxq 'overall=INCONCLUSIVE_OR_FAIL' "$FAIL_ATTESTATION" ||
    fail 'route-change fixture did not fail the overall batch'
grep -Fxq 'default_route_preserved=false' "$FAIL_ATTESTATION" ||
    fail 'route-change fixture did not record the route safety failure'

echo 'PASS: Tahoe SAE/PMF profile-preflight and four-epoch runner fixture'
