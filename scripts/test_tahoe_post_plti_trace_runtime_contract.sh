#!/usr/bin/env bash
# Static safety contract for the dedicated post-PLTI runtime runner.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_post_plti_trace_runtime.sh"
EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_post_plti_trace_runtime_evidence_contract.sh"

fail() {
    printf 'FAIL: post-PLTI runtime runner contract: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    local needle="$1" label="$2"
    grep -Fq -- "$needle" "$RUNNER" || fail "missing $label"
}

forbid_literal() {
    local needle="$1" label="$2"
    ! grep -Fq -- "$needle" "$RUNNER" || fail "forbidden $label"
}

[ -f "$RUNNER" ] || fail 'runner missing'
[ -f "$EVIDENCE_CONTRACT" ] || fail 'evidence contract missing'
bash -n "$RUNNER"
bash "$EVIDENCE_CONTRACT" --self-test

for needle in \
    'PINNED_GUEST="devops@127.0.0.1"' \
    'PINNED_PORT=3322' \
    'StrictHostKeyChecking=yes' \
    'PINNED_GUEST_HOSTKEY_SHA256' \
    'PINNED_WIFI_INTERFACE="en1"' \
    'PINNED_GUEST_BUILD="25C56"' \
    '--identity-evidence' \
    'itlwm-tahoe-lab-kext-identity-binding/v2' \
    'candidate_kext_bound' \
    'all(value is True for value in checks.values())' \
    'ready_for_exact_candidate_runtime_experiment' \
    'identity-bound source commit' \
    'source_identity_sha256' \
    'semantic release tag' \
    'single_mutable_release_per_semantic_version' \
    'reset reset' \
    'get control' \
    'get snapshot' \
    'get trace' \
    'get report' \
    'final-off off' \
    'wait_for_control_ack' \
    'seal_trace' \
    'observe_trace_before_seal' \
    'seal seal' \
    'wait_for_reset_snapshot_buffer_sync' \
    'backend IWN' \
    '[ "$backend" = 1 ]' \
    'read_trace_once read-1' \
    'read_trace_once read-2' \
    'TRACE_FIRST_MISSING_STAGE' \
    'TRACE_SEAL_ACKNOWLEDGED' \
    'first_missing_stage' \
    'seal_control_acknowledged' \
    'trace-double-read-unstable' \
    'KERNEL_CHAIN_OBSERVED' \
    'trace-verdict-diagnostic' \
    'saved_profile_autojoin_only' \
    'requested_cycles": 1' \
    'physical_host_touched' \
    'guest_rebooted_by_runner' \
    'runtime-attestation.json' \
    'client_output_retained_local_only' \
    'networksetup -setairportpower en1' \
    'RADIO_OFF_PENDING=1' \
    'RADIO_RECOVERY_ATTEMPTED=1'; do
    require_literal "$needle" "required safety/sequence token: $needle"
done

# This runner is intentionally a narrow radio stimulus, not a join/scan or
# network reconfiguration tool.  Search exact command carriers, rather than
# natural-language non-claim comments.
for needle in \
    '-setairportnetwork' \
    '-listpreferredwirelessnetworks' \
    'airport -s' \
    'wdutil scan' \
    'ipconfig ' \
    'ifconfig ' \
    'route add' \
    'route delete' \
    'route change' \
    'kmutil ' \
    'kextload' \
    'kextutil' \
    'shutdown -r' \
    '/sbin/reboot' \
    'scp ' \
    'rsync ' \
    'curl ' \
    'AIAM_POST_PLTI_TRACE_TEST_MODE'; do
    forbid_literal "$needle" "capability: $needle"
done

forbid_literal '--source-commit' 'free source-commit label'

# A passing trace is stricter than merely observing a radio recovery: reset
# generation, snapshot/buffer agreement, stable reads and diagnostic shutdown
# all have to hold.  Optional asynchronous firmware completions are delegated
# to the shared ordered classifier and are not guessed by this shell harness.
python3 - "$RUNNER" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")
ordered = (
    'capture_trace_client reset reset',
    'wait_for_control_ack reset-ack',
    'wait_for_reset_snapshot_buffer_sync',
    'remote_radio_power off',
    'remote_radio_power on',
    'observe_trace_before_seal',
    'seal_trace || fail_phase trace-seal',
    'read_trace_once read-1 0',
    'read_trace_once read-2 0',
    'disable_trace || fail_phase trace-final-off',
)
cursor = 0
for item in ordered:
    cursor = text.find(item, cursor)
    if cursor < 0:
        raise SystemExit(f"FAIL: post-PLTI runtime order missing {item}")
    cursor += len(item)
if 'EapolTxDone' in text or 'eapol-txdone' in text:
    raise SystemExit('FAIL: shell runner must not independently require asynchronous EAPOL TX completion')
print('PASS: post-PLTI runtime runner static safety contract')
PY
