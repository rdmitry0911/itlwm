#!/usr/bin/env bash
# Behavioral contract for the switcher's bounded passive nl80211 retry owner.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$ROOT/scripts/tahoe_labap_bss_switcher.sh"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

test -f "$SCRIPT" || fail "switcher missing"
bash -n "$SCRIPT" || fail "switcher syntax"

passive_scan_body="$(sed -n '/^passive_flushed_scan()/,/^}/p' "$SCRIPT")"
[ -n "$passive_scan_body" ] || fail "passive retry owner missing"

TMP_DIR="$(mktemp -d /tmp/aiam-labap-passive-retry.XXXXXX)" ||
    fail "temporary directory"
cleanup() {
    /usr/bin/find -P "$TMP_DIR" -depth -delete >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM

run_case() {
    local name="$1" fake_rc="$2" expected_rc="$3" expected_calls="$4"
    local state_file="$TMP_DIR/$name.count" output

    printf '%s\n' 0 >"$state_file"
    output="$(
        {
            printf '%s\n' \
                'set -u' \
                'IW=/dev/null' \
                'STA_IF=sta0' \
                'LABAP_PASSIVE_SCAN_ATTEMPTS=4' \
                'LABAP_PASSIVE_SCAN_RETRY_INTERVAL_SECONDS=0' \
                'sudo_cmd() {' \
                '    calls=$(<"$TEST_STATE_FILE")' \
                '    printf "%s\\n" "$((calls + 1))" >"$TEST_STATE_FILE"' \
                '    return "$FAKE_RC"' \
                '}'
            printf '%s\n' "$passive_scan_body"
            printf '%s\n' \
                'set +e' \
                'passive_flushed_scan' \
                'case_rc=$?' \
                'set -e' \
                'printf "%s\\n" "rc=$case_rc calls=$(<"$TEST_STATE_FILE")"'
        } | TEST_STATE_FILE="$state_file" FAKE_RC="$fake_rc" bash
    )" || fail "$name harness"
    [ "$output" = "rc=$expected_rc calls=$expected_calls" ] ||
        fail "$name result: $output"
}

# EBUSY is the sole retryable nl80211 status.  A persistent busy scan must
# still fail closed after exactly the fixed number of passive attempts.
run_case busy 240 240 4
# A non-EBUSY scanner failure must not be retried or turned into an empty scan.
run_case hard_failure 1 1 1
# A successful scan returns immediately; the caller owns topology parsing.
run_case success 0 0 1

printf 'PASS: Tahoe LabAP passive scan retry behavior\n'
