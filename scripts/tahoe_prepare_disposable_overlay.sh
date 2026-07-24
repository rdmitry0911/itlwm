#!/usr/bin/env bash
# Create, but never boot, a fresh Tahoe qcow2 external overlay.
#
# The helper deliberately owns only a new directory below an explicitly named
# VM root.  The backing image is inspected read-only, must itself be a qcow2
# root (not another overlay), and must not be open by another process.  The
# resulting overlay is therefore one direct writable layer over one immutable
# backing image.  No QEMU system process, guest reboot, kext operation, AP
# operation, or network operation is performed here.
set -euo pipefail

readonly QEMU_IMG="/usr/bin/qemu-img"
readonly DISK_NAME="tahoe-pmf-runtime.qcow2"
readonly ATTESTATION_NAME="overlay-attestation.json"
readonly OVMF_VARS_NAME="OVMF_VARS-1920x1080.fd"

BASE_IMAGE=""
VM_ROOT=""
OUT_DIR_NAME=""
OVMF_VARS_TEMPLATE=""
BASE_IMAGE_SEEN=0
VM_ROOT_SEEN=0
OUT_DIR_NAME_SEEN=0
OVMF_VARS_TEMPLATE_SEEN=0
STAGING_DIR=""

usage() {
    cat >&2 <<'EOF'
usage: tahoe_prepare_disposable_overlay.sh \
  --base-image /absolute/root-tahoe.qcow2 \
  --vm-root /absolute/pinned-vm-root \
  --out-dir fresh-overlay-directory \
  [--ovmf-vars-template /absolute/OVMF_VARS-template.fd]

Creates exactly these new files below --vm-root/fresh-overlay-directory:
  tahoe-pmf-runtime.qcow2
  overlay-attestation.json

When --ovmf-vars-template is supplied, also creates a private, distinct-inode
copy named OVMF_VARS-1920x1080.fd.  The template must be an unopened,
nonempty, absolute regular file that is not a symlink.

The base must be an unopened qcow2 image with no backing image.  This helper
does not boot QEMU or reboot a guest.  The output directory must be a fresh
single path component so publication can be atomic.
EOF
}

fail() {
    printf 'OVERLAY_PREP_FAIL:%s\n' "$1" >&2
    exit 1
}

cleanup_staging() {
    local status="$1"
    trap - EXIT HUP INT TERM
    if [ -n "$STAGING_DIR" ] && [ -d "$STAGING_DIR" ]; then
        # STAGING_DIR is created by mktemp directly below the validated VM
        # root.  Never clean a caller-provided path.
        /usr/bin/find -P "$STAGING_DIR" -depth -delete >/dev/null 2>&1 || true
    fi
    exit "$status"
}

trap 'cleanup_staging $?' EXIT
trap 'exit 1' HUP INT TERM

safe_output_leaf() {
    local value="$1"
    [[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,119}$ ]] &&
        [ "$value" != "." ] && [ "$value" != ".." ]
}

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

