#!/usr/bin/env bash
# Static contract for the one-shot, identity-free IWN WCL physical-scan runner.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
RUNNER="$ROOT/scripts/run_tahoe_wcl_physical_scan_runtime.sh"
EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_wcl_physical_scan_runtime_evidence_contract.sh"
TRACE_CONTRACT="$ROOT/scripts/test_tahoe_wcl_physical_scan_trace_contract.sh"
IDENTITY_CONTRACT="$ROOT/scripts/test_tahoe_iwn_lab_loaded_identity_contract.sh"

fail() {
    printf 'FAIL: WCL physical-scan runtime runner contract: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    grep -Fq -- "$1" "$RUNNER" || fail "missing $2"
}

forbid_literal() {
    ! grep -Fq -- "$1" "$RUNNER" || fail "forbidden $2"
}

[ -x "$RUNNER" ] || fail 'runner missing or not executable'
[ -x "$EVIDENCE_CONTRACT" ] || fail 'evidence contract missing or not executable'
[ -f "$TRACE_CONTRACT" ] || fail 'WCL trace contract missing'
[ -f "$IDENTITY_CONTRACT" ] || fail 'loaded-identity contract missing'
bash -n "$RUNNER"
bash "$EVIDENCE_CONTRACT" --self-test

for needle in \
    'PINNED_GUEST="devops@127.0.0.1"' \
    'PINNED_PORT=3322' \
    'PINNED_GUEST_BUILD="25C56"' \
    'PINNED_GUEST_HOSTKEY_SHA256' \
    'StrictHostKeyChecking=yes' \
    'load_direct_runtime_candidate_receipt' \
    'capture_identity before' \
    'capture_identity after' \
    'candidate_kext_bound' \
    'ready_for_exact_local_lab_candidate_runtime_experiment' \
    'valid_trace_tool_path' \
    'valid_trace_client_sha256' \
    'remote_trace_client_exists' \
    'scan-wcl-physical' \
    'SCAN_ENDPOINT_BINDING="unresolved"' \
    'endpoint_binding' \
    'airport-itlwm-bsd' \
    'iwn-wcl-physical-scan-report' \
    'IWN_WCL_PHYSICAL_SCAN_OBSERVED' \
    'BRANCH_NOT_OBSERVED' \
    'LOWER_LEASE_NOT_OBSERVED' \
    'DONE_PUBLICATION_NOT_OBSERVED' \
    'result_publication_issued' \
    'first_missing_stage' \
    'bounded sealed IWN physical-scan lifecycles' \
    'itlwm-tahoe-iwn-wcl-physical-scan-runtime/v3' \
    'aggregate_sum_valid' \
    'local_only_raw_artifacts' \
    'association, authentication, or SAE functionality' \
    'roaming, reconnect, or multi-AP behavior' \
    'data-plane or Internet reachability' \
    'physical-host validation'; do
    require_literal "$needle" "required runner token: $needle"
done

# The scan runner owns neither a radio cycle nor joining, routing, driver
# installation, AP control, nor an identity-bearing scan interface.
for needle in \
    'remote_radio_power' \
    'run_tahoe_post_plti_trace_runtime.sh' \
    'capture_tahoe_lab_ap_visibility.py' \
    'capture_tahoe_join_trigger_pmk_delivery.py' \
    '-setairportnetwork' \
    '-listpreferredwirelessnetworks' \
    'airport -s' \
    'wdutil scan' \
    'networksetup ' \
    'ifconfig ' \
    'ipconfig ' \
    'route add' \
    'route delete' \
    'route change' \
    'kmutil ' \
    'kextload' \
    'kextunload' \
    'kextutil' \
    '/sbin/reboot' \
    'shutdown -r' \
    'hostapd' \
    'scp ' \
    'rsync ' \
    'curl '; do
    forbid_literal "$needle" "control surface: $needle"
done

python3 - "$RUNNER" <<'PY'
from pathlib import Path
import sys


text = Path(sys.argv[1]).read_text(encoding="utf-8")


def fail(message):
    raise SystemExit(f"FAIL: WCL physical-scan runtime runner contract: {message}")


def require(token, label):
    if token not in text:
        fail(f"missing {label}: {token}")


def ordered(*tokens):
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"ordered lifecycle token missing: {token}")
        cursor = position + len(token)


