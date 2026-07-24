#!/usr/bin/env bash
# Validate, then (only when explicitly requested) boot one prepared Tahoe v2
# disposable pair.  This is deliberately a one-shot host boundary: it never
# stops another guest, falls back to a shared variables store, or manages a
# second recovery guest.  A failed or exited launch leaves its pair consumed;
# discard that pair and prepare a new one from the known-working base.
set -euo pipefail

readonly QEMU_IMG="/usr/bin/qemu-img"
readonly RECEIPT_NAME="overlay-attestation.json"
readonly DISK_NAME="tahoe-pmf-runtime.qcow2"
readonly OVMF_VARS_NAME="OVMF_VARS-1920x1080.fd"
readonly LAUNCH_MARKER_NAME=".aiam-disposable-pair-consumed"
readonly MONITOR_NAME="qemu-monitor.sock"
readonly SERIAL_NAME="qemu-serial.log"
readonly MANAGEMENT_PORT=3322
readonly VIRTIO_NET_MAC="52:54:00:c9:18:28"

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
OVERLAY_EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_disposable_overlay_evidence_contract.sh"

PAIR_DIR=""
VM_ROOT=""
QEMU_BIN=""
OVMF_CODE=""
VFIO_PCI=""
MODE=""
CHECK_TMP=""
SYSTEMD_RUN=""
SYSTEMCTL=""
PAIR_UNIT=""

PAIR_DIR_SEEN=0
VM_ROOT_SEEN=0
QEMU_BIN_SEEN=0
OVMF_CODE_SEEN=0
VFIO_PCI_SEEN=0
MODE_SEEN=0

usage() {
    cat >&2 <<'EOF'
usage: tahoe_launch_disposable_pair.sh \
  --pair-dir /absolute/prepared-v2-pair \
  --vm-root /absolute/pinned-vm-root \
  --qemu-bin /absolute/qemu-system-x86_64 \
  --ovmf-code /absolute/OVMF_CODE.fd \
  --vfio-pci DOMAIN:BUS:DEVICE.FUNCTION \
  --check-only|--launch

--check-only validates one fresh v2 pair without starting QEMU or a service.
--launch repeats that validation, atomically consumes the pair, then starts
exactly one fixed-profile QEMU process.  It never stops or changes another
guest.  A consumed pair must be discarded rather than reused.

The launch submits one deterministic pair-derived transient user unit.  The
user service manager owns only that new QEMU process; this helper never
restarts, stops, or otherwise manages a guest after submission.
EOF
}

fail() {
    printf 'DISPOSABLE_PAIR_LAUNCH_FAIL:%s\n' "$1" >&2
    exit 1
}

cleanup_check_tmp() {
    local status="$1"
    trap - EXIT HUP INT TERM
    if [ -n "$CHECK_TMP" ] && [ -d "$CHECK_TMP" ]; then
        # CHECK_TMP is created directly below /tmp by mktemp and is never a
        # caller-supplied path.
        /usr/bin/find -P "$CHECK_TMP" -depth -delete >/dev/null 2>&1 || true
    fi
    exit "$status"
}

trap 'cleanup_check_tmp $?' EXIT
trap 'exit 1' HUP INT TERM