prepare_ovmf_vars_copy() {
    local template_path="$1" copy_path="$2"

    # Hash the opened template before and after the copy, and hash the opened
    # destination after fsync.  A simple pathname-to-pathname copy would leave
    # a replace-or-mutate race unobserved between validation and publication.
    python3 - "$template_path" "$copy_path" <<'PY'
import hashlib
import os
import stat
import sys


def fail(reason: str) -> None:
    raise SystemExit(reason)


def fingerprint(value: os.stat_result) -> tuple[int, int, int, int, int]:
    return (value.st_dev, value.st_ino, value.st_size,
            value.st_mtime_ns, value.st_ctime_ns)


def require_regular_nonempty(value: os.stat_result, reason: str) -> None:
    if not stat.S_ISREG(value.st_mode) or value.st_size <= 0:
        fail(reason)


def stable_hash(fd: int) -> tuple[str, os.stat_result]:
    before = os.fstat(fd)
    require_regular_nonempty(before, "file-not-regular-nonempty")
    os.lseek(fd, 0, os.SEEK_SET)
    digest = hashlib.sha256()
    total = 0
    while True:
        chunk = os.read(fd, 1024 * 1024)
        if not chunk:
            break
        digest.update(chunk)
        total += len(chunk)
    after = os.fstat(fd)
    if fingerprint(before) != fingerprint(after) or total != before.st_size:
        fail("file-changed-during-hash")
    return digest.hexdigest(), after


def write_all(fd: int, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        written = os.write(fd, data[offset:])
        if written <= 0:
            fail("copy-write-failed")
        offset += written


if len(sys.argv) != 3:
    fail("copy-arguments")
if not hasattr(os, "O_NOFOLLOW"):
    fail("no-follow-unavailable")

template_path, copy_path = sys.argv[1:]
if os.path.basename(copy_path) != "OVMF_VARS-1920x1080.fd":
    fail("copy-name-invalid")

try:
    template_lstat = os.lstat(template_path)
except OSError:
    fail("template-stat-failed")
require_regular_nonempty(template_lstat, "template-not-regular-nonempty")

source_fd = -1
copy_fd = -1
try:
    source_fd = os.open(template_path, os.O_RDONLY | os.O_NOFOLLOW)
    source_open_stat = os.fstat(source_fd)
    require_regular_nonempty(source_open_stat, "template-not-regular-nonempty")
    if fingerprint(template_lstat) != fingerprint(source_open_stat):
        fail("template-replaced-before-copy")

    template_before_sha256, template_before_stat = stable_hash(source_fd)
    if fingerprint(source_open_stat) != fingerprint(template_before_stat):
        fail("template-changed-before-copy")

    copy_fd = os.open(copy_path,
                      os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_RDWR,
                      0o600)
    os.fchmod(copy_fd, 0o600)
    os.lseek(source_fd, 0, os.SEEK_SET)
    copy_source_before = os.fstat(source_fd)
    copied = 0
    copy_digest = hashlib.sha256()
    while True:
        chunk = os.read(source_fd, 1024 * 1024)
        if not chunk:
            break
        copy_digest.update(chunk)
        write_all(copy_fd, chunk)
        copied += len(chunk)
    os.fsync(copy_fd)
    copy_source_after = os.fstat(source_fd)
    if (fingerprint(copy_source_before) != fingerprint(copy_source_after) or
            copied != copy_source_before.st_size):
        fail("template-changed-during-copy")

    template_after_sha256, template_after_stat = stable_hash(source_fd)
    copy_after_sha256, copy_after_stat = stable_hash(copy_fd)
    if (fingerprint(template_before_stat) != fingerprint(template_after_stat) or
            template_before_sha256 != template_after_sha256):
        fail("template-changed-during-copy")
    if (copy_digest.hexdigest() != template_before_sha256 or
            copy_after_sha256 != template_before_sha256):
        fail("template-copy-hash-mismatch")
    if stat.S_IMODE(copy_after_stat.st_mode) != 0o600:
        fail("copy-mode-invalid")
    if (copy_after_stat.st_dev, copy_after_stat.st_ino) == (
            template_after_stat.st_dev, template_after_stat.st_ino):
        fail("copy-inode-not-distinct")

    template_after_lstat = os.lstat(template_path)
    copy_after_lstat = os.lstat(copy_path)
    if (fingerprint(template_after_lstat) != fingerprint(template_after_stat) or
            fingerprint(copy_after_lstat) != fingerprint(copy_after_stat)):
        fail("path-changed-during-copy")
    if stat.S_IMODE(copy_after_lstat.st_mode) != 0o600:
        fail("copy-mode-invalid")
except OSError:
    fail("copy-io-failed")
finally:
    if copy_fd >= 0:
        os.close(copy_fd)
    if source_fd >= 0:
        os.close(source_fd)

print(f"{template_before_sha256} {copy_after_sha256} {copy_after_stat.st_size}")
PY
}

validate_ovmf_vars_copy_before_publish() {
    local template_path="$1" copy_path="$2" expected_template_sha256="$3"
    local expected_copy_sha256="$4" expected_size="$5"

    python3 - "$template_path" "$copy_path" "$expected_template_sha256" \
        "$expected_copy_sha256" "$expected_size" <<'PY'
import hashlib
import os
import re
import stat
import sys


def fail(reason: str) -> None:
    raise SystemExit(reason)


def fingerprint(value: os.stat_result) -> tuple[int, int, int, int, int]:
    return (value.st_dev, value.st_ino, value.st_size,
            value.st_mtime_ns, value.st_ctime_ns)


def stable_path_hash(path: str) -> tuple[str, os.stat_result]:
    try:
        before_path = os.lstat(path)
    except OSError:
        fail("file-stat-failed")
    if not stat.S_ISREG(before_path.st_mode) or before_path.st_size <= 0:
        fail("file-not-regular-nonempty")
    if not hasattr(os, "O_NOFOLLOW"):
        fail("no-follow-unavailable")
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    except OSError:
        fail("file-open-failed")
    try:
        before = os.fstat(fd)
        if fingerprint(before_path) != fingerprint(before):
            fail("file-replaced-before-hash")
        os.lseek(fd, 0, os.SEEK_SET)
        digest = hashlib.sha256()
        total = 0
        while True:
            chunk = os.read(fd, 1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
            total += len(chunk)
        after = os.fstat(fd)
    except OSError:
        fail("file-hash-failed")
    finally:
        os.close(fd)
    if fingerprint(before) != fingerprint(after) or total != before.st_size:
        fail("file-changed-during-hash")
    try:
        after_path = os.lstat(path)
    except OSError:
        fail("file-stat-failed")
    if fingerprint(after_path) != fingerprint(after):
        fail("path-changed-during-hash")
    return digest.hexdigest(), after


if len(sys.argv) != 6:
    fail("verify-arguments")
template_path, copy_path, template_sha256, copy_sha256, expected_size = sys.argv[1:]
if (re.fullmatch(r"[0-9a-f]{64}", template_sha256) is None or
        re.fullmatch(r"[0-9a-f]{64}", copy_sha256) is None or
        not expected_size.isdecimal() or int(expected_size) <= 0):
    fail("verify-input-invalid")
if os.path.basename(copy_path) != "OVMF_VARS-1920x1080.fd":
    fail("copy-name-invalid")

actual_template_sha256, template_stat = stable_path_hash(template_path)
actual_copy_sha256, copy_stat = stable_path_hash(copy_path)
if stat.S_IMODE(copy_stat.st_mode) != 0o600:
    fail("copy-mode-invalid")
if (template_stat.st_dev, template_stat.st_ino) == (copy_stat.st_dev, copy_stat.st_ino):
    fail("copy-inode-not-distinct")
if (actual_template_sha256 != template_sha256 or
        actual_copy_sha256 != copy_sha256 or
        actual_template_sha256 != actual_copy_sha256 or
        copy_stat.st_size != int(expected_size)):
    fail("template-copy-hash-mismatch")
PY
}

validate_base_info() {
    local info_path="$1"
    python3 - "$info_path" <<'PY'
import json
import sys

try:
    with open(sys.argv[1], "r", encoding="utf-8") as source:
        info = json.load(source)
except (IndexError, OSError, json.JSONDecodeError):
    raise SystemExit(1)

if not isinstance(info, dict) or info.get("format") != "qcow2":
    raise SystemExit(1)
if info.get("backing-filename") or info.get("full-backing-filename"):
    raise SystemExit(1)
virtual_size = info.get("virtual-size")
cluster_size = info.get("cluster-size")
if (not isinstance(virtual_size, int) or isinstance(virtual_size, bool) or
        virtual_size <= 0):
    raise SystemExit(1)
if (not isinstance(cluster_size, int) or isinstance(cluster_size, bool) or
        cluster_size <= 0):
    raise SystemExit(1)
PY
}

write_and_validate_attestation() {
    local base_info_path="$1" overlay_info_path="$2" overlay_map_path="$3"
    local base_image="$4" overlay_image="$5" attestation="$6"
    shift 6

    python3 - "$base_info_path" "$overlay_info_path" "$overlay_map_path" \
        "$base_image" "$overlay_image" "$attestation" "$@" <<'PY'
import hashlib
import json
import os
import re
import sys


def fail(reason: str) -> None:
    raise SystemExit(reason)


if len(sys.argv) not in (7, 10):
    fail("attestation-arguments")


try:
    with open(sys.argv[1], "r", encoding="utf-8") as source:
        base = json.load(source)
    with open(sys.argv[2], "r", encoding="utf-8") as source:
        overlay = json.load(source)
    with open(sys.argv[3], "r", encoding="utf-8") as source:
        mapping = json.load(source)
except (IndexError, OSError, json.JSONDecodeError):
    fail("invalid-qemu-img-json")

base_path = os.path.realpath(sys.argv[4])
overlay_path = os.path.realpath(sys.argv[5])
attestation_path = sys.argv[6]
ovmf_vars_enabled = len(sys.argv) == 10
if ovmf_vars_enabled:
    template_sha256 = sys.argv[7]
    copy_sha256 = sys.argv[8]
    copy_size_text = sys.argv[9]
    if (re.fullmatch(r"[0-9a-f]{64}", template_sha256) is None or
            re.fullmatch(r"[0-9a-f]{64}", copy_sha256) is None or
            template_sha256 != copy_sha256 or
            not copy_size_text.isdecimal() or int(copy_size_text) <= 0):
        fail("ovmf-vars-attestation-input-invalid")
    copy_size_bytes = int(copy_size_text)

if not isinstance(base, dict) or not isinstance(overlay, dict):
    fail("image-info-shape")
if base.get("format") != "qcow2" or overlay.get("format") != "qcow2":
    fail("image-format")
if base.get("backing-filename") or base.get("full-backing-filename"):
    fail("base-is-not-root")
if overlay.get("backing-filename-format") != "qcow2":
    fail("backing-format")
if os.path.realpath(str(overlay.get("full-backing-filename", ""))) != base_path:
    fail("backing-relation")
if os.path.realpath(str(overlay.get("filename", ""))) != overlay_path:
    fail("overlay-identity")

base_virtual_size = base.get("virtual-size")
overlay_virtual_size = overlay.get("virtual-size")
base_cluster_size = base.get("cluster-size")
overlay_cluster_size = overlay.get("cluster-size")
for value in (base_virtual_size, overlay_virtual_size,
              base_cluster_size, overlay_cluster_size):
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        fail("image-size")
if base_virtual_size != overlay_virtual_size:
    fail("virtual-size-mismatch")
if base.get("dirty-flag") is not False or overlay.get("dirty-flag") is not False:
    fail("dirty-image")

if not isinstance(mapping, list) or not mapping:
    fail("map-shape")
covered = 0
for extent in mapping:
    if not isinstance(extent, dict):
        fail("map-entry")
    start = extent.get("start")
    length = extent.get("length")
    depth = extent.get("depth")
    if (not isinstance(start, int) or isinstance(start, bool) or start < 0 or
            not isinstance(length, int) or isinstance(length, bool) or length <= 0 or
            not isinstance(depth, int) or isinstance(depth, bool) or depth < 1):
        fail("map-entry")
    if start != covered:
        fail("map-coverage")
    covered += length
if covered != overlay_virtual_size:
    fail("map-coverage")


def qcow2_metadata(info: dict) -> dict:
    format_data = info.get("format-specific", {})
    if not isinstance(format_data, dict):
        format_data = {}
    data = format_data.get("data", {})
    if not isinstance(data, dict):
        data = {}
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


base_metadata = qcow2_metadata(base)
overlay_metadata = qcow2_metadata(overlay)
document = {
    "schema": "itlwm-tahoe-disposable-overlay/v1",
    "result": "PASS",
    "scope": {
        "qemu_guest_started_by_helper": False,
        "guest_rebooted_by_helper": False,
        "base_image_mutated_by_helper": False,
        "candidate_or_auxkc_mutated_by_helper": False,
        "physical_validation_host_touched": False,
    },
    "overlay": {
        "format": "qcow2",
        "one_direct_backing_image_verified": True,
        "base_has_no_backing_image": True,
        "top_overlay_data_allocated": False,
        "base_virtual_size": base_virtual_size,
        "overlay_virtual_size": overlay_virtual_size,
        "base_metadata_sha256": digest(base_metadata),
        "overlay_metadata_sha256": digest(overlay_metadata),
    },
    "launch_contract": {
        "pinned_vm_root_required": True,
        "disk_selector_environment_variable": "ITLWM_DISK",
        "guest_boot_performed_by_helper": False,
    },
    "local_only": {
        "image_paths_retained_local_only": True,
        "wireless_identity_or_credential_recorded": False,
    },
    "non_claims": [
        "candidate activation",
        "guest boot or reboot",
        "PMF/BIP association",
        "traffic reachability",
        "physical-host validation",
    ],
}

if ovmf_vars_enabled:
    document["schema"] = "itlwm-tahoe-disposable-overlay/v2"
    document["launch_contract"] = {
        "pinned_vm_root_required": True,
        "disk_selector_environment_variable": "ITLWM_DISK",
        "guest_boot_performed_by_helper": False,
        "ovmf_vars_selector_environment_variable": "ITLWM_OVMF_VARS",
        "ovmf_vars_file_name": "OVMF_VARS-1920x1080.fd",
        "ovmf_vars_sha256_must_match_before_first_boot": True,
    }
    document["ovmf_vars"] = {
        "format": "raw-pflash",
        "file_name": "OVMF_VARS-1920x1080.fd",
        "per_overlay_copy_created": True,
        "copy_mode_0600": True,
        "copy_has_distinct_inode": True,
        "template_not_in_use_precheck": True,
        "template_stable_during_copy": True,
        "template_sha256": template_sha256,
        "copy_sha256": copy_sha256,
        "template_copy_sha256_match": True,
        "copy_size_bytes": copy_size_bytes,
    }

try:
    fd = os.open(attestation_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as output:
        json.dump(document, output, sort_keys=True, indent=2)
        output.write("\n")
except OSError:
    fail("attestation-write")
PY
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --base-image)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$BASE_IMAGE_SEEN" -eq 0 ] || fail "base-image-duplicate"
            BASE_IMAGE="$2"
            BASE_IMAGE_SEEN=1
            shift 2
            ;;
        --vm-root)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$VM_ROOT_SEEN" -eq 0 ] || fail "vm-root-duplicate"
            VM_ROOT="$2"
            VM_ROOT_SEEN=1
            shift 2
            ;;
        --out-dir)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$OUT_DIR_NAME_SEEN" -eq 0 ] || fail "out-dir-duplicate"
            OUT_DIR_NAME="$2"
            OUT_DIR_NAME_SEEN=1
            shift 2
            ;;
        --ovmf-vars-template)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            [ "$OVMF_VARS_TEMPLATE_SEEN" -eq 0 ] ||
                fail "ovmf-vars-template-duplicate"
            OVMF_VARS_TEMPLATE="$2"
            OVMF_VARS_TEMPLATE_SEEN=1
            shift 2
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

