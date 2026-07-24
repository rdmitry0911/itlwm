#!/usr/bin/env bash
# Static and synthetic contract for the host-side Tahoe overlay preparer.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
HELPER="$ROOT/scripts/tahoe_prepare_disposable_overlay.sh"
FIXTURE="$ROOT/scripts/test_tahoe_prepare_disposable_overlay_fixture.sh"
EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_disposable_overlay_evidence_contract.sh"
PROTOCOL="$ROOT/docs/TAHOE_DISPOSABLE_OVERLAY_PROTOCOL.md"

fail() {
    printf 'FAIL: Tahoe disposable-overlay contract: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    local path="$1" needle="$2" label="$3"
    grep -Fq -- "$needle" "$path" || fail "missing $label"
}

forbid_literal() {
    local path="$1" needle="$2" label="$3"
    ! grep -Fq -- "$needle" "$path" || fail "forbidden $label"
}

for path in "$HELPER" "$FIXTURE" "$EVIDENCE_CONTRACT" "$PROTOCOL"; do
    [ -f "$path" ] || fail "required file is missing: ${path##*/}"
done
[ -x "$HELPER" ] && [ -x "$FIXTURE" ] && [ -x "$EVIDENCE_CONTRACT" ] ||
    fail 'overlay scripts must be executable'
bash -n "$HELPER"
bash -n "$FIXTURE"
"$EVIDENCE_CONTRACT" --self-test
"$HELPER" --help >/dev/null 2>&1

for needle in \
    'readonly DISK_NAME="tahoe-pmf-runtime.qcow2"' \
    'readonly ATTESTATION_NAME="overlay-attestation.json"' \
    'safe_output_leaf' \
    'fuser-unavailable' \
    'base-image-in-use' \
    'base-image-not-direct-qcow2' \
    'mktemp -d "$vm_root/.aiam-overlay-stage.XXXXXX"' \
    'base_info_path="$STAGING_DIR/base-info.json"' \
    'overlay_map_path="$STAGING_DIR/overlay-map.json"' \
    '/usr/bin/unlink "$transient_metadata"' \
    'create -f qcow2 -F qcow2 -b "$base_image" "$overlay_image"' \
    'one_direct_backing_image_verified' \
    'top_overlay_data_allocated' \
    'base_image_mutated_by_helper' \
    'candidate_or_auxkc_mutated_by_helper' \
    'disk_selector_environment_variable' \
    'depth < 1' \
    '/bin/mv -T -n -- "$STAGING_DIR" "$final_dir"'; do
    require_literal "$HELPER" "$needle" "overlay safety token: $needle"
done

for needle in \
    'qemu-system' \
    'boot-macOS' \
    'ssh ' \
    'kextload' \
    'kextunload' \
    'kmutil ' \
    '/sbin/reboot' \
    'shutdown -r' \
    'hostapd' \
    'route add' \
    'route delete' \
    'rm -rf' \
    'git '; do
    forbid_literal "$HELPER" "$needle" "helper capability: $needle"
done

python3 - "$HELPER" "$PROTOCOL" <<'PY'
from pathlib import Path
import re
import sys

helper = Path(sys.argv[1]).read_text(encoding="utf-8")
protocol = Path(sys.argv[2]).read_text(encoding="utf-8")


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: Tahoe disposable-overlay contract: {message}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} misses ordered token: {token}")
        cursor = position + len(token)


main_start = helper.find('base_image="$(canonical_regular_file "$BASE_IMAGE")"')
if main_start < 0:
    fail("helper main entry is missing")
main = helper[main_start:]
ordered(main, "fresh-overlay transaction",
        '"$FUSER" -s -- "$base_image"',
        'mktemp -d "$vm_root/.aiam-overlay-stage.XXXXXX"',
        'base_info_path="$STAGING_DIR/base-info.json"',
        'validate_base_info "$base_info_path"',
        'create -f qcow2 -F qcow2 -b "$base_image" "$overlay_image"',
        'map --output=json "$overlay_image" >"$overlay_map_path"',
        'write_and_validate_attestation',
        '/usr/bin/unlink "$transient_metadata"',
        '/bin/mv -T -n -- "$STAGING_DIR" "$final_dir"',
        'STAGING_DIR=""',
        "OVERLAY_READY")

for forbidden, label in (
    ('write_and_validate_attestation "$base_info_json"',
     "serialized qemu metadata passed through argv"),
    ('write_and_validate_attestation "$overlay_info_json"',
     "serialized overlay metadata passed through argv"),
    ('write_and_validate_attestation "$overlay_map_json"',
     "serialized overlay map passed through argv"),
):
    if forbidden in helper:
        fail(f"helper retains {label}")

for token in (
    "one direct backing", "read-only", "does not boot", "does not activate",
    "local-only", "fresh", "AP preflight",
):
    if token not in protocol:
        fail(f"protocol omits boundary: {token}")

for pattern, label in (
    (r"\b(?:\d{1,3}\.){3}\d{1,3}\b", "IPv4 literal"),
    (r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", "MAC literal"),
):
    if re.search(pattern, protocol):
        fail(f"protocol contains {label}")

print("PASS: Tahoe disposable-overlay static contract")
PY

"$FIXTURE"