canonical_directory() {
    local input="$1" resolved
    case "$input" in
        /*) ;;
        *) return 1 ;;
    esac
    [ -d "$input" ] && [ ! -L "$input" ] || return 1
    resolved="$(cd -P -- "$input" && pwd -P)" || return 1
    [ -d "$resolved" ] && [ ! -L "$resolved" ] || return 1
    printf '%s\n' "$resolved"
}

canonical_regular_file() {
    local input="$1" parent leaf resolved_parent resolved
    case "$input" in
        /*) ;;
        *) return 1 ;;
    esac
    [ -f "$input" ] && [ ! -L "$input" ] || return 1
    parent="$(dirname -- "$input")"
    leaf="$(basename -- "$input")"
    resolved_parent="$(canonical_directory "$parent")" || return 1
    resolved="$resolved_parent/$leaf"
    [ -f "$resolved" ] && [ ! -L "$resolved" ] || return 1
    printf '%s\n' "$resolved"
}

validate_pair_layout() {
    local allow_marker="$1"
    python3 - "$PAIR_DIR" "$allow_marker" "$DISK_NAME" "$RECEIPT_NAME" \
        "$OVMF_VARS_NAME" "$LAUNCH_MARKER_NAME" "$MONITOR_NAME" \
        "$SERIAL_NAME" <<'PY'
import os
import stat
import sys

pair, allow_marker_text, disk, receipt, vars_name, marker, monitor, serial = sys.argv[1:]
allow_marker = allow_marker_text == "1"


def fail(reason: str) -> None:
    raise SystemExit(reason)


try:
    pair_stat = os.lstat(pair)
except OSError:
    fail("pair-dir-stat-failed")
if not stat.S_ISDIR(pair_stat.st_mode) or stat.S_IMODE(pair_stat.st_mode) != 0o700:
    fail("pair-dir-mode-invalid")
if pair_stat.st_uid != os.geteuid():
    fail("pair-dir-owner-invalid")
try:
    entries = set(os.listdir(pair))
except OSError:
    fail("pair-dir-list-failed")
expected = {disk, receipt, vars_name}
if allow_marker:
    expected.add(marker)
elif marker in entries:
    fail("launch-marker-exists")
if entries != expected:
    fail("pair-layout-unexpected-entry")
for name in (disk, receipt, vars_name):
    path = os.path.join(pair, name)
    try:
        item = os.lstat(path)
    except OSError:
        fail("pair-file-stat-failed")
    if not stat.S_ISREG(item.st_mode) or stat.S_IMODE(item.st_mode) != 0o600:
        fail("pair-file-mode-invalid")
    if item.st_uid != os.geteuid() or item.st_nlink != 1 or item.st_size <= 0:
        fail("pair-file-ownership-invalid")
if allow_marker:
    marker_path = os.path.join(pair, marker)
    try:
        marker_stat = os.lstat(marker_path)
    except OSError:
        fail("launch-marker-stat-failed")
    if (not stat.S_ISDIR(marker_stat.st_mode) or
            stat.S_IMODE(marker_stat.st_mode) != 0o700 or
            marker_stat.st_uid != os.geteuid()):
        fail("launch-marker-invalid")
    try:
        if os.listdir(marker_path):
            fail("launch-marker-not-empty")
    except OSError:
        fail("launch-marker-list-failed")
for unexpected in (monitor, serial):
    if os.path.lexists(os.path.join(pair, unexpected)):
        fail("pair-runtime-artifact-exists")
PY
}

validate_v2_receipt_and_read_binding() {
    "$OVERLAY_EVIDENCE_CONTRACT" --attestation "$RECEIPT" >/dev/null ||
        fail "overlay-receipt-invalid"

    local fields extra
    fields="$(python3 - "$RECEIPT" <<'PY'
import json
import sys

try:
    document = json.load(open(sys.argv[1], encoding="utf-8"))
    if document.get("schema") != "itlwm-tahoe-disposable-overlay/v2":
        raise ValueError("not-v2")
    ovmf = document["ovmf_vars"]
    overlay = document["overlay"]
    values = (
        ovmf["copy_sha256"],
        ovmf["copy_size_bytes"],
        overlay["base_virtual_size"],
        overlay["overlay_virtual_size"],
        overlay["base_metadata_sha256"],
        overlay["overlay_metadata_sha256"],
    )
    if any(not isinstance(value, (str, int)) or isinstance(value, bool)
           for value in values):
        raise ValueError("shape")
    print(" ".join(str(value) for value in values))
except (OSError, ValueError, KeyError, json.JSONDecodeError):
    raise SystemExit(1)
PY
)" || fail "overlay-receipt-binding-invalid"
    extra=""
    if ! IFS=' ' read -r RECEIPT_VARS_SHA256 RECEIPT_VARS_SIZE \
            RECEIPT_BASE_SIZE RECEIPT_OVERLAY_SIZE RECEIPT_BASE_METADATA_SHA256 \
            RECEIPT_OVERLAY_METADATA_SHA256 extra <<<"$fields"; then
        fail "overlay-receipt-binding-invalid"
    fi
    [[ "$RECEIPT_VARS_SHA256" =~ ^[0-9a-f]{64}$ &&
        "$RECEIPT_VARS_SIZE" =~ ^[1-9][0-9]*$ &&
        "$RECEIPT_BASE_SIZE" =~ ^[1-9][0-9]*$ &&
        "$RECEIPT_OVERLAY_SIZE" =~ ^[1-9][0-9]*$ &&
        "$RECEIPT_BASE_METADATA_SHA256" =~ ^[0-9a-f]{64}$ &&
        "$RECEIPT_OVERLAY_METADATA_SHA256" =~ ^[0-9a-f]{64}$ &&
        -z "$extra" ]] || fail "overlay-receipt-binding-invalid"
}

validate_private_vars_binding() {
    python3 - "$OVMF_VARS" "$RECEIPT_VARS_SHA256" "$RECEIPT_VARS_SIZE" <<'PY'
import hashlib
import os
import stat
import sys

path, expected_digest, expected_size_text = sys.argv[1:]


def fail(reason: str) -> None:
    raise SystemExit(reason)


def fingerprint(value: os.stat_result) -> tuple[int, int, int, int, int, int, int]:
    return (value.st_dev, value.st_ino, value.st_mode, value.st_nlink,
            value.st_size, value.st_mtime_ns, value.st_ctime_ns)


if not expected_size_text.isdecimal() or int(expected_size_text) <= 0:
    fail("vars-receipt-size-invalid")
try:
    before_path = os.lstat(path)
except OSError:
    fail("vars-stat-failed")
if (not stat.S_ISREG(before_path.st_mode) or
        stat.S_IMODE(before_path.st_mode) != 0o600 or
        before_path.st_nlink != 1 or
        before_path.st_size != int(expected_size_text)):
    fail("vars-file-invalid")
if not hasattr(os, "O_NOFOLLOW"):
    fail("no-follow-unavailable")
try:
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
except OSError:
    fail("vars-open-failed")
try:
    before = os.fstat(fd)
    if fingerprint(before_path) != fingerprint(before):
        fail("vars-replaced-before-hash")
    digest = hashlib.sha256()
    total = 0
    while True:
        chunk = os.read(fd, 1024 * 1024)
        if not chunk:
            break
        digest.update(chunk)
        total += len(chunk)
    after = os.fstat(fd)
finally:
    os.close(fd)
if fingerprint(before) != fingerprint(after) or total != before.st_size:
    fail("vars-changed-during-hash")
try:
    after_path = os.lstat(path)
except OSError:
    fail("vars-stat-failed")
if fingerprint(after_path) != fingerprint(after):
    fail("vars-path-changed-during-hash")
if digest.hexdigest() != expected_digest:
    fail("vars-receipt-hash-mismatch")
PY
}

assert_file_not_in_use() {
    local path="$1" label="$2"
    if "$FUSER" -s "$path" >/dev/null 2>&1; then
        fail "$label-in-use"
    fi
}

validate_fresh_direct_qcow2() {
    "$QEMU_IMG" info --output=json "$OVERLAY" >"$CHECK_TMP/overlay-info.json" \
        2>/dev/null || fail "overlay-info-unavailable"
    local backing extra
    backing="$(python3 - "$CHECK_TMP/overlay-info.json" <<'PY'
import json
import sys

try:
    document = json.load(open(sys.argv[1], encoding="utf-8"))
    value = document.get("full-backing-filename")
    if document.get("format") != "qcow2" or not isinstance(value, str):
        raise ValueError("shape")
    if (not value.startswith("/") or "\x00" in value or
            "\n" in value or "\r" in value):
        raise ValueError("backing")
    print(value)
except (OSError, ValueError, json.JSONDecodeError):
    raise SystemExit(1)
PY
)" || fail "overlay-backing-invalid"
    extra=""
    IFS= read -r backing extra <<<"$backing" || fail "overlay-backing-invalid"
    [ -z "$extra" ] || fail "overlay-backing-invalid"
    BACKING_IMAGE="$(canonical_regular_file "$backing")" ||
        fail "overlay-backing-invalid"
    [ "$BACKING_IMAGE" != "$OVERLAY" ] || fail "overlay-backing-invalid"
    assert_file_not_in_use "$BACKING_IMAGE" "backing-image"
    "$QEMU_IMG" info --output=json "$BACKING_IMAGE" >"$CHECK_TMP/backing-info.json" \
        2>/dev/null || fail "backing-info-unavailable"
    "$QEMU_IMG" map --output=json "$OVERLAY" >"$CHECK_TMP/overlay-map.json" \
        2>/dev/null || fail "overlay-map-unavailable"

    python3 - "$RECEIPT" "$OVERLAY" "$BACKING_IMAGE" \
        "$CHECK_TMP/overlay-info.json" "$CHECK_TMP/backing-info.json" \
        "$CHECK_TMP/overlay-map.json" <<'PY'
import hashlib
import json
import os
import sys


def fail(reason: str) -> None:
    raise SystemExit(reason)


def load(path: str) -> object:
    try:
        return json.load(open(path, encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        fail("qemu-json-invalid")


def positive(value: object, reason: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        fail(reason)
    return value


def metadata(info: dict) -> dict:
    specific = info.get("format-specific")
    data = specific.get("data") if isinstance(specific, dict) else None
    data = data if isinstance(data, dict) else {}
    return {
        "format": info.get("format"),
        "virtual_size": info.get("virtual-size"),
        "cluster_size": info.get("cluster-size"),
        "dirty_flag": info.get("dirty-flag"),
        "qcow2_compat": data.get("compat", "unknown"),
        "qcow2_refcount_bits": data.get("refcount-bits", "unknown"),
    }


def digest(value: dict) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


receipt, overlay_path, backing_path, overlay_info_path, backing_info_path, map_path = sys.argv[1:]
document = load(receipt)
overlay = load(overlay_info_path)
backing = load(backing_info_path)
mapping = load(map_path)
if not all(isinstance(value, dict) for value in (document, overlay, backing)):
    fail("qemu-info-shape-invalid")
if not isinstance(mapping, list) or not mapping:
    fail("overlay-map-shape-invalid")
if overlay.get("format") != "qcow2" or backing.get("format") != "qcow2":
    fail("qcow2-format-invalid")
if overlay.get("backing-filename-format") != "qcow2":
    fail("overlay-backing-format-invalid")
if backing.get("backing-filename") or backing.get("full-backing-filename"):
    fail("backing-is-not-root")
if os.path.realpath(str(overlay.get("filename", ""))) != os.path.realpath(overlay_path):
    fail("overlay-identity-invalid")
if os.path.realpath(str(overlay.get("full-backing-filename", ""))) != os.path.realpath(backing_path):
    fail("overlay-backing-relation-invalid")
overlay_section = document.get("overlay")
if not isinstance(overlay_section, dict):
    fail("receipt-overlay-invalid")
base_size = positive(backing.get("virtual-size"), "backing-size-invalid")
overlay_size = positive(overlay.get("virtual-size"), "overlay-size-invalid")
if base_size != overlay_size:
    fail("overlay-size-mismatch")
if base_size != overlay_section.get("base_virtual_size") or overlay_size != overlay_section.get("overlay_virtual_size"):
    fail("receipt-size-binding-invalid")
if backing.get("dirty-flag") is not False or overlay.get("dirty-flag") is not False:
    fail("qcow2-dirty-flag-invalid")
if digest(metadata(backing)) != overlay_section.get("base_metadata_sha256"):
    fail("receipt-backing-metadata-mismatch")
if digest(metadata(overlay)) != overlay_section.get("overlay_metadata_sha256"):
    fail("receipt-overlay-metadata-mismatch")
covered = 0
for extent in mapping:
    if not isinstance(extent, dict):
        fail("overlay-map-entry-invalid")
    start = extent.get("start")
    length = extent.get("length")
    depth = extent.get("depth")
    if (not isinstance(start, int) or isinstance(start, bool) or start < 0 or
            not isinstance(length, int) or isinstance(length, bool) or length <= 0 or
            not isinstance(depth, int) or isinstance(depth, bool) or depth < 1):
        fail("overlay-map-entry-invalid")
    if start != covered:
        fail("overlay-map-coverage-invalid")
    covered += length
if covered != overlay_size:
    fail("overlay-map-coverage-invalid")
PY
}

validate_pair() {
    local allow_marker="$1"
    validate_pair_layout "$allow_marker" || fail "pair-layout-invalid"
    validate_v2_receipt_and_read_binding
    validate_private_vars_binding || fail "private-vars-invalid"
    assert_file_not_in_use "$OVERLAY" "overlay-image"
    assert_file_not_in_use "$OVMF_VARS" "private-vars"
    validate_fresh_direct_qcow2 || fail "fresh-direct-qcow2-invalid"
}

assert_no_existing_qemu() {
    local status
    if python3 <<'PY'
import os
from pathlib import Path

for entry in Path("/proc").iterdir():
    if not entry.name.isdecimal():
        continue
    try:
        comm = (entry / "comm").read_text(encoding="utf-8").strip()
        raw = (entry / "cmdline").read_bytes().split(b"\0")
    except OSError:
        continue
    executable = os.path.basename(raw[0].decode("utf-8", "replace")) if raw and raw[0] else ""
    if comm.startswith("qemu-system-") or executable.startswith("qemu-system-"):
        raise SystemExit(1)
PY
    then
        return 0
    else
        status="$?"
        [ "$status" -eq 1 ] && fail "existing-qemu-refused"
        fail "qemu-presence-probe-unavailable"
    fi
}

assert_management_port_free() {
    local status
    if python3 - "$MANAGEMENT_PORT" <<'PY'
import pathlib
import sys

port = int(sys.argv[1])
for name in ("/proc/net/tcp", "/proc/net/tcp6"):
    try:
        rows = pathlib.Path(name).read_text(encoding="utf-8").splitlines()[1:]
    except OSError:
        raise SystemExit(1)
    for row in rows:
        fields = row.split()
        if len(fields) < 4 or fields[3] != "0A" or ":" not in fields[1]:
            continue
        try:
            local_port = int(fields[1].rsplit(":", 1)[1], 16)
        except ValueError:
            raise SystemExit(1)
        if local_port == port:
            raise SystemExit(2)
PY
    then
        return 0
    else
        status="$?"
        case "$status" in
            2) fail "management-port-in-use" ;;
            *) fail "management-port-probe-unavailable" ;;
        esac
    fi
}

consume_pair_atomically() {
    umask 077
    if ! /bin/mkdir -- "$LAUNCH_MARKER"; then
        fail "launch-marker-create-failed"
    fi
    [ -d "$LAUNCH_MARKER" ] && [ ! -L "$LAUNCH_MARKER" ] ||
        fail "launch-marker-create-failed"
}

derive_pair_unit() {
    PAIR_UNIT="$(python3 - "$PAIR_DIR" <<'PY'
import hashlib
import sys

digest = hashlib.sha256(sys.argv[1].encode("utf-8")).hexdigest()
print(f"aiam-tahoe-disposable-{digest[:20]}.service")
PY
)" || fail "pair-unit-derive-failed"
    [[ "$PAIR_UNIT" =~ ^aiam-tahoe-disposable-[0-9a-f]{20}\.service$ ]] ||
        fail "pair-unit-derive-failed"
}

assert_pair_unit_absent() {
    local listed
    listed="$("$SYSTEMCTL" --user list-units --all --no-legend "$PAIR_UNIT" \
        2>/dev/null)" || fail "pair-unit-probe-unavailable"
    [ -z "$listed" ] || fail "pair-unit-exists"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --pair-dir)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$PAIR_DIR_SEEN" -eq 0 ] || fail "pair-dir-duplicate"
            PAIR_DIR="$2"
            PAIR_DIR_SEEN=1
            shift 2
            ;;
        --vm-root)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$VM_ROOT_SEEN" -eq 0 ] || fail "vm-root-duplicate"
            VM_ROOT="$2"
            VM_ROOT_SEEN=1
            shift 2
            ;;
        --qemu-bin)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$QEMU_BIN_SEEN" -eq 0 ] || fail "qemu-bin-duplicate"
            QEMU_BIN="$2"
            QEMU_BIN_SEEN=1
            shift 2
            ;;
        --ovmf-code)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$OVMF_CODE_SEEN" -eq 0 ] || fail "ovmf-code-duplicate"
            OVMF_CODE="$2"
            OVMF_CODE_SEEN=1
            shift 2
            ;;
        --vfio-pci)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$VFIO_PCI_SEEN" -eq 0 ] || fail "vfio-pci-duplicate"
            VFIO_PCI="$2"
            VFIO_PCI_SEEN=1
            shift 2
            ;;
        --check-only)
            [ "$MODE_SEEN" -eq 0 ] || fail "mode-duplicate"
            MODE="check"
            MODE_SEEN=1
            shift
            ;;
        --launch)
            [ "$MODE_SEEN" -eq 0 ] || fail "mode-duplicate"
            MODE="launch"
            MODE_SEEN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

[ -n "$PAIR_DIR" ] && [ -n "$VM_ROOT" ] && [ -n "$QEMU_BIN" ] &&
    [ -n "$OVMF_CODE" ] && [ -n "$VFIO_PCI" ] && [ -n "$MODE" ] || {
    usage
    exit 2
}
[ -x "$QEMU_IMG" ] || fail "qemu-img-unavailable"
[ -x "$OVERLAY_EVIDENCE_CONTRACT" ] || fail "overlay-evidence-contract-unavailable"
FUSER="$(command -v fuser || true)"
[ -n "$FUSER" ] || fail "fuser-unavailable"

PAIR_DIR="$(canonical_directory "$PAIR_DIR")" || fail "pair-dir-invalid"
VM_ROOT="$(canonical_directory "$VM_ROOT")" || fail "vm-root-invalid"
[ "$(dirname -- "$PAIR_DIR")" = "$VM_ROOT" ] ||
    fail "pair-dir-not-direct-vm-root-child"
QEMU_BIN="$(canonical_regular_file "$QEMU_BIN")" || fail "qemu-bin-invalid"
[ -x "$QEMU_BIN" ] || fail "qemu-bin-not-executable"
[ "$(basename -- "$QEMU_BIN")" = "qemu-system-x86_64" ] ||
    fail "qemu-bin-name-invalid"
OVMF_CODE="$(canonical_regular_file "$OVMF_CODE")" || fail "ovmf-code-invalid"
[ -s "$OVMF_CODE" ] || fail "ovmf-code-empty"
[[ "$VFIO_PCI" =~ ^[[:xdigit:]]{4}:[[:xdigit:]]{2}:[[:xdigit:]]{2}\.[0-7]$ ]] ||
    fail "vfio-pci-invalid"
VFIO_PCI="$(tr '[:upper:]' '[:lower:]' <<<"$VFIO_PCI")"

RECEIPT="$PAIR_DIR/$RECEIPT_NAME"
OVERLAY="$PAIR_DIR/$DISK_NAME"
OVMF_VARS="$PAIR_DIR/$OVMF_VARS_NAME"
LAUNCH_MARKER="$PAIR_DIR/$LAUNCH_MARKER_NAME"
MONITOR="$PAIR_DIR/$MONITOR_NAME"
SERIAL="$PAIR_DIR/$SERIAL_NAME"

CHECK_TMP="$(mktemp -d /tmp/aiam-tahoe-disposable-pair-check.XXXXXX)" ||
    fail "check-temp-create-failed"
validate_pair 0

if [ "$MODE" = "check" ]; then
    printf 'DISPOSABLE_PAIR_CHECK_READY\n'
    exit 0
fi

# A boot is never a takeover: an already-running QEMU or a listener on the
# fixed local management port is a hard stop.  The launcher does not attempt
# to stop either one.
assert_no_existing_qemu
assert_management_port_free
SYSTEMD_RUN="$(command -v systemd-run || true)"
SYSTEMCTL="$(command -v systemctl || true)"
[ -n "$SYSTEMD_RUN" ] && [ -n "$SYSTEMCTL" ] || fail "user-systemd-unavailable"
derive_pair_unit
assert_pair_unit_absent
consume_pair_atomically

# Re-run the complete read-only preflight after claiming the pair.  If anything
# drifts, the already-consumed pair remains a discard-only boundary.
validate_pair 1
assert_no_existing_qemu
assert_management_port_free
assert_pair_unit_absent

if [ -n "$CHECK_TMP" ] && [ -d "$CHECK_TMP" ]; then
    /usr/bin/find -P "$CHECK_TMP" -depth -delete >/dev/null 2>&1 ||
        fail "check-temp-cleanup-failed"
    CHECK_TMP=""
fi
trap - EXIT HUP INT TERM

# The QEMU argv is intentionally closed: no caller-provided extra arguments,
# shared OVMF variables store, management helper, or second guest exists here.
# The user manager owns only this newly submitted unit; a submission failure
# leaves the pair consumed so it cannot be retried after an ambiguous start.
"$SYSTEMD_RUN" --user --collect --quiet --unit="$PAIR_UNIT" \
    --property=Type=exec \
    --property=Restart=no \
    --property=KillMode=control-group \
    --property=LimitMEMLOCK=infinity \
    --property=UMask=0077 \
    --property="WorkingDirectory=$VM_ROOT" \
    --property=StandardOutput=journal \
    --property=StandardError=journal \
    -- \
    "$QEMU_BIN" \
    -name aiam-tahoe-disposable-pair \
    -m 16384 \
    -cpu Haswell-noTSX,vendor=GenuineIntel,hv-vendor-id=VMwareVMware,+invtsc,+hypervisor,vmware-cpuid-freq=on \
    -machine q35,accel=kvm \
    -smp 8,cores=4,sockets=1 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-tablet,bus=xhci.0 \
    -device usb-ehci,id=ehci \
    -global ICH9-LPC.acpi-pci-hotplug-with-bridge-support=off \
    -device isa-applesmc,osk=ourhardworkbythesewordsguardedpleasedontsteal\(c\)AppleComputerInc \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$OVMF_VARS" \
    -smbios type=2 \
    -device ich9-ahci,id=sata \
    -drive id=MacHDD,if=none,file="$OVERLAY",format=qcow2,cache=writeback,aio=threads \
    -device ide-hd,bus=sata.4,drive=MacHDD \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:${MANAGEMENT_PORT}-:22 \
    -device virtio-net-pci,netdev=net0,id=net0,mac="$VIRTIO_NET_MAC" \
    -device vfio-pci,host="$VFIO_PCI" \
    -monitor "unix:$MONITOR,server,nowait" \
    -serial "file:$SERIAL" \
    -display none || fail "pair-unit-submit-failed"
printf 'DISPOSABLE_PAIR_LAUNCH_SUBMITTED:%s\n' "$PAIR_UNIT"
