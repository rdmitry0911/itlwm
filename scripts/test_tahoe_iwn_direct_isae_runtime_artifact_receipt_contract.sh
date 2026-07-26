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

help_output="$(python3 "$tool" --help)"
case "$help_output" in
    *--harden-guest-artifacts*) ;;
    *) fail 'receipt tool does not expose root-only artifact hardening mode' ;;
esac

python3 - "$tool" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(encoding="utf-8")

required = (
    'itlwm-tahoe-iwn-direct-sae-lab-artifacts/v3',
    'guest-local-direct-sae-lab-artifact-binding',
    'pinned-guest-root-ancestry-isae-runtime-dir/v3',
    'ARTIFACT_DIR_RE',
    'STAGING_ARTIFACT_DIR_RE',
    'ROOT_ARTIFACT_PARENT',
    'ROOT_ARTIFACT_DIR',
    'artifact_probe_script',
    'harden_guest_artifact_script',
    'harden_guest_artifacts',
    '--harden-guest-artifacts',
    'pair-one-direct=',
    'pair-two-direct=',
    'artifact_parent_root_owned_nonwritable',
    'direct_client_root_owned_nonwritable',
    'trace_client_root_owned_nonwritable',
    'direct_client_stable_during_capture',
    'trace_client_stable_during_capture',
    'object_pairs_hook=reject_duplicate_object_keys',
    'parse_constant=reject_nonfinite_json_constant',
    'os.O_EXCL',
    '0o600',
    'load_artifact_receipt',
    'require_artifact_dir',
    'PINNED_QEMU_BUILD',
    'PINNED_HOST_KEY_FINGERPRINT',
    'trusted_host_environment',
    '/usr/bin/ssh',
    '/usr/bin/ssh-keygen',
    '/usr/bin/sudo',
    '/usr/sbin/chown root:wheel',
    '/usr/bin/install -d -o root -g wheel -m 700',
    '/bin/cp -pP',
    '/bin/chmod 700',
    '/bin/chmod 500',
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

probe = text[text.find('def artifact_probe_script('):text.find('def pinned_ssh(')]
for token in (
    'root_owned_nonwritable() {',
    "/usr/bin/stat -f '%u'",
    "/usr/bin/stat -f '%Lp'",
    'test "$owner" = 0',
    "'^[0-7][0145][0145]$'",
    'for root_path in /private /private/var /private/var/db __ROOT_ARTIFACT_PARENT__ "$artifact_dir"; do',
    'root_owned_nonwritable "$file"',
):
    if token not in probe:
        raise SystemExit(f'FAIL: receipt probe lacks root-owned nonwritable check: {token}')

harden = text[text.find('def harden_guest_artifact_script('):text.find('def make_receipt(')]
for token in (
    'staging_dir="$1"',
    'root_artifact_dir="__ROOT_ARTIFACT_DIR__"',
    'root_artifact_parent="__ROOT_ARTIFACT_PARENT__"',
    'test -d "$staging_dir" && test ! -L "$staging_dir"',
    'test ! -e "$root_artifact_dir" && test ! -L "$root_artifact_dir"',
    '/usr/bin/install -d -o root -g wheel -m 700 "$root_artifact_parent"',
    '/bin/cp -pP "$source_tool" "$destination_tool"',
    'test -f "$destination_tool" && test ! -L "$destination_tool" && test -x "$destination_tool"',
    '/usr/sbin/chown root:wheel',
    '/bin/chmod 700 "$root_artifact_dir"',
    '/bin/chmod 500 "$destination_tool"',
    'test "$before" = "$after" && test "$before" = "$copied"',
    'def harden_guest_artifacts(',
    '"/usr/bin/sudo", "-n", "/bin/bash", "-s", "--", staging_dir',
    'env=trusted_host_environment()',
):
    if token not in harden:
        raise SystemExit(f'FAIL: artifact hardening lacks {token}')

if harden.find('/usr/sbin/chown root:wheel "$root_artifact_dir"') > harden.find('/bin/chmod 700 "$root_artifact_dir"'):
    raise SystemExit('FAIL: artifact hardening changes modes before root ownership')

pinned_transport = text[text.find('def pinned_ssh('):text.find('def parse_probe_output(')]
for token in (
    '"/usr/bin/ssh"',
    '"/usr/bin/ssh-keygen"',
    'fields[1] != PINNED_HOST_KEY_FINGERPRINT',
    'env=trusted_host_environment()',
):
    if token not in pinned_transport:
        raise SystemExit(f'FAIL: artifact transport lacks pinned trusted-host control: {token}')
if '"ssh"' in pinned_transport or '"ssh-keygen"' in pinned_transport:
    raise SystemExit('FAIL: artifact transport retains a PATH-resolved host tool')

host_environment = text[text.find('def trusted_host_environment('):text.find('def require_regular_file(')]
for token in (
    '"PATH": "/usr/bin:/bin"',
    '"LC_ALL": "C"',
    '"LANG": "C"',
    '"HOME": pwd.getpwuid(os.getuid()).pw_dir',
):
    if token not in host_environment:
        raise SystemExit(f'FAIL: artifact transport host environment lacks {token}')

capture = text[text.find('def capture_guest_artifacts('):text.find('def harden_guest_artifact_script(')]
for token in (
    '"/usr/bin/sudo", "-n", "/bin/bash", "-s", "--", artifact_dir',
    'env=trusted_host_environment()',
):
    if token not in capture:
        raise SystemExit(f'FAIL: artifact capture lacks root-only pinned execution: {token}')

main = text[text.find('def main() -> int:'):]
for token in (
    'parser.add_argument("--harden-guest-artifacts", action="store_true")',
    'if args.harden_guest_artifacts:',
    'harden_guest_artifacts(args.guest_artifact_dir)',
    'artifact-directory=hardened',
):
    if token not in main:
        raise SystemExit(f'FAIL: artifact hardening mode lacks {token}')
print('PASS: direct-SAE artifact receipt static contract')
PY
