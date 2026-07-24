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
    '--trace-client-sha256' \
    'valid_trace_client_sha256' \
    'TRACE_CLIENT_SHA256' \
    'expected_sha256' \
    '/usr/bin/shasum -a 256' \
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
    'local label="${1:-final-off}" off_sequence' \
    'capture_trace_client "$label" off' \
    'wait_for_control_ack' \
    'seal_trace' \
    'observe_trace_before_seal' \
    'seal seal' \
    'wait_for_reset_snapshot_buffer_sync' \
    'backend IWN' \
    '[ "$backend" = 1 ]' \
    '[ "$backend" = 3 ]' \
    'TRACE_BACKEND="iwx"' \
    'trace-backend-iwx-ordered-unsupported' \
    'backend == "iwn"' \
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
    'RADIO_RECOVERY_ATTEMPTED=1' \
    '--arm-while-radio-off' \
    'preflight-reset reset' \
    'preflight-reset-ack' \
    'preflight-off' \
    'BACKEND_PREFLIGHT_IWN=1' \
    'backend_preflight_iwn' \
    'TRACE_ARMED_WHILE_RADIO_OFF=1' \
    'trace_armed_while_radio_off' \
    'radio-off-before-final-reset' \
    'radio-off-after-final-reset-ack' \
    'radio-off-after-final-reset-sync'; do
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

def index_after(needle: str, start: int = 0) -> int:
    position = text.find(needle, start)
    if position < 0:
        raise SystemExit(f"FAIL: post-PLTI runtime order missing {needle}")
    return position

strict_start = index_after('if [ "$ARM_WHILE_RADIO_OFF" -eq 1 ]; then',
                           index_after('wait_for_radio_state on || fail_phase radio-precondition-on'))
strict_end = index_after('\nfi\n\nTRACE_MAY_BE_ARMED=1', strict_start)
preflight_reset = index_after('capture_trace_client preflight-reset reset', strict_start)
preflight_ack = index_after('wait_for_control_ack preflight-reset-ack', preflight_reset)
preflight_disable = index_after('disable_trace preflight-off', preflight_ack)
fresh_on = index_after('wait_for_radio_state on || fail_phase radio-precondition-on', preflight_disable)
strict_off = index_after('remote_radio_power off', fresh_on)
if not strict_start < preflight_reset < preflight_ack < preflight_disable < fresh_on < strict_off < strict_end:
    raise SystemExit('FAIL: strict IWN backend preflight must finish before its radio OFF')

iwx_boundary = index_after('fail_phase trace-backend-iwx-ordered-unsupported', preflight_ack)
if not preflight_ack < iwx_boundary < strict_off:
    raise SystemExit('FAIL: strict IWX boundary must precede strict radio OFF')

final_reset = index_after('capture_trace_client reset reset', strict_end)
reset_ack = index_after('wait_for_control_ack reset-ack', final_reset)
reset_sync = index_after('wait_for_reset_snapshot_buffer_sync', reset_ack)
legacy_start = index_after('if [ "$ARM_WHILE_RADIO_OFF" -eq 0 ]; then', reset_sync)
legacy_off = index_after('remote_radio_power off', legacy_start)
radio_on = index_after('remote_radio_power on', legacy_off)
if not final_reset < reset_ack < reset_sync < legacy_off < radio_on:
    raise SystemExit('FAIL: legacy arm-then-radio ordering regressed')
if not strict_off < final_reset < radio_on:
    raise SystemExit('FAIL: strict final reset must occur after OFF and before the only ON')
off_before_reset = index_after(
    'wait_for_radio_state off || fail_phase radio-off-before-final-reset', strict_off)
off_after_ack = index_after(
    'wait_for_radio_state off || fail_phase radio-off-after-final-reset-ack', reset_ack)
off_after_sync = index_after(
    'wait_for_radio_state off || fail_phase radio-off-after-final-reset-sync', reset_sync)
armed_while_off = index_after('TRACE_ARMED_WHILE_RADIO_OFF=1', off_after_sync)
if not (strict_off < off_before_reset < final_reset < reset_ack < off_after_ack <
        reset_sync < off_after_sync < armed_while_off < radio_on):
    raise SystemExit('FAIL: strict causal radio-OFF checks are not ordered around final reset')
for item in (
    'observe_trace_before_seal',
    'seal_trace || fail_phase trace-seal',
    'read_trace_once read-1 0',
    'read_trace_once read-2 0',
    'disable_trace || fail_phase trace-final-off',
):
    index_after(item, radio_on)
if 'EapolTxDone' in text or 'eapol-txdone' in text:
    raise SystemExit('FAIL: shell runner must not independently require asynchronous EAPOL TX completion')
iwx = text.find('TRACE_BACKEND="iwx"')
if iwx < 0:
    raise SystemExit('FAIL: post-PLTI runtime runner lacks an explicit IWX ordered boundary')
remote_trace = text.find('remote_trace() {')
remote_exists = text.find('remote_trace_client_exists() {')
if remote_trace < 0 or remote_exists < 0:
    raise SystemExit('FAIL: post-PLTI runtime runner lacks trace-client helpers')
for helper in (text[remote_trace:remote_exists], text[remote_exists:text.find('remote_radio_state()', remote_exists)]):
    if '"$TRACE_CLIENT_SHA256"' not in helper or 'expected_sha256' not in helper or 'shasum -a 256' not in helper:
        raise SystemExit('FAIL: trace-client digest is not checked on every generic client invocation')
print('PASS: post-PLTI runtime runner static safety contract')
PY
