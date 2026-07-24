#!/usr/bin/env bash
# Static contract for the one-shot Tahoe v2 disposable-pair launcher.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
LAUNCHER="$ROOT/scripts/tahoe_launch_disposable_pair.sh"
FIXTURE="$ROOT/scripts/test_tahoe_launch_disposable_pair_fixture.sh"
PROTOCOL="$ROOT/docs/TAHOE_DISPOSABLE_OVERLAY_PROTOCOL.md"

fail() {
    printf 'FAIL: Tahoe disposable-pair launcher contract: %s\n' "$*" >&2
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

for path in "$LAUNCHER" "$FIXTURE" "$PROTOCOL"; do
    [ -f "$path" ] || fail "required file is missing: ${path##*/}"
done
[ -x "$LAUNCHER" ] && [ -x "$FIXTURE" ] ||
    fail 'launcher scripts must be executable'
bash -n "$LAUNCHER"
bash -n "$FIXTURE"
"$LAUNCHER" --help >/dev/null 2>&1

for needle in \
    'readonly QEMU_IMG="/usr/bin/qemu-img"' \
    'readonly LAUNCH_MARKER_NAME=".aiam-disposable-pair-consumed"' \
    'readonly MONITOR_NAME="qemu-monitor.sock"' \
    'readonly SERIAL_NAME="qemu-serial.log"' \
    'readonly MANAGEMENT_PORT=3322' \
    'readonly VIRTIO_NET_MAC="52:54:00:c9:18:28"' \
    '--pair-dir /absolute/prepared-v2-pair' \
    '--vm-root /absolute/pinned-vm-root' \
    '--qemu-bin /absolute/qemu-system-x86_64' \
    '--ovmf-code /absolute/OVMF_CODE.fd' \
    '--vfio-pci DOMAIN:BUS:DEVICE.FUNCTION' \
    '--check-only|--launch' \
    'pair-dir-not-direct-vm-root-child' \
    'qemu-bin-name-invalid' \
    'launch-marker-exists' \
    'pair-runtime-artifact-exists' \
    'itlwm-tahoe-disposable-overlay/v2' \
    '"$OVERLAY_EVIDENCE_CONTRACT" --attestation "$RECEIPT"' \
    'os.O_RDONLY | os.O_NOFOLLOW' \
    'vars-receipt-hash-mismatch' \
    'qemu-img-unavailable' \
    'overlay-backing-relation-invalid' \
    'receipt-overlay-metadata-mismatch' \
    'overlay-map-coverage-invalid' \
    'assert_no_existing_qemu' \
    'assert_management_port_free' \
    'management-port-in-use' \
    'derive_pair_unit' \
    'aiam-tahoe-disposable-' \
    'assert_pair_unit_absent' \
    'consume_pair_atomically' \
    '"$SYSTEMD_RUN" --user --collect --quiet --unit="$PAIR_UNIT"' \
    '--property=Restart=no' \
    '--property=LimitMEMLOCK=infinity' \
    '--property=UMask=0077' \
    '--property="WorkingDirectory=$VM_ROOT"' \
    '-drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE"' \
    '-drive if=pflash,format=raw,file="$OVMF_VARS"' \
    '-drive id=MacHDD,if=none,file="$OVERLAY",format=qcow2,cache=writeback,aio=threads' \
    '-netdev user,id=net0,hostfwd=tcp:127.0.0.1:${MANAGEMENT_PORT}-:22' \
    '-device virtio-net-pci,netdev=net0,id=net0,mac="$VIRTIO_NET_MAC"' \
    '-device vfio-pci,host="$VFIO_PCI"' \
    '-monitor "unix:$MONITOR,server,nowait"' \
    '-serial "file:$SERIAL"' \
    'DISPOSABLE_PAIR_CHECK_READY' \
    'DISPOSABLE_PAIR_LAUNCH_SUBMITTED:'; do
    require_literal "$LAUNCHER" "$needle" "launcher safety token: $needle"
done

for needle in \
    'ITLWM_DISK' \
    'ITLWM_OVMF_VARS' \
    'vmctl' \
    'boot-macOS-headless' \
    'systemctl --user stop' \
    'systemctl --user restart' \
    'systemctl --user kill' \
    '-no-reboot' \
    'rm -rf' \
    '"$@"'; do
    forbid_literal "$LAUNCHER" "$needle" "launcher capability: $needle"
done

python3 - "$LAUNCHER" "$PROTOCOL" <<'PY'
from pathlib import Path
import re
import sys

launcher = Path(sys.argv[1]).read_text(encoding="utf-8")
protocol = Path(sys.argv[2]).read_text(encoding="utf-8")


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: Tahoe disposable-pair launcher contract: {message}")


def ordered(label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = launcher.find(token, cursor)
        if position < 0:
            fail(f"{label} misses ordered token: {token}")
        cursor = position + len(token)


ordered("check-only boundary",
        'validate_pair 0',
        'if [ "$MODE" = "check" ]; then',
        "DISPOSABLE_PAIR_CHECK_READY",
        'SYSTEMD_RUN="$(command -v systemd-run || true)"')
ordered("launch claim boundary",
        'assert_no_existing_qemu',
        'assert_management_port_free',
        'derive_pair_unit',
        'assert_pair_unit_absent',
        'consume_pair_atomically',
        'validate_pair 1',
        '"$SYSTEMD_RUN" --user --collect --quiet --unit="$PAIR_UNIT"')

if launcher.count('-device vfio-pci,host="$VFIO_PCI"') != 1:
    fail("launcher must pass exactly one explicit VFIO device")
if launcher.count('-drive if=pflash,format=raw,file="$OVMF_VARS"') != 1:
    fail("launcher must pass exactly one pair-local mutable variables store")
if launcher.count('-monitor "unix:$MONITOR,server,nowait"') != 1:
    fail("launcher must pass exactly one pair-local monitor")
if launcher.count('-serial "file:$SERIAL"') != 1:
    fail("launcher must pass exactly one pair-local serial log")
if 'exec "$QEMU_BIN"' in launcher:
    fail("launcher retains unmanaged direct QEMU exec")
if re.search(r'\b(?:\d{1,3}\.){3}\d{1,3}\b', protocol):
    fail("protocol contains IPv4 literal")
for required in (
    "v2", "check-only", "systemd", "consumed", "private variables",
    "one", "not a recovery", "does not stop",
):
    if required not in protocol:
        fail(f"protocol omits boundary: {required}")

print("PASS: Tahoe disposable-pair launcher static contract")
PY

"$FIXTURE"