ordered(
    'read_candidate_receipt || {',
    'capture_identity before || fail_phase candidate-identity-before',
    'remote_trace_client_exists || fail_phase trace-client-preflight',
    'capture_trace_client reset reset || fail_phase trace-reset-request',
    'wait_for_control_ack reset-ack',
    'wait_for_initial_snapshot || fail_phase trace-reset-snapshot-sync',
    'remote_trace scan-wcl-physical >',
    'wait_for_preseal_episode_close || true',
    'capture_trace_client seal seal || fail_phase trace-seal',
    'wait_for_control_ack seal-ack',
    'capture_final_state state-read-1 || fail_phase wcl-state-first-read',
    'capture_final_state state-read-2 || fail_phase wcl-state-second-read',
    'capture_identity after || fail_phase candidate-identity-after',
    'remote_trace_client_exists || fail_phase trace-client-postflight',
)

if text.count('remote_trace scan-wcl-physical >') != 1:
    fail('runner must issue exactly one fixed physical scan')
if text.count('capture_final_state state-read-') != 2:
    fail('runner must make exactly two sealed final reads')
if text.count('cmp -s "$OUT_DIR/state-read-1-snapshot.stdout"') != 1:
    fail('runner lacks exact double-read snapshot comparison')
if text.count('cmp -s "$OUT_DIR/state-read-1-report.stdout"') != 1:
    fail('runner lacks exact double-read report comparison')

binding_start = text.find('remote_trace_client_exists() {')
binding_end = text.find('\n}\n\ncapture_trace_client()', binding_start)
if binding_start < 0 or binding_end < 0:
    fail('receipt-bound trace-client preflight is missing or unterminated')
binding = text[binding_start:binding_end]
if 'get control' in binding:
    fail('trace-client preflight incorrectly requires an ACK before reset')
for token in ('test -f "$tool" && test ! -L "$tool" && test -x "$tool"',
              'shasum -a 256 "$tool"',
              '[ "$observed" = "$expected_sha256" ]'):
    if token not in binding:
        fail(f'trace-client preflight lacks receipt binding: {token}')

for token in (
        'wcl_physical_scan_stimulus=(ok|client-unavailable|interface-unavailable|scan-failed|count-overflow|airport-itlwm-bsd-unresolved)',
        'endpoint_binding=(airport-itlwm-bsd|unresolved) total=([0-9]+)',
        'band_2ghz=([0-9]+) band_5ghz=([0-9]+)',
        'band_6ghz=([0-9]+) band_other=([0-9]+)',
        'SCAN_ENDPOINT_BINDING="${values[1]}"',
        'AIAM_WCL_SCAN_ENDPOINT_BINDING="$SCAN_ENDPOINT_BINDING"',
        '"endpoint_binding": value("SCAN_ENDPOINT_BINDING")',
        '[ "$SCAN_ENDPOINT_BINDING" = airport-itlwm-bsd ]',
        'SCAN_BAND_2GHZ + SCAN_BAND_5GHZ + SCAN_BAND_6GHZ + SCAN_BAND_OTHER',
        'IWN_WCL_PHYSICAL_SCAN_OBSERVED',
        'TRACE_BACKEND="IWN"',
        'report["iwn_wcl_physical_scan_verdict"] != "BRANCH_NOT_OBSERVED"',
        'report["first_missing_stage"] != "request"',
        'report["result_publication_issued"] != "0"',
        'report["result_publication_issued"] not in {"0", "1"}',
        'preseal_episode_is_closed()',
        'wait_for_preseal_episode_close()',
        'u32(snapshot["entry_count"]) < 3',
        'WCL_ENTRIES" -ge $((WCL_EPISODE_COUNT * 4))',
        'WCL_ENTRIES" -le 128',
        'WCL_EPISODE_COUNT" -ge 1',
        'WCL_EPISODE_COUNT" -le 2',
        'WCL_ACTIVE_EPISODE" = 0',
        'WCL_FIRST_MISSING_STAGE" = none',
        'AIAM_WCL_TRACE_BACKEND',
):
    require(token, 'strict aggregate/report grammar')

for token in (
        'capture_tahoe_iwn_lab_loaded_identity.py',
        'candidate_kext_bound',
        'ready_for_exact_local_lab_candidate_runtime_experiment',
        'test -f "$tool" && test ! -L "$tool" && test -x "$tool"',
        'shasum -a 256 "$tool"',
        'TRACE_MAY_BE_ARMED',
        'TRACE_MAY_BE_ARMED=1\ncapture_trace_client reset reset',
        '[ "$CAPTURE_GENERATION" -eq 0 ] && return 0',
        'disable_trace >/dev/null 2>&1 || true',
):
    require(token, 'candidate/trace binding or safe cleanup')

print('WCL physical-scan runtime runner contract OK')
PY
