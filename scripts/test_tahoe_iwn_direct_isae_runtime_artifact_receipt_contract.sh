#!/usr/bin/env bash
# Static and self-test gate for the guest-local direct-SAE artifact receipt.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
tool="$root/scripts/capture_tahoe_iwn_direct_sae_runtime_artifact_receipt.py"

fail() {
    printf 'FAIL: direct-SAE artifact receipt contract: %s\n' "$*" >&2
    exit 1
}

[ -x "$tool" ] || fail 'receipt tool missing or not executable'
python3 -m py_compile "$tool"
python3 "$tool" --self-test

python3 - "$tool" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")

required = (
    'itlwm-tahoe-iwn-direct-sae-lab-artifacts/v1',
    'guest-local-direct-sae-lab-artifact-binding',
    'ARTIFACT_DIR_RE',
    'artifact_probe_script',
    'pair-one-direct=',
    'pair-two-direct=',
    'direct_client_stable_during_capture',
    'trace_client_stable_during_capture',
    'object_pairs_hook=reject_duplicate_object_keys',
    'parse_constant=reject_nonfinite_json_constant',
    'os.O_EXCL',
    '0o600',
    'load_artifact_receipt',
    'require_artifact_dir',
    'PINNED_QEMU_BUILD',
    'StrictHostKeyChecking=yes',
    'GlobalKnownHostsFile=/dev/null',
)
for token in required:
    if token not in text:
        raise SystemExit(f'FAIL: direct-SAE artifact receipt lacks {token}')

forbidden = (
    'networksetup',
    'CoreWLAN',
    'Keychain',
    'IORegistry',
    '--direct-client-sha256',
    '--trace-client-sha256',
    'password',
    'passphrase',
    'ssid',
    'bssid',
    'dtrace -',
)
for token in forbidden:
    if token.lower() in text.lower():
        raise SystemExit(f'FAIL: direct-SAE artifact receipt exposes forbidden surface {token}')

capture = text[text.find('def make_receipt('):text.find('def validate_artifact_receipt_document(')]
if 'artifact_dir' in capture:
    raise SystemExit('FAIL: artifact receipt serializes guest artifact directory')
if 'runtime_experiment_performed": False' not in capture:
    raise SystemExit('FAIL: artifact receipt overclaims a runtime experiment')
if 'driver_identity_bound": False' not in capture:
    raise SystemExit('FAIL: artifact receipt overclaims loaded driver identity')
if text.count('digest "$artifact_dir/__DIRECT_CLIENT__"') != 2:
    raise SystemExit('FAIL: direct client is not double-hashed')
if text.count('digest "$artifact_dir/__TRACE_CLIENT__"') != 2:
    raise SystemExit('FAIL: trace client is not double-hashed')
if 'guest artifact changed during receipt capture' not in text:
    raise SystemExit('FAIL: artifact receipt does not reject hash drift')
print('PASS: direct-SAE artifact receipt static contract')
PY
