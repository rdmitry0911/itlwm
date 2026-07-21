#!/usr/bin/env bash
# Exercise the disposable-overlay preparer only with synthetic qcow2 images.
# No guest is started and no project image is ever opened for writing.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
HELPER="$ROOT/scripts/tahoe_prepare_disposable_overlay.sh"
EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_disposable_overlay_evidence_contract.sh"
TMP=""

fail() {
    printf 'FAIL: Tahoe disposable-overlay fixture: %s\n' "$*" >&2
    exit 1
}

cleanup() {
    local status="$?"
    trap - EXIT HUP INT TERM
    if [ -n "$TMP" ] && [ -d "$TMP" ]; then
        /usr/bin/find -P "$TMP" -depth -delete >/dev/null 2>&1 || true
    fi
    exit "$status"
}

trap cleanup EXIT
trap 'exit 1' HUP INT TERM

[ -x "$HELPER" ] || fail 'helper is not executable'
[ -x "$EVIDENCE_CONTRACT" ] || fail 'evidence contract is not executable'
command -v qemu-img >/dev/null 2>&1 || fail 'qemu-img is unavailable'
TMP="$(mktemp -d /tmp/aiam-tahoe-overlay-fixture.XXXXXX)"
VM_ROOT="$TMP/vm-root"
BASE="$TMP/base.qcow2"
mkdir "$VM_ROOT"
qemu-img create -q -f qcow2 "$BASE" 32M
BASE_BEFORE="$(sha256sum "$BASE" | awk '{print $1}')"

READY_OUTPUT="$("$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
    --out-dir pmf-runtime-fixture)"
[ "$READY_OUTPUT" = 'OVERLAY_READY' ] || fail 'helper did not report readiness'

FINAL_DIR="$VM_ROOT/pmf-runtime-fixture"
OVERLAY="$FINAL_DIR/tahoe-pmf-runtime.qcow2"
ATTESTATION="$FINAL_DIR/overlay-attestation.json"
[ -d "$FINAL_DIR" ] && [ ! -L "$FINAL_DIR" ] || fail 'fresh output directory is missing'
[ -f "$OVERLAY" ] && [ ! -L "$OVERLAY" ] || fail 'fresh overlay is missing'
[ -f "$ATTESTATION" ] && [ ! -L "$ATTESTATION" ] || fail 'attestation is missing'
[ "$(stat -c '%a' "$FINAL_DIR")" = '700' ] || fail 'output directory permissions changed'
[ "$(stat -c '%a' "$OVERLAY")" = '600' ] || fail 'overlay permissions changed'
[ "$(stat -c '%a' "$ATTESTATION")" = '600' ] || fail 'attestation permissions changed'
[ "$(sha256sum "$BASE" | awk '{print $1}')" = "$BASE_BEFORE" ] ||
    fail 'base image was changed'

OVERLAY_INFO_JSON="$(qemu-img info --output=json "$OVERLAY")"
python3 - "$BASE" "$OVERLAY_INFO_JSON" <<'PY'
import json
import os
import sys

info = json.loads(sys.argv[2])
base = os.path.realpath(sys.argv[1])
if info.get("format") != "qcow2":
    raise SystemExit("overlay is not qcow2")
if info.get("backing-filename-format") != "qcow2":
    raise SystemExit("overlay backing format changed")
if os.path.realpath(str(info.get("full-backing-filename", ""))) != base:
    raise SystemExit("overlay backing relation changed")
PY

OVERLAY_MAP_JSON="$(qemu-img map --output=json "$OVERLAY")"
python3 - "$OVERLAY_MAP_JSON" <<'PY'
import json
import sys

rows = json.loads(sys.argv[1])
if not isinstance(rows, list) or not rows:
    raise SystemExit("overlay map is empty")
if any(not isinstance(row, dict) or row.get("depth", 0) < 1 for row in rows):
    raise SystemExit("fresh overlay has top-level data")
PY

"$EVIDENCE_CONTRACT" --attestation "$ATTESTATION"

if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-fixture >/dev/null 2>&1; then
    fail 'helper accepted an existing output directory'
fi

CHAINED="$TMP/chained.qcow2"
qemu-img create -q -f qcow2 -F qcow2 -b "$BASE" "$CHAINED"
if "$HELPER" --base-image "$CHAINED" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-chain >/dev/null 2>&1; then
    fail 'helper accepted a backing-image chain'
fi

ln -s "$TMP/absent" "$VM_ROOT/pmf-runtime-symlink"
if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-symlink >/dev/null 2>&1; then
    fail 'helper accepted a symlinked output directory'
fi

ln -s "$BASE" "$TMP/base-link.qcow2"
if "$HELPER" --base-image "$TMP/base-link.qcow2" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-base-link >/dev/null 2>&1; then
    fail 'helper accepted a symlinked base image'
fi

[ "$(sha256sum "$BASE" | awk '{print $1}')" = "$BASE_BEFORE" ] ||
    fail 'negative cases changed the base image'
printf 'PASS: Tahoe disposable-overlay synthetic fixture\n'