[ -n "$BASE_IMAGE" ] && [ -n "$VM_ROOT" ] && [ -n "$OUT_DIR_NAME" ] || {
    usage
    exit 2
}
[ -x "$QEMU_IMG" ] || fail "qemu-img-unavailable"
command -v python3 >/dev/null 2>&1 || fail "python3-unavailable"
FUSER="$(command -v fuser || true)"
[ -n "$FUSER" ] || fail "fuser-unavailable"

base_image="$(canonical_regular_file "$BASE_IMAGE")" || fail "base-image-invalid"
vm_root="$(canonical_directory "$VM_ROOT")" || fail "vm-root-invalid"
ovmf_vars_template=""
if [ -n "$OVMF_VARS_TEMPLATE" ]; then
    ovmf_vars_template="$(canonical_regular_file "$OVMF_VARS_TEMPLATE")" ||
        fail "ovmf-vars-template-invalid"
    [ -s "$ovmf_vars_template" ] || fail "ovmf-vars-template-empty"
fi
safe_output_leaf "$OUT_DIR_NAME" || fail "out-dir-invalid"
final_dir="$vm_root/$OUT_DIR_NAME"
[ ! -e "$final_dir" ] && [ ! -L "$final_dir" ] || fail "out-dir-exists"

# A running QEMU holding the root image is a hard stop.  The helper does not
# attempt to reason about a live guest or force its shutdown.
# PSmisc fuser treats `--` as an absent process specification on the host
# version used for this lab.  Keep the absolute, validated regular-file path
# as the sole operand so a running QEMU holding the root image is a hard stop.
if "$FUSER" -s "$base_image" >/dev/null 2>&1; then
    fail "base-image-in-use"
