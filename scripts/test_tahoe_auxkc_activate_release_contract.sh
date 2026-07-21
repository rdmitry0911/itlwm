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
    grep -Fq "$literal" "$HELPER" ||
        fail "missing $label: $literal"
}

test -x "$HELPER" || fail "activation helper is not executable"
bash -n "$HELPER"
bash "$HELPER" --help >/dev/null 2>&1

require_literal 'require_private_path "$candidate" "candidate"' 'candidate private-path scope'
require_literal 'require_private_path "$work_root" "work root"' 'work-root private-path scope'
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
        "transaction_armed=1",
        '/bin/mv "$CANONICAL_BUNDLE" "$bundle_displaced"',
        '/bin/mv "$CANONICAL_AUXKC" "$auxkc_displaced"',
        'activation_state=READY_FOR_GUEST_REBOOT',
        'transaction_armed=0',
        'ACTIVATION_READY:')

if main.find("transaction_armed=0") < main.find('activation_state=READY_FOR_GUEST_REBOOT'):
    fail("transaction disarmed before durable READY summary")

print("PASS: Tahoe next-boot AuxKC activation abort-rollback contract")
PY

printf 'PASS: Tahoe next-boot AuxKC activation helper static contract\n'
