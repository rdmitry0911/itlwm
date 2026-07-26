#!/usr/bin/env bash
# Static contract for the guest-only, next-boot AuxKC activation helper.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
HELPER="$ROOT/scripts/tahoe_auxkc_activate_release.sh"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    local literal="$1"
    local label="$2"
    grep -Fq -- "$literal" "$HELPER" ||
        fail "missing $label: $literal"
}

test -x "$HELPER" || fail "activation helper is not executable"
bash -n "$HELPER"
bash "$HELPER" --help >/dev/null 2>&1

require_literal 'require_private_path "$candidate" "candidate"' 'candidate private-path scope'
require_literal 'require_private_path "$work_root" "work root"' 'work-root private-path scope'
require_literal '--baseline /private/path/canonical-baseline.json' 'root-owned baseline input'
require_literal '[ "$baseline" = "$work_root/canonical-baseline.json" ]' 'baseline root binding'
require_literal 'value.st_uid != 0' 'baseline root ownership gate'
require_literal 'stat.S_IMODE(value.st_mode) != 0o600' 'baseline private-mode gate'
require_literal 'validate_canonical_baseline' 'pre-transaction baseline revalidation'
require_literal '[ "$(/usr/bin/id -u)" -eq 0 ] || fail "must run as root"' 'root-only helper boundary'
require_literal 'validate_root_owned_tree() {' 'physical tree validator'
require_literal 'validate_bridge_root() {' 'bridge root validator'
require_literal 'validate_bridge_root "$work_root"' 'work-root sealed binding'
require_literal '[ "$candidate" = "$work_root/preflight/AirportItlwm.kext" ]' 'bridge preflight candidate binding'
require_literal 'validate_root_owned_tree "$candidate"' 'candidate physical tree binding'
require_literal '/usr/bin/ditto --norsrc --noacl --noextattr --noqtn "$candidate" "$new_bundle"' 'metadata-stripped candidate staging'
require_literal 'validate_root_owned_tree "$work"' 'work tree physical validation before cleanup'
require_literal 'validate_root_owned_tree "$new_bundle"' 'candidate tree physical validation before cleanup'
require_literal 'validate_baseline_companion_members "$work/candidate_auxkc.members"' 'candidate companion-members fence'
require_literal 'validate_baseline_companion_members "$work/canonical_after.members"' 'post-swap companion-members fence'
require_literal 'validate_auxkc "$CANONICAL_AUXKC" "canonical_before"' 'pre-swap exact member validation'
require_literal 'validate_auxkc "$new_auxkc" "candidate_auxkc" "$expected_uuid"' 'new collection candidate UUID validation'
require_literal 'validate_auxkc "$CANONICAL_AUXKC" "canonical_after" "$expected_uuid"' 'post-swap candidate UUID validation'
require_literal 'bundle_backup=' 'bundle rollback copy'
require_literal 'auxkc_backup=' 'AuxKC rollback copy'
require_literal 'rollback_bundle' 'bundle rollback path'
require_literal 'rollback_auxkc' 'AuxKC rollback path'
require_literal "trap 'emergency_rollback \$?' EXIT" 'exit rollback trap'
require_literal "trap 'exit 129' HUP" 'HUP rollback path'
require_literal "trap 'exit 130' INT" 'INT rollback path'
require_literal "trap 'exit 143' TERM" 'TERM rollback path'
require_literal 'ACTIVATION_ABORT_ROLLED_BACK' 'categorical abort rollback witness'
require_literal 'transaction_armed=1' 'pre-mutation transaction arm'
require_literal 'transaction_armed=0' 'post-summary transaction disarm'
require_literal 'activation_state=READY_FOR_GUEST_REBOOT' 'explicit next-boot boundary'

if grep -nE 'kextload|kextunload|/sbin/reboot|kmutil[[:space:]]+(load|unload)|rm[[:space:]]+-rf' "$HELPER"; then
    fail 'helper contains a prohibited direct-load, reboot, or destructive removal command'
fi

python3 - "$HELPER" <<'PY'
from pathlib import Path
import sys


helper = Path(sys.argv[1]).read_text(encoding="utf-8")


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: Tahoe next-boot AuxKC activation helper: {message}")


def body(text: str, marker: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing function: {marker}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing function body: {marker}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    fail(f"unterminated function: {marker}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} misses ordered token: {token}")
        cursor = position + len(token)


abort = body(helper, "emergency_rollback()")
ordered(abort, "abort rollback order",
        "trap - EXIT HUP INT TERM",
        '[ "$transaction_armed" = 1 ] || exit "$status"',
        "rollback_in_progress=1",
        "rollback_auxkc",
        "rollback_bundle",
        "ACTIVATION_ABORT_ROLLED_BACK")

main_start = helper.find('candidate_sha="$(/usr/bin/shasum')
if main_start < 0:
    fail("activation main section is missing")
main = helper[main_start:]
ordered(main, "transaction arm/disarm order",
        "validate_canonical_baseline",
        "transaction_armed=1",
        '/bin/mv "$CANONICAL_BUNDLE" "$bundle_displaced"',
        '/bin/mv "$CANONICAL_AUXKC" "$auxkc_displaced"',
        'activation_state=READY_FOR_GUEST_REBOOT',
        'transaction_armed=0',
        'ACTIVATION_READY:')

tree_validator = body(helper, "validate_root_owned_tree()")
for token in ('os.lstat', 'stat.S_ISLNK', 'stat.S_ISDIR', 'stat.S_ISREG',
              'value.st_nlink != 1', '0o7022', 'os.scandir'):
    if token not in tree_validator:
        fail("physical tree validator misses: " + token)
bridge_root = body(helper, "validate_bridge_root()")
for token in ('/private/var/tmp/aiam-iwn-activation-', '("/private/var", False)',
              '("/private/var/tmp", True)', 'stat.S_ISVTX',
              'value.st_uid != 0', 'stat.S_IMODE(value.st_mode) != 0o700'):
    if token not in bridge_root:
        fail("bridge-root validator misses: " + token)
ordered(bridge_root, "sealed work-root ACL cleanup",
        '/bin/chmod -N "$root"', '/bin/chmod 700 "$root"')
ordered(main, "validate before recursive metadata cleanup",
        'validate_root_owned_tree "$work"',
        'clear_untrusted_metadata "$work"',
        '/usr/bin/ditto --norsrc --noacl --noextattr --noqtn "$candidate" "$new_bundle"',
        'validate_root_owned_tree "$new_bundle"',
        'clear_untrusted_metadata "$new_bundle"')
ordered(main, "companion-members rollback fences",
        'validate_auxkc "$new_auxkc" "candidate_auxkc" "$expected_uuid"',
        'validate_baseline_companion_members "$work/candidate_auxkc.members"',
        '/bin/mv "$CANONICAL_AUXKC" "$auxkc_displaced"',
        'validate_auxkc "$CANONICAL_AUXKC" "canonical_after" "$expected_uuid"',
        'validate_baseline_companion_members "$work/canonical_after.members"',
        'transaction_armed=0')

collision_guard = helper[helper.find('for path in "$work"'):main_start]
for token in ('$bundle_failed', '$auxkc_failed'):
    if token not in collision_guard:
        fail("rollback quarantine path missing from collision guard: " + token)

if main.find("transaction_armed=0") < main.find('activation_state=READY_FOR_GUEST_REBOOT'):
    fail("transaction disarmed before durable READY summary")

print("PASS: Tahoe next-boot AuxKC activation abort-rollback contract")
PY

printf 'PASS: Tahoe next-boot AuxKC activation helper static contract\n'