fi
if [ -n "$ovmf_vars_template" ] &&
        "$FUSER" -s "$ovmf_vars_template" >/dev/null 2>&1; then
    fail "ovmf-vars-template-in-use"
fi

umask 077
STAGING_DIR="$(mktemp -d "$vm_root/.aiam-overlay-stage.XXXXXX")" ||
    fail "staging-create-failed"
case "$STAGING_DIR" in
    "$vm_root"/.aiam-overlay-stage.*) ;;
    *) fail "staging-path-invalid" ;;
esac

overlay_image="$STAGING_DIR/$DISK_NAME"
attestation="$STAGING_DIR/$ATTESTATION_NAME"
base_info_path="$STAGING_DIR/base-info.json"
overlay_info_path="$STAGING_DIR/overlay-info.json"
overlay_map_path="$STAGING_DIR/overlay-map.json"
ovmf_vars_copy=""
ovmf_template_sha256=""
ovmf_copy_sha256=""
ovmf_copy_size=""

if [ -n "$ovmf_vars_template" ]; then
    ovmf_vars_copy="$STAGING_DIR/$OVMF_VARS_NAME"
    ovmf_copy_result="$(prepare_ovmf_vars_copy "$ovmf_vars_template" \
        "$ovmf_vars_copy")" || fail "ovmf-vars-copy-invalid"
    ovmf_copy_extra=""
    if ! IFS=' ' read -r ovmf_template_sha256 ovmf_copy_sha256 \
            ovmf_copy_size ovmf_copy_extra <<<"$ovmf_copy_result"; then
        fail "ovmf-vars-copy-result-invalid"
    fi
    [[ "$ovmf_template_sha256" =~ ^[0-9a-f]{64}$ &&
        "$ovmf_copy_sha256" =~ ^[0-9a-f]{64}$ &&
        "$ovmf_copy_size" =~ ^[1-9][0-9]*$ &&
        -z "$ovmf_copy_extra" ]] || fail "ovmf-vars-copy-result-invalid"
