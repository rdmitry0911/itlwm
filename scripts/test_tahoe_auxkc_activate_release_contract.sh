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
    rg -F --quiet "$literal" "$HELPER" ||
        fail "missing $label: $literal"
}

test -x "$HELPER" || fail "activation helper is not executable"
bash -n "$HELPER"
bash "$HELPER" --help >/dev/null

require_literal 'require_private_path "$candidate" "candidate"' 'candidate private-path scope'
require_literal 'require_private_path "$work_root" "work root"' 'work-root private-path scope'
require_literal 'validate_auxkc "$CANONICAL_AUXKC" "canonical_before"' 'pre-swap exact member validation'
require_literal 'validate_auxkc "$new_auxkc" "candidate_auxkc" "$expected_uuid"' 'new collection candidate UUID validation'
require_literal 'validate_auxkc "$CANONICAL_AUXKC" "canonical_after" "$expected_uuid"' 'post-swap candidate UUID validation'
require_literal 'bundle_backup=' 'bundle rollback copy'
require_literal 'auxkc_backup=' 'AuxKC rollback copy'
require_literal 'rollback_bundle' 'bundle rollback path'
require_literal 'rollback_auxkc' 'AuxKC rollback path'
require_literal 'activation_state=READY_FOR_GUEST_REBOOT' 'explicit next-boot boundary'

if rg -n 'kextload|kextunload|/sbin/reboot|kmutil[[:space:]]+(load|unload)|rm[[:space:]]+-rf' "$HELPER"; then
    fail 'helper contains a prohibited direct-load, reboot, or destructive removal command'
fi

printf 'PASS: Tahoe next-boot AuxKC activation helper static contract\n'
