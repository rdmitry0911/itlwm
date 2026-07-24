#!/usr/bin/env bash
# Exercise the disposable-overlay preparer only with synthetic qcow2 images.
# No guest is started and no project image is ever opened for writing.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
HELPER="$ROOT/scripts/tahoe_prepare_disposable_overlay.sh"
EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_disposable_overlay_evidence_contract.sh"
TMP=""
BASE_HOLDER=""
OVMF_VARS_HOLDER=""

fail() {
    printf 'FAIL: Tahoe disposable-overlay fixture: %s\n' "$*" >&2
    exit 1
}

cleanup() {
    local status="$?"
    trap - EXIT HUP INT TERM
    if [ -n "$BASE_HOLDER" ]; then
        kill "$BASE_HOLDER" >/dev/null 2>&1 || true
        wait "$BASE_HOLDER" 2>/dev/null || true
    fi
    if [ -n "$OVMF_VARS_HOLDER" ]; then
        kill "$OVMF_VARS_HOLDER" >/dev/null 2>&1 || true
        wait "$OVMF_VARS_HOLDER" 2>/dev/null || true
    fi
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
OVMF_VARS_TEMPLATE="$TMP/ovmf-vars-template.fd"
mkdir "$VM_ROOT"
qemu-img create -q -f qcow2 "$BASE" 32M
/usr/bin/dd if=/dev/zero of="$OVMF_VARS_TEMPLATE" bs=4096 count=16 status=none
BASE_BEFORE="$(sha256sum "$BASE" | awk '{print $1}')"
OVMF_VARS_TEMPLATE_BEFORE="$(sha256sum "$OVMF_VARS_TEMPLATE" | awk '{print $1}')"

READY_OUTPUT="$("$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
    --out-dir pmf-runtime-fixture)"
[ "$READY_OUTPUT" = 'OVERLAY_READY' ] || fail 'helper did not report readiness'

FINAL_DIR="$VM_ROOT/pmf-runtime-fixture"
OVERLAY="$FINAL_DIR/tahoe-pmf-runtime.qcow2"
ATTESTATION="$FINAL_DIR/overlay-attestation.json"
[ -d "$FINAL_DIR" ] && [ ! -L "$FINAL_DIR" ] || fail 'fresh output directory is missing'
[ -f "$OVERLAY" ] && [ ! -L "$OVERLAY" ] || fail 'fresh overlay is missing'
[ -f "$ATTESTATION" ] && [ ! -L "$ATTESTATION" ] || fail 'attestation is missing'
[ ! -e "$FINAL_DIR/OVMF_VARS-1920x1080.fd" ] &&
    [ ! -L "$FINAL_DIR/OVMF_VARS-1920x1080.fd" ] ||
    fail 'v1 overlay unexpectedly contains an OVMF vars copy'
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
python3 - "$ATTESTATION" <<'PY'
import json
import sys

document = json.load(open(sys.argv[1], encoding="utf-8"))
if document.get("schema") != "itlwm-tahoe-disposable-overlay/v1":
    raise SystemExit("v1 schema changed without an OVMF template")
if "ovmf_vars" in document:
    raise SystemExit("v1 receipt gained an OVMF section")
PY

OVMF_READY_OUTPUT="$("$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
    --out-dir pmf-runtime-ovmf --ovmf-vars-template "$OVMF_VARS_TEMPLATE")"
[ "$OVMF_READY_OUTPUT" = 'OVERLAY_READY' ] ||
    fail 'helper did not report OVMF-overlay readiness'
OVMF_FINAL_DIR="$VM_ROOT/pmf-runtime-ovmf"
OVMF_OVERLAY="$OVMF_FINAL_DIR/tahoe-pmf-runtime.qcow2"
OVMF_ATTESTATION="$OVMF_FINAL_DIR/overlay-attestation.json"
OVMF_VARS_COPY="$OVMF_FINAL_DIR/OVMF_VARS-1920x1080.fd"
[ -d "$OVMF_FINAL_DIR" ] && [ ! -L "$OVMF_FINAL_DIR" ] ||
    fail 'OVMF output directory is missing'
[ -f "$OVMF_OVERLAY" ] && [ ! -L "$OVMF_OVERLAY" ] ||
    fail 'OVMF overlay is missing'
[ -f "$OVMF_ATTESTATION" ] && [ ! -L "$OVMF_ATTESTATION" ] ||
    fail 'OVMF attestation is missing'
[ -f "$OVMF_VARS_COPY" ] && [ ! -L "$OVMF_VARS_COPY" ] ||
    fail 'OVMF vars copy is missing'
[ "$(stat -c '%a' "$OVMF_VARS_COPY")" = '600' ] ||
    fail 'OVMF vars copy permissions changed'
[ "$(stat -c '%d:%i' "$OVMF_VARS_TEMPLATE")" != \
    "$(stat -c '%d:%i' "$OVMF_VARS_COPY")" ] ||
    fail 'OVMF vars copy shares the template inode'
[ "$(sha256sum "$OVMF_VARS_TEMPLATE" | awk '{print $1}')" = \
    "$OVMF_VARS_TEMPLATE_BEFORE" ] || fail 'OVMF vars template was changed'