fi

if ! "$QEMU_IMG" info --output=json "$base_image" >"$base_info_path" 2>/dev/null; then
    fail "base-image-info-unavailable"
fi
validate_base_info "$base_info_path" || fail "base-image-not-direct-qcow2"

if ! "$QEMU_IMG" create -f qcow2 -F qcow2 -b "$base_image" "$overlay_image" \
        >/dev/null 2>&1; then
    fail "overlay-create-failed"
fi
[ -f "$overlay_image" ] && [ ! -L "$overlay_image" ] ||
    fail "overlay-create-invalid"

if ! "$QEMU_IMG" info --output=json "$overlay_image" >"$overlay_info_path" 2>/dev/null; then
    fail "overlay-info-unavailable"
fi
if ! "$QEMU_IMG" map --output=json "$overlay_image" >"$overlay_map_path" 2>/dev/null; then
    fail "overlay-map-unavailable"
fi
if [ -n "$ovmf_vars_template" ]; then
    validate_ovmf_vars_copy_before_publish "$ovmf_vars_template" \
        "$ovmf_vars_copy" "$ovmf_template_sha256" "$ovmf_copy_sha256" \
        "$ovmf_copy_size" || fail "ovmf-vars-copy-changed-before-publish"
    write_and_validate_attestation "$base_info_path" "$overlay_info_path" \
        "$overlay_map_path" "$base_image" "$overlay_image" "$attestation" \
        "$ovmf_template_sha256" "$ovmf_copy_sha256" "$ovmf_copy_size" ||
        fail "overlay-attestation-invalid"
else
    write_and_validate_attestation "$base_info_path" "$overlay_info_path" \
        "$overlay_map_path" "$base_image" "$overlay_image" "$attestation" ||
        fail "overlay-attestation-invalid"
fi
[ -f "$attestation" ] && [ ! -L "$attestation" ] ||
    fail "overlay-attestation-missing"
for transient_metadata in "$base_info_path" "$overlay_info_path" "$overlay_map_path"; do
    /usr/bin/unlink "$transient_metadata" || fail "overlay-metadata-cleanup-failed"
done

if ! /bin/mv -T -n -- "$STAGING_DIR" "$final_dir" || [ -e "$STAGING_DIR" ]; then
    # -T prevents a directory collision from becoming a nested move, and -n
    # makes a late caller-created destination a hard no-clobber failure.
    fail "overlay-publish-collision"
fi
STAGING_DIR=""
printf 'OVERLAY_READY\n'
