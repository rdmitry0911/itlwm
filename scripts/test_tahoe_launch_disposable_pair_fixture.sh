#!/usr/bin/env bash
# Synthetic check-only fixture for the Tahoe v2 disposable-pair launcher.
# It prepares only small local qcow2 files and never invokes QEMU or a user
# service manager.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PREPARER="$ROOT/scripts/tahoe_prepare_disposable_overlay.sh"
LAUNCHER="$ROOT/scripts/tahoe_launch_disposable_pair.sh"

TMP=""

fail() {
    printf 'FAIL: Tahoe disposable-pair launcher fixture: %s\n' "$*" >&2
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

[ -x "$PREPARER" ] || fail 'preparer is not executable'
[ -x "$LAUNCHER" ] || fail 'launcher is not executable'
command -v qemu-img >/dev/null 2>&1 || fail 'qemu-img is unavailable'
command -v qemu-io >/dev/null 2>&1 || fail 'qemu-io is unavailable'

TMP="$(mktemp -d /tmp/aiam-tahoe-disposable-pair-fixture.XXXXXX)"
VM_ROOT="$TMP/vm-root"
BASE="$TMP/base.qcow2"
VARS_TEMPLATE="$TMP/ovmf-vars-template.fd"
OVMF_CODE="$TMP/OVMF_CODE.fd"
FAKE_QEMU="$TMP/qemu-system-x86_64"
MOCK_BIN="$TMP/mock-bin"
QEMU_MARKER="$TMP/qemu-invoked"
SYSTEMD_MARKER="$TMP/systemd-invoked"
mkdir "$VM_ROOT" "$MOCK_BIN"
/usr/bin/qemu-img create -q -f qcow2 "$BASE" 32M
/usr/bin/dd if=/dev/zero of="$VARS_TEMPLATE" bs=4096 count=16 status=none
/usr/bin/dd if=/dev/zero of="$OVMF_CODE" bs=4096 count=16 status=none

printf '%s\n' '#!/bin/sh' \
    ': >"$PAIR_LAUNCH_QEMU_MARKER"' \
    'exit 99' >"$FAKE_QEMU"
printf '%s\n' '#!/bin/sh' \
    ': >"$PAIR_LAUNCH_SYSTEMD_MARKER"' \
    'exit 99' >"$MOCK_BIN/systemd-run"
chmod 700 "$FAKE_QEMU" "$MOCK_BIN/systemd-run"
export PAIR_LAUNCH_QEMU_MARKER="$QEMU_MARKER"
export PAIR_LAUNCH_SYSTEMD_MARKER="$SYSTEMD_MARKER"

"$PREPARER" --base-image "$BASE" --vm-root "$VM_ROOT" --out-dir pair \
    --ovmf-vars-template "$VARS_TEMPLATE" >/dev/null
PAIR="$VM_ROOT/pair"
VARS="$PAIR/OVMF_VARS-1920x1080.fd"
OVERLAY="$PAIR/tahoe-pmf-runtime.qcow2"

READY="$(PATH="$MOCK_BIN:$PATH" "$LAUNCHER" --pair-dir "$PAIR" \
    --vm-root "$VM_ROOT" --qemu-bin "$FAKE_QEMU" --ovmf-code "$OVMF_CODE" \
    --vfio-pci 0000:00:00.0 --check-only)"
[ "$READY" = 'DISPOSABLE_PAIR_CHECK_READY' ] ||
    fail 'check-only did not report pair readiness'
[ ! -e "$QEMU_MARKER" ] && [ ! -e "$SYSTEMD_MARKER" ] ||
    fail 'check-only invoked QEMU or the user service manager'
[ ! -e "$PAIR/.aiam-disposable-pair-consumed" ] ||
    fail 'check-only consumed the pair'
[ ! -e "$PAIR/qemu-monitor.sock" ] && [ ! -e "$PAIR/qemu-serial.log" ] ||
    fail 'check-only created runtime artifacts'

touch "$PAIR/.aiam-disposable-pair-consumed"
if PATH="$MOCK_BIN:$PATH" "$LAUNCHER" --pair-dir "$PAIR" --vm-root "$VM_ROOT" \
        --qemu-bin "$FAKE_QEMU" --ovmf-code "$OVMF_CODE" \
        --vfio-pci 0000:00:00.0 --check-only >/dev/null 2>&1; then
    fail 'check-only accepted a consumed pair'
fi
/usr/bin/unlink "$PAIR/.aiam-disposable-pair-consumed"

chmod 640 "$VARS"
if PATH="$MOCK_BIN:$PATH" "$LAUNCHER" --pair-dir "$PAIR" --vm-root "$VM_ROOT" \
        --qemu-bin "$FAKE_QEMU" --ovmf-code "$OVMF_CODE" \
        --vfio-pci 0000:00:00.0 --check-only >/dev/null 2>&1; then
    fail 'check-only accepted a private variables file with the wrong mode'
fi
chmod 600 "$VARS"

cp -- "$VARS" "$TMP/original-vars.fd"
printf '\001' | /usr/bin/dd of="$VARS" bs=1 count=1 conv=notrunc status=none
if PATH="$MOCK_BIN:$PATH" "$LAUNCHER" --pair-dir "$PAIR" --vm-root "$VM_ROOT" \
        --qemu-bin "$FAKE_QEMU" --ovmf-code "$OVMF_CODE" \
        --vfio-pci 0000:00:00.0 --check-only >/dev/null 2>&1; then
    fail 'check-only accepted a private variables hash mismatch'
fi
cp -- "$TMP/original-vars.fd" "$VARS"
chmod 600 "$VARS"

"$PREPARER" --base-image "$BASE" --vm-root "$VM_ROOT" --out-dir allocated \
    --ovmf-vars-template "$VARS_TEMPLATE" >/dev/null
ALLOCATED_PAIR="$VM_ROOT/allocated"
/usr/bin/qemu-io -c 'write 0 512' \
    "$ALLOCATED_PAIR/tahoe-pmf-runtime.qcow2" >/dev/null
if PATH="$MOCK_BIN:$PATH" "$LAUNCHER" --pair-dir "$ALLOCATED_PAIR" \
        --vm-root "$VM_ROOT" --qemu-bin "$FAKE_QEMU" --ovmf-code "$OVMF_CODE" \
        --vfio-pci 0000:00:00.0 --check-only >/dev/null 2>&1; then
    fail 'check-only accepted a top-level allocated qcow2 overlay'
fi

if PATH="$MOCK_BIN:$PATH" "$LAUNCHER" --pair-dir "$PAIR" --vm-root "$VM_ROOT" \
        --qemu-bin "$FAKE_QEMU" --ovmf-code "$OVMF_CODE" \
        --vfio-pci invalid --check-only >/dev/null 2>&1; then
    fail 'check-only accepted a malformed VFIO address'
fi
[ ! -e "$QEMU_MARKER" ] && [ ! -e "$SYSTEMD_MARKER" ] ||
    fail 'negative check-only case invoked QEMU or the user service manager'

printf 'PASS: Tahoe disposable-pair launcher synthetic check-only fixture\n'
