#!/usr/bin/env bash
# Static safety gate for the separate opaque-stdin direct-ISAE runtime runner.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
runner="$root/scripts/run_tahoe_iwn_direct_isae_runtime.py"
receipt="$root/scripts/capture_tahoe_iwn_direct_sae_runtime_artifact_receipt.py"
receipt_contract="$root/scripts/test_tahoe_iwn_direct_isae_runtime_artifact_receipt_contract.sh"
evidence_contract="$root/scripts/test_tahoe_iwn_direct_isae_runtime_evidence_contract.sh"
client_contract="$root/scripts/test_tahoe_iwn_direct_sae_lab_client_contract.sh"

fail() {
    printf 'FAIL: direct-ISAE runtime runner contract: %s\n' "$*" >&2
    exit 1
}

[ -x "$runner" ] || fail 'runner missing or not executable'
[ -x "$receipt" ] || fail 'artifact receipt helper missing or not executable'
[ -x "$receipt_contract" ] || fail 'artifact receipt contract missing or not executable'
[ -x "$evidence_contract" ] || fail 'evidence contract missing or not executable'
[ -x "$client_contract" ] || fail 'direct UserClient helper contract missing or not executable'
python3 -m py_compile "$runner"
bash "$receipt_contract"
bash "$evidence_contract"
bash "$client_contract"

python3 - "$runner" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")

required = (
    'itlwm-tahoe-iwn-direct-isae-runtime/v1',
    'require_fifo_stdin',
    'stat.S_ISFIFO',
    'sys.stdin.buffer',
    'load_artifact_receipt',
    'remote_artifacts_bound',
    'REMOTE_ARTIFACTS_BOUND',
    'REMOTE_TRACE',
    'REMOTE_QUERY',
    'REMOTE_SUBMIT',
    '--submit-stdin',
    '--hold-milliseconds',
    'guest.run_root_command(',
    'shlex.quote(REMOTE_SUBMIT)',
    'remote_query(',
    'parse_lab_status',
    'wait_control(',
    'wait_snapshot(',
    'get", "iwn-direct-sae-report',
    'DIRECT_SAE_4WAY_PORT_VALID',
    'report_one == report_two',
    'positive_trace',
    'validate_evidence_document',
    'unparsed_transport_output_retained',
    'artifact_path_retained',
    'host_dtrace_zero',
    'guest_dtrace_zero',
    'trusted_host_environment',
    'PINNED_HOST_KEY_FINGERPRINT',
    'ROOT_ARTIFACT_PARENT',
    'ROOT_ARTIFACT_DIR',
    'run_root_script',
    'trace_reset_may_be_active',
    'trace_cleanup_fallback_attempted',
    'sealed-snapshot-report',
    'StrictHostKeyChecking=yes',
    'GlobalKnownHostsFile=/dev/null',
)
for token in required:
    if token not in text:
        raise SystemExit(f'FAIL: direct-ISAE runner lacks {token}')

forbidden = (
    'networksetup',
    'CoreWLAN',
    'Keychain',
    'IORegistry',
    'scanForNetworks',
    'airport -s',
    'ifconfig ',
    'route add',
    'route delete',
    'kmutil ',
    'kextload',
    'kextutil',
    'shutdown -r',
    '/sbin/reboot',
    'run_tahoe_iwn_direct_sae_runtime.sh',
    'sys.stdin.read',
    'request_stream.read',
    'request_stream.write',
    'dtrace -',
    '--password',
    '--passphrase',
    '--ssid',
    '--bssid',
    '--direct-client-sha256',
    '--trace-client-sha256',
)
for token in forbidden:
    if token.lower() in text.lower():
        raise SystemExit(f'FAIL: direct-ISAE runner exposes forbidden surface {token}')

submit_start = text.find('def remote_submit(')
submit_end = text.find('\ndef parse_lab_status(', submit_start)
if submit_start < 0 or submit_end < 0:
    raise SystemExit('FAIL: direct-ISAE submit helper missing')
submit = text[submit_start:submit_end]
for token in ('guest.run_root_command(', 'exec /usr/bin/sudo -n /bin/bash -c', 'shlex.quote(REMOTE_SUBMIT)', 'timeout=hold_milliseconds // 1000 + 20'):
    if token not in submit:
        raise SystemExit(f'FAIL: direct-ISAE submit transport lacks {token}')
for token in ('run_script(', 'read_text(', 'write_text(', 'stdin.read', 'tee', 'heredoc'):
    if token in submit:
        raise SystemExit(f'FAIL: direct-ISAE submit transport consumes or persists opaque input: {token}')

run_start = text.find('def run(')
if run_start < 0:
    raise SystemExit('FAIL: direct-ISAE run sequence missing')
