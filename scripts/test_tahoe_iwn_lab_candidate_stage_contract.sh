#!/usr/bin/env bash
# Static contract for the source-controlled IWN lab candidate artifact bridge.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
ADAPTER="$ROOT/scripts/prepare_tahoe_iwn_lab_candidate_stage.sh"
FIXTURE="$ROOT/scripts/test_tahoe_iwn_lab_candidate_stage_fixture.sh"

fail() {
    printf 'FAIL: Tahoe IWN lab candidate-stage contract: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    local needle="$1" label="$2"
    grep -Fq -- "$needle" "$ADAPTER" || fail "missing $label"
}

forbid_literal() {
    local needle="$1" label="$2"
    ! grep -Fq -- "$needle" "$ADAPTER" || fail "forbidden $label"
}

[ -x "$ADAPTER" ] || fail 'adapter is not executable'
[ -x "$FIXTURE" ] || fail 'fixture is not executable'
bash -n "$ADAPTER"
bash "$FIXTURE"

for token in \
    '--collect' \
    '--stage' \
    '--dry-run' \
    '--self-test' \
    '--gate-build-dir' \
    '--worktree' \
    '--artifacts-dir' \
    '--candidate-receipt' \
    '--archive' \
    '--trace-client' \
    '--guest-candidate-dir' \
    '--guest-trace-dir' \
    '--stage-report' \
    'GATE_DIR_PREFIX="/tmp/aiam-tahoe-sae-layer-gate."' \
    'GUEST_CANDIDATE_PREFIX="/private/tmp/aiam-iwn-lab-candidate-"' \
    'GUEST_TRACE_PREFIX="/private/tmp/aiam-post-plti-trace-"' \
    'parse_gate_build_dir()' \
    'parse_guest_pair()' \
    'stage_values_shape_valid()' \
    "IFS='|' read -r -a fields" \
    'require_clean_committed_source()' \
    'status --porcelain=v1 --untracked-files=all' \
    'git -C "$ROOT" diff --quiet' \
    'git -C "$ROOT" diff --cached --quiet' \
    'git -C "$ROOT" worktree add --detach' \
    'GIT_CONFIG_KEY_0=core.excludesFile' \
    'capture_tahoe_iwn_lab_candidate_receipt.py' \
    'load_direct_runtime_candidate_receipt' \
    'candidate-receipt-must-be-private-0600' \
    'stat.S_IMODE(value.st_mode) != 0o600' \
    'PINNED_GUEST_HOSTKEY_LINE=' \
    'PINNED_GUEST_HOSTKEY_SHA256=' \
    'StrictHostKeyChecking=yes' \
    'UserKnownHostsFile="$KNOWN_HOSTS"' \
    'GlobalKnownHostsFile=/dev/null' \
    'UpdateHostKeys=no' \
    'PRIVATE_STAGE_VERIFIED' \
    'remote_extracted_candidate_rehashed' \
    'candidate_kext_installed": False' \
    'candidate_kext_loaded": False' \
    'auxkc_mutated": False' \
    'guest_rebooted": False' \
    'runtime_experiment_performed": False'; do
    require_literal "$token" "candidate-stage token: $token"
done

# This adapter may consume the output of the existing gate, but it must never
# become another way to invoke a build or any operational driver control.
for token in \
    '"$ROOT/scripts/run_tahoe_sae_quarantine_layer.sh"' \
    'build_tahoe.sh' \
    'build_post_plti_trace.sh' \
    'xcodebuild' \
    'kmutil ' \
    'kextload' \
    'kextutil' \
    'shutdown -r' \
    '/sbin/reboot' \
    'qemu-system' \
    'networksetup ' \
    'airport -' \
    'wdutil ' \
    'ssid' \
    'bssid' \
    'passphrase' \
    'password' \
    'keychain' \
    'StrictHostKeyChecking=no' \
    'ssh-keyscan' \
    '--remote' \
    '/home/dima/Projects/itlwm'; do
    forbid_literal "$token" "candidate-stage capability: $token"
done

python3 - "$ADAPTER" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")


def require_order(*tokens: str) -> None:
    cursor = 0
    for token in tokens:
        cursor = text.find(token, cursor)
        if cursor < 0:
            raise SystemExit(f"FAIL: candidate-stage order lacks {token}")
        cursor += len(token)


require_order(
    'require_clean_committed_source',
    'parse_gate_build_dir "$GATE_BUILD_DIR"',
    'prepare_guest_transport',
    'assert_pinned_guest_build',
    'assert_remote_gate_artifacts',
    'make_fresh_detached_worktree',
    'extract_and_package_remote_artifacts',
    'capture_receipt_v2',
    'verify_collection_receipt',
    'write_collection_report',
)
require_order(
    'validate_stage_inputs_and_write_manifest',
    'prepare_guest_transport',
    'create_remote_stage_directories',
    '"${SCP[@]}" "$ARCHIVE"',
    'verify_remote_stage',
    'write_stage_report',
)

# Only the source-controlled pin is allowed to choose the destination.  The
# caller supplies one constrained gate token, never host/path/SSH overrides.
for forbidden in ('TAHOE_', 'REMOTE=', 'PORT="${', 'SSH_CONFIG'):
    if forbidden in text:
        raise SystemExit(f"FAIL: candidate-stage accepts mutable transport {forbidden}")
if text.count('PINNED_GUEST="devops@127.0.0.1"') != 1:
    raise SystemExit('FAIL: candidate-stage pinned guest is not singular')
if 'gate_build_dir_token' not in text or 'GATE_BUILD_DIR' not in text:
    raise SystemExit('FAIL: candidate-stage does not reduce gate path to safe token')
print('PASS: Tahoe IWN lab candidate-stage static checks')
PY

printf 'PASS: Tahoe IWN lab candidate-stage contract\n'