[ "$(sha256sum "$OVMF_VARS_COPY" | awk '{print $1}')" = \
    "$OVMF_VARS_TEMPLATE_BEFORE" ] || fail 'OVMF vars copy hash differs from template'
"$EVIDENCE_CONTRACT" --attestation "$OVMF_ATTESTATION"
python3 - "$OVMF_ATTESTATION" "$OVMF_VARS_TEMPLATE" "$OVMF_VARS_COPY" <<'PY'
import hashlib
import json
import os
import sys

document = json.load(open(sys.argv[1], encoding="utf-8"))
template = open(sys.argv[2], "rb").read()
copy = open(sys.argv[3], "rb").read()
expected = hashlib.sha256(template).hexdigest()
ovmf_vars = document.get("ovmf_vars", {})
if document.get("schema") != "itlwm-tahoe-disposable-overlay/v2":
    raise SystemExit("v2 schema missing with OVMF template")
if hashlib.sha256(copy).hexdigest() != expected:
    raise SystemExit("copy hash differs from template")
if ovmf_vars.get("template_sha256") != expected:
    raise SystemExit("template digest does not bind the source")
if ovmf_vars.get("copy_sha256") != expected:
    raise SystemExit("copy digest does not bind the published copy")
if ovmf_vars.get("copy_size_bytes") != len(copy):
    raise SystemExit("copy size does not bind the published copy")
if os.stat(sys.argv[2]).st_ino == os.stat(sys.argv[3]).st_ino and \
        os.stat(sys.argv[2]).st_dev == os.stat(sys.argv[3]).st_dev:
    raise SystemExit("copy inode is not distinct")
PY

if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-fixture >/dev/null 2>&1; then
    fail 'helper accepted an existing output directory'
fi

if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-ovmf-duplicate \
        --ovmf-vars-template "$OVMF_VARS_TEMPLATE" \
        --ovmf-vars-template "$OVMF_VARS_TEMPLATE" >/dev/null 2>&1; then
    fail 'helper accepted a duplicate OVMF vars template option'
fi

if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-ovmf-relative \
        --ovmf-vars-template relative-template.fd >/dev/null 2>&1; then
    fail 'helper accepted a relative OVMF vars template'
fi

OVMF_VARS_EMPTY="$TMP/ovmf-vars-empty.fd"
: >"$OVMF_VARS_EMPTY"
if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-ovmf-empty \
        --ovmf-vars-template "$OVMF_VARS_EMPTY" >/dev/null 2>&1; then
    fail 'helper accepted an empty OVMF vars template'
fi

OVMF_VARS_LINK="$TMP/ovmf-vars-link.fd"
ln -s "$OVMF_VARS_TEMPLATE" "$OVMF_VARS_LINK"
if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-ovmf-link \
        --ovmf-vars-template "$OVMF_VARS_LINK" >/dev/null 2>&1; then
    fail 'helper accepted a symlinked OVMF vars template'
fi

CHAINED="$TMP/chained.qcow2"
qemu-img create -q -f qcow2 -F qcow2 -b "$BASE" "$CHAINED"
if "$HELPER" --base-image "$CHAINED" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-chain >/dev/null 2>&1; then
    fail 'helper accepted a backing-image chain'
fi

# Hold the root image open in a child process.  This exercises the actual
# fuser invocation rather than merely asserting that the source mentions the
# rejection label; a live QEMU root image must be rejected before staging.
( exec 9<"$BASE"; sleep 60 ) &
BASE_HOLDER="$!"
for _ in $(seq 1 20); do
    if fuser -s "$BASE" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
fuser -s "$BASE" >/dev/null 2>&1 || fail 'fixture could not hold base image open'
if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-in-use >/dev/null 2>&1; then
    fail 'helper accepted an in-use base image'
fi
kill "$BASE_HOLDER" >/dev/null 2>&1 || true
wait "$BASE_HOLDER" 2>/dev/null || true
BASE_HOLDER=""

# The OVMF variables template is an input to the disposable pair as well.
# It must be rejected while another process holds it, before any staging copy
# can be published.
( exec 9<"$OVMF_VARS_TEMPLATE"; sleep 60 ) &
OVMF_VARS_HOLDER="$!"
for _ in $(seq 1 20); do
    if fuser -s "$OVMF_VARS_TEMPLATE" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
fuser -s "$OVMF_VARS_TEMPLATE" >/dev/null 2>&1 ||
    fail 'fixture could not hold OVMF vars template open'
if "$HELPER" --base-image "$BASE" --vm-root "$VM_ROOT" \
        --out-dir pmf-runtime-ovmf-in-use \
        --ovmf-vars-template "$OVMF_VARS_TEMPLATE" >/dev/null 2>&1; then
    fail 'helper accepted an in-use OVMF vars template'
fi
kill "$OVMF_VARS_HOLDER" >/dev/null 2>&1 || true
wait "$OVMF_VARS_HOLDER" 2>/dev/null || true
OVMF_VARS_HOLDER=""

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
[ "$(sha256sum "$OVMF_VARS_TEMPLATE" | awk '{print $1}')" = \
    "$OVMF_VARS_TEMPLATE_BEFORE" ] ||
    fail 'negative cases changed the OVMF vars template'
printf 'PASS: Tahoe disposable-overlay synthetic fixture\n'