run = text[run_start:]
ordered = (
    'request_stream = require_fifo_stdin()',
    'receipt = load_artifact_receipt',
    'remote_artifacts_bound(',
    'state.trace_reset_may_be_active = True',
    'remote_trace(guest, artifact_dir, state.trace_sha256, ("reset",))',
    'remote_query(',
    'remote_submit(',
    'remote_trace(guest, artifact_dir, state.trace_sha256, ("seal",))',
    '("get", "iwn-direct-sae-report")',
    'report_one == report_two',
    'remote_artifacts_bound(',
)
cursor = 0
for token in ordered:
    position = run.find(token, cursor)
    if position < 0:
        raise SystemExit(f'FAIL: direct-ISAE runtime order lacks {token}')
    cursor = position + len(token)

if run.count('remote_submit(') != 1:
    raise SystemExit('FAIL: direct-ISAE runner may submit more than once')
if run.count('("get", "iwn-direct-sae-report")') != 2:
    raise SystemExit('FAIL: direct-ISAE runner must read two frozen reports')
if 'trace_reset_may_be_active and not state.trace_final_disabled' not in run:
    raise SystemExit('FAIL: direct-ISAE runner has no failed-cycle trace cleanup')
if 'remote_trace(guest, artifact_dir, state.trace_sha256, ("off",))' not in run:
    raise SystemExit('FAIL: direct-ISAE runner has no trace-off fallback')
if run.find('state.trace_reset_may_be_active = True') > run.find('remote_trace(guest, artifact_dir, state.trace_sha256, ("reset",))'):
    raise SystemExit('FAIL: direct-ISAE runner arms cleanup after reset')
for token in (
    'state.trace_cleanup_fallback_attempted = True',
    'state.trace_cleanup_seal_confirmed = True',
    'state.trace_cleanup_off_attempted = True',
    'state.trace_cleanup_disabled_confirmed = True',
):
    if token not in run:
        raise SystemExit(f'FAIL: direct-ISAE runner lacks cleanup evidence state {token}')
for token in (
    'final["entry_count"] == first["entry_count"]',
    'final["episode_count"] == first["episode_count"]',
    'final["active_episode"] == first["active_episode"]',
):
    if token not in run:
        raise SystemExit(f'FAIL: direct-ISAE runner does not bind sealed snapshot to report: {token}')

transport_start = text.find('class PinnedGuest:')
transport_end = text.find('\ndef decoded_stdout(', transport_start)
if transport_start < 0 or transport_end < 0:
    raise SystemExit('FAIL: direct-ISAE pinned transport missing')
transport = text[transport_start:transport_end]
for token in (
    '"/usr/bin/ssh-keygen"',
    'fields[1] != PINNED_HOST_KEY_FINGERPRINT',
    '"/usr/bin/ssh"',
    'env=trusted_host_environment()',
    'def run_root_script(',
    '"/usr/bin/sudo", "-n", "/bin/bash", "-s", "--"',
    'def run_root_command(',
):
    if token not in transport:
        raise SystemExit(f'FAIL: direct-ISAE transport lacks pinned root execution {token}')
for token in ('["ssh"', '["ssh-keygen"', '["pgrep"'):
    if token in text:
        raise SystemExit('FAIL: direct-ISAE runner retains a PATH-resolved host tool')

verify_start = text.find('REMOTE_VERIFY_LIBRARY =')
verify_end = text.find('\n\nREMOTE_ARTIFACTS_BOUND', verify_start)
if verify_start < 0 or verify_end < 0:
    raise SystemExit('FAIL: direct-ISAE root artifact verifier missing')
verify = text[verify_start:verify_end]
for token in (
    'test "$artifact_dir" = "__ROOT_ARTIFACT_DIR__"',
    'for root_path in /private /private/var /private/var/db __ROOT_ARTIFACT_PARENT__',
    'root_owned_nonwritable "$root_path"',
    'root_owned_exact_mode "$artifact_dir" 700',
    'root_owned_exact_mode "$tool" 500',
):
    if token not in verify:
        raise SystemExit(f'FAIL: direct-ISAE root verifier lacks ancestry control {token}')
if 'exec /usr/bin/sudo -n -- "$tool"' in text:
    raise SystemExit('FAIL: direct-ISAE runner verifies under one identity and executes under another')

evidence_start = text.find('def evidence_document(')
evidence_end = text.find('\ndef require_exact_keys(', evidence_start)
if evidence_start < 0 or evidence_end < 0:
    raise SystemExit('FAIL: direct-ISAE evidence builder missing')
evidence = text[evidence_start:evidence_end]
for forbidden_label in ('ssid', 'bssid', 'password', 'passphrase', 'keychain', 'credential'):
    if forbidden_label in evidence.lower():
        raise SystemExit(f'FAIL: direct-ISAE aggregate names forbidden field {forbidden_label}')
for required_token in (
    '"wireless_identity_collected": False',
    '"network_input_collected": False',
    '"opaque_request_retained": False',
    '"unparsed_transport_output_retained": False',
    '"artifact_path_retained": False',
    '"readiness_observed": state.readiness_observed',
    '"cleanup_fallback_attempted": state.trace_cleanup_fallback_attempted',
):
    if required_token not in evidence:
        raise SystemExit(f'FAIL: direct-ISAE aggregate lacks privacy invariant {required_token}')
print('PASS: direct-ISAE runtime runner static contract')
PY
