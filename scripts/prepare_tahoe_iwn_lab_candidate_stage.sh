#!/usr/bin/env bash
# Prepare one exact IWN software-PMF lab candidate for a later, separately
# authorized Tahoe runtime experiment.
#
# This is deliberately an artifact bridge, not another build or runtime
# pipeline.  --collect consumes only the safe /tmp token printed by the
# existing run_tahoe_sae_quarantine_layer.sh gate, copies its already-built
# lab kext and trace executable into a fresh detached linked worktree, and
# creates the typed local receipt v2.  --stage later copies those exact bytes
# to fresh, restricted guest paths and re-hashes both archive and extraction.
#
# It never invokes a compiler, changes an AP or profile, installs/loads a
# kext, mutates AuxKC, starts a guest, or reboots anything.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
RECEIPT_TOOL_NAME="capture_tahoe_iwn_lab_candidate_receipt.py"
LAB_PROFILE="iwn-software-pmf-lab"
LAB_KEXT_REL="Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext"
LAB_TRACE_REL="Build/Debug/Tahoe-IwnSoftwarePmfLab/airport_itlwm_post_plti_trace"
ARCHIVE_NAME="AirportItlwm-iwn-software-pmf-lab.kext.zip"
TRACE_NAME="airport_itlwm_post_plti_trace"
RECEIPT_NAME="iwn-lab-candidate-receipt-v2.json"
MANIFEST_NAME="iwn-lab-bundle-manifest.json"
COLLECTION_REPORT_NAME="iwn-lab-collection-attestation.json"

PINNED_GUEST="devops@127.0.0.1"
PINNED_PORT=3322
PINNED_GUEST_BUILD="25C56"
PINNED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
PINNED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"
GATE_DIR_PREFIX="/tmp/aiam-tahoe-sae-layer-gate."
GUEST_CANDIDATE_PREFIX="/private/tmp/aiam-iwn-lab-candidate-"
GUEST_TRACE_PREFIX="/private/tmp/aiam-post-plti-trace-"

MODE=""
DRY_RUN=0
SELF_TEST=0
GATE_BUILD_DIR=""
GATE_BUILD_TOKEN=""
WORKTREE=""
ARTIFACTS_DIR=""
CANDIDATE_RECEIPT=""
ARCHIVE=""
TRACE_CLIENT=""
GUEST_CANDIDATE_DIR=""
GUEST_TRACE_DIR=""
GUEST_TOKEN=""
STAGE_REPORT=""
SOURCE_HEAD=""
KNOWN_HOSTS=""
COLLECT_STAGING=""
STAGE_STAGING=""
WORKTREE_CREATED=0
declare -a SSH
declare -a SCP

usage() {
    cat >&2 <<'EOF'
usage:
  prepare_tahoe_iwn_lab_candidate_stage.sh --collect \
    --gate-build-dir /tmp/aiam-tahoe-sae-layer-gate.TOKEN \
    --worktree /absolute/fresh/detached-worktree \
    --artifacts-dir /absolute/fresh/local-artifacts-dir [--dry-run]

  prepare_tahoe_iwn_lab_candidate_stage.sh --stage \
    --candidate-receipt /absolute/iwn-lab-candidate-receipt-v2.json \
    --archive /absolute/AirportItlwm-iwn-software-pmf-lab.kext.zip \
    --trace-client /absolute/airport_itlwm_post_plti_trace \
    --guest-candidate-dir /private/tmp/aiam-iwn-lab-candidate-TOKEN \
    --guest-trace-dir /private/tmp/aiam-post-plti-trace-TOKEN \
    --stage-report /absolute/fresh/private-stage-attestation.json [--dry-run]

  prepare_tahoe_iwn_lab_candidate_stage.sh --self-test

--collect does not run a build.  It accepts only the fresh gate-directory
token printed by the existing full Tahoe gate.  --stage does not install,
load, activate, or reboot; it only copies and verifies exact artifacts below
fresh /private/tmp paths on the pinned guest.  --dry-run makes no SSH
connection and creates no worktree, artifact directory, guest path, or report.
EOF
}

fail() {
    printf 'CANDIDATE_STAGE_FAIL:%s\n' "$1" >&2
    exit 1
}

delete_owned_directory() {
    local path="$1"
    [ -n "$path" ] && [ -d "$path" ] && [ ! -L "$path" ] || return 0
    case "$(basename -- "$path")" in
        .aiam-iwn-lab-collect.*|.aiam-iwn-lab-stage.*) ;;
        *) return 0 ;;
    esac
    /usr/bin/find -P "$path" -depth -delete >/dev/null 2>&1 || true
}

cleanup() {
    local status="$?"
    trap - EXIT HUP INT TERM
    if [ -n "$KNOWN_HOSTS" ] && [ -f "$KNOWN_HOSTS" ] && [ ! -L "$KNOWN_HOSTS" ]; then
        rm -f -- "$KNOWN_HOSTS"
    fi
    delete_owned_directory "$COLLECT_STAGING"
    delete_owned_directory "$STAGE_STAGING"
    if [ "$WORKTREE_CREATED" -eq 1 ] && [ -n "$WORKTREE" ] && [ -d "$WORKTREE" ] && [ ! -L "$WORKTREE" ]; then
        git -C "$ROOT" worktree remove --force "$WORKTREE" >/dev/null 2>&1 || true
    fi
    exit "$status"
}

trap cleanup EXIT
trap 'exit 1' HUP INT TERM

safe_leaf() {
    local value="$1"
    [[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,119}$ ]] &&
        [ "$value" != "." ] && [ "$value" != ".." ]
}

safe_token() {
    local value="$1"
    [[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]]
}

canonical_directory() {
    local input="$1" resolved
    case "$input" in
        /*) ;;
        *) return 1 ;;
    esac
    [ -d "$input" ] && [ ! -L "$input" ] || return 1
    resolved="$(CDPATH= cd -- "$input" && pwd -P)" || return 1
    [ -d "$resolved" ] && [ ! -L "$resolved" ] || return 1
    printf '%s\n' "$resolved"
}

canonical_existing_regular_file() {
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

canonical_new_path() {
    local input="$1" parent leaf resolved_parent
    case "$input" in
        /*) ;;
        *) return 1 ;;
    esac
    [ ! -e "$input" ] && [ ! -L "$input" ] || return 1
    parent="$(dirname -- "$input")"
    leaf="$(basename -- "$input")"
    safe_leaf "$leaf" || return 1
    resolved_parent="$(canonical_directory "$parent")" || return 1
    [ ! -e "$resolved_parent/$leaf" ] && [ ! -L "$resolved_parent/$leaf" ] || return 1
    printf '%s\n' "$resolved_parent/$leaf"
}

path_is_below() {
    local child="$1" parent="$2"
    [ "$child" = "$parent" ] || [[ "$child" == "$parent/"* ]]
}

require_new_path_outside_source() {
    local input="$1" label="$2" resolved
    resolved="$(canonical_new_path "$input")" || fail "$label-must-be-fresh-absolute-non-symlink-path"
    path_is_below "$resolved" "$ROOT" && fail "$label-must-be-outside-source-worktree"
    printf '%s\n' "$resolved"
}

parse_gate_build_dir() {
    local value="$1" token
    case "$value" in
        "$GATE_DIR_PREFIX"*) token="${value#"$GATE_DIR_PREFIX"}" ;;
        *) return 1 ;;
    esac
    safe_token "$token" || return 1
    # The full input must be reconstructed from one safe token; no caller
    # supplied remote hostname, shell fragment, or arbitrary remote path is
    # ever retained.
    [ "$value" = "$GATE_DIR_PREFIX$token" ] || return 1
    GATE_BUILD_TOKEN="$token"
    GATE_BUILD_DIR="$GATE_DIR_PREFIX$token"
}

parse_guest_pair() {
    local candidate="$1" trace="$2" candidate_token trace_token
    case "$candidate" in
        "$GUEST_CANDIDATE_PREFIX"*) candidate_token="${candidate#"$GUEST_CANDIDATE_PREFIX"}" ;;
        *) return 1 ;;
    esac
    case "$trace" in
        "$GUEST_TRACE_PREFIX"*) trace_token="${trace#"$GUEST_TRACE_PREFIX"}" ;;
        *) return 1 ;;
    esac
    safe_token "$candidate_token" && safe_token "$trace_token" || return 1
    [ "$candidate" = "$GUEST_CANDIDATE_PREFIX$candidate_token" ] || return 1
    [ "$trace" = "$GUEST_TRACE_PREFIX$trace_token" ] || return 1
    [ "$candidate_token" = "$trace_token" ] || return 1
    GUEST_TOKEN="$candidate_token"
    GUEST_CANDIDATE_DIR="$GUEST_CANDIDATE_PREFIX$GUEST_TOKEN"
    GUEST_TRACE_DIR="$GUEST_TRACE_PREFIX$GUEST_TOKEN"
}

require_clean_committed_source() {
    local status head
    status="$(git -C "$ROOT" status --porcelain=v1 --untracked-files=all)" ||
        fail "source-status-unavailable"
    [ -z "$status" ] || fail "source-worktree-is-not-clean"
    git -C "$ROOT" diff --quiet || fail "source-worktree-has-unstaged-changes"
    git -C "$ROOT" diff --cached --quiet || fail "source-worktree-has-staged-changes"
    head="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || true)"
    [[ "$head" =~ ^[0-9a-f]{40}$ ]] || fail "source-head-is-not-a-full-commit"
    SOURCE_HEAD="$head"
}

prepare_guest_transport() {
    KNOWN_HOSTS="$(mktemp /tmp/aiam-iwn-lab-candidate-known-hosts.XXXXXX)"
    chmod 600 "$KNOWN_HOSTS"
    printf '%s\n' "$PINNED_GUEST_HOSTKEY_LINE" > "$KNOWN_HOSTS"
    local observed
    observed="$(ssh-keygen -lf "$KNOWN_HOSTS" -E sha256 2>/dev/null |
        awk 'NR == 1 { print $2; exit }')"
    [ "$observed" = "$PINNED_GUEST_HOSTKEY_SHA256" ] ||
        fail "pinned-guest-host-key-fingerprint-mismatch"
    SSH=(
        ssh -F /dev/null -o BatchMode=yes -o ConnectTimeout=8
        -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$KNOWN_HOSTS"
        -o GlobalKnownHostsFile=/dev/null -o UpdateHostKeys=no -o LogLevel=ERROR
        -p "$PINNED_PORT" "$PINNED_GUEST"
    )
    SCP=(
        scp -F /dev/null -o BatchMode=yes -o ConnectTimeout=8
        -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$KNOWN_HOSTS"
        -o GlobalKnownHostsFile=/dev/null -o UpdateHostKeys=no -o LogLevel=ERROR
        -P "$PINNED_PORT"
    )
}

assert_pinned_guest_build() {
    local observed
    observed="$("${SSH[@]}" 'sw_vers -buildVersion')"
    [ "$observed" = "$PINNED_GUEST_BUILD" ] || fail "pinned-guest-build-mismatch"
}

assert_remote_gate_artifacts() {
    "${SSH[@]}" "set -eu
dir='$GATE_BUILD_DIR'
[ -d \"\$dir\" ] && [ ! -L \"\$dir\" ]
[ -d \"\$dir/$LAB_KEXT_REL\" ] && [ ! -L \"\$dir/$LAB_KEXT_REL\" ]
[ -f \"\$dir/$LAB_TRACE_REL\" ] && [ ! -L \"\$dir/$LAB_TRACE_REL\" ]
test -z \"\$(find \"\$dir/$LAB_KEXT_REL\" -type l -print -quit)\"
test -z \"\$(find \"\$dir/$LAB_KEXT_REL\" ! -type d ! -type f -print -quit)\"
"
}

make_fresh_detached_worktree() {
    [ ! -e "$WORKTREE" ] && [ ! -L "$WORKTREE" ] || fail "worktree-path-is-not-fresh"
    git -C "$ROOT" worktree add --detach "$WORKTREE" "$SOURCE_HEAD" >/dev/null
    WORKTREE_CREATED=1
    [ "$(git -C "$WORKTREE" rev-parse HEAD)" = "$SOURCE_HEAD" ] ||
        fail "detached-worktree-head-mismatch"
    [ ! -e "$WORKTREE/Build" ] && [ ! -L "$WORKTREE/Build" ] ||
        fail "detached-worktree-already-has-build-output"
}

extract_and_package_remote_artifacts() {
    local remote_tar="$1" trace_output="$2" archive_output="$3"
    python3 - "$remote_tar" "$WORKTREE" "$trace_output" "$archive_output" \
        "$LAB_KEXT_REL" "$LAB_TRACE_REL" <<'PY'
import os
import stat
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath


remote_tar = Path(sys.argv[1])
worktree = Path(sys.argv[2])
trace_output = Path(sys.argv[3])
archive_output = Path(sys.argv[4])
kext_rel = sys.argv[5]
trace_rel = sys.argv[6]
kext_prefix = kext_rel + "/"


def fail(reason: str) -> None:
    raise SystemExit(f"remote-artifact-{reason}")


def safe_member_name(name: str) -> None:
    if not name or name.startswith("/") or "\\" in name:
        fail("unsafe-tar-member")
    parts = PurePosixPath(name).parts
    if any(part in {"", ".", ".."} for part in parts):
        fail("unsafe-tar-member")
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in name):
        fail("unsafe-tar-member")


def ensure_parent(path: Path, root: Path) -> None:
    try:
        relative = path.parent.relative_to(root)
    except ValueError:
        fail("target-escaped-root")
    current = root
    for component in relative.parts:
        current = current / component
        if current.exists() or current.is_symlink():
            metadata = current.lstat()
            if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
                fail("target-parent-is-not-safe-directory")
        else:
            current.mkdir(mode=0o700)


def write_member(archive: tarfile.TarFile, member: tarfile.TarInfo,
                 target: Path, root: Path, mode: int) -> None:
    ensure_parent(target, root)
    if target.exists() or target.is_symlink():
        fail("duplicate-or-existing-target")
    source = archive.extractfile(member)
    if source is None:
        fail("unreadable-tar-member")
    fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    try:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            view = memoryview(chunk)
            while view:
                written = os.write(fd, view)
                if written <= 0:
                    fail("write-failed")
                view = view[written:]
        os.fsync(fd)
    finally:
        os.close(fd)
        source.close()


if not remote_tar.is_file() or remote_tar.is_symlink():
    fail("tar-not-regular")
if trace_output.exists() or trace_output.is_symlink():
    fail("trace-target-not-fresh")
if archive_output.exists() or archive_output.is_symlink():
    fail("archive-target-not-fresh")

seen_files: set[str] = set()
seen_trace = False
with tarfile.open(remote_tar, mode="r:") as archive:
    for member in archive.getmembers():
        safe_member_name(member.name)
        is_kext = member.name == kext_rel or member.name.startswith(kext_prefix)
        is_trace = member.name == trace_rel
        if not (is_kext or is_trace):
            fail("tar-member-outside-lab-artifacts")
        if member.issym() or member.islnk() or member.isdev() or member.isfifo():
            fail("tar-member-is-not-regular-or-directory")
        if member.isdir():
            continue
        if not member.isreg():
            fail("tar-member-is-not-regular-or-directory")
        if member.name in seen_files:
            fail("duplicate-tar-member")
        seen_files.add(member.name)
        if is_trace:
            if seen_trace:
                fail("duplicate-trace-member")
            write_member(archive, member, trace_output, trace_output.parent, 0o700)
            seen_trace = True
            continue
        target = worktree / member.name
        write_member(archive, member, target, worktree, 0o700 if member.mode & 0o111 else 0o600)

kext = worktree / kext_rel
info = kext / "Contents" / "Info.plist"
binary = kext / "Contents" / "MacOS" / "AirportItlwm"
if not (kext.is_dir() and not kext.is_symlink() and info.is_file() and
        not info.is_symlink() and binary.is_file() and not binary.is_symlink()):
    fail("required-kext-files-missing")
if not (trace_output.is_file() and not trace_output.is_symlink() and seen_trace):
    fail("required-trace-file-missing")
os.chmod(trace_output, 0o700)

with zipfile.ZipFile(archive_output, mode="x", compression=zipfile.ZIP_DEFLATED,
                     compresslevel=9) as packaged:
    files: list[Path] = []
    for item in sorted(kext.rglob("*")):
        metadata = item.lstat()
        relative = item.relative_to(kext).as_posix()
        if stat.S_ISLNK(metadata.st_mode):
            fail("kext-member-is-symlink")
        if stat.S_ISDIR(metadata.st_mode):
            continue
        if not stat.S_ISREG(metadata.st_mode):
            fail("kext-member-is-not-regular")
        if not relative or "\\" in relative or any(
            part in {"", ".", ".."} for part in PurePosixPath(relative).parts
        ):
            fail("unsafe-kext-member")
        files.append(item)
    if not files:
        fail("kext-has-no-regular-files")
    for item in files:
        relative = item.relative_to(kext).as_posix()
        info = zipfile.ZipInfo(f"AirportItlwm.kext/{relative}")
        info.date_time = (1980, 1, 1, 0, 0, 0)
        info.create_system = 3
        source_mode = stat.S_IMODE(item.lstat().st_mode)
        info.external_attr = ((stat.S_IFREG | (source_mode & 0o777)) << 16)
        packaged.writestr(info, item.read_bytes(), compress_type=zipfile.ZIP_DEFLATED,
                          compresslevel=9)

os.chmod(archive_output, 0o600)
PY
}

capture_receipt_v2() {
    local exclude_file="$1" receipt_output="$2"
    printf 'Build/\n' > "$exclude_file"
    chmod 600 "$exclude_file"
    # Build/ is intentionally ignored only through this child process's
    # private config.  The detached source tree itself stays exactly at the
    # committed HEAD that receipt v2 records; every other untracked path still
    # makes the receipt helper fail closed.
    env -i PATH="$PATH" HOME="${HOME:-/nonexistent}" LC_ALL=C \
        GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null \
        GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.excludesFile \
        GIT_CONFIG_VALUE_0="$exclude_file" \
        python3 "$WORKTREE/scripts/$RECEIPT_TOOL_NAME" \
            --profile "$LAB_PROFILE" \
            --staged-kext "$WORKTREE/$LAB_KEXT_REL" \
            --archive "$COLLECT_STAGING/$ARCHIVE_NAME" \
            --trace-client "$COLLECT_STAGING/$TRACE_NAME" \
            --output "$receipt_output"
}

verify_collection_receipt() {
    local receipt="$1"
    python3 - "$ROOT/scripts" "$receipt" "$SOURCE_HEAD" <<'PY'
import re
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from capture_tahoe_iwn_lab_candidate_receipt import load_direct_runtime_candidate_receipt

candidate = load_direct_runtime_candidate_receipt(Path(sys.argv[2]))
expected_head = sys.argv[3]
if candidate.get("source_commit") != expected_head:
    raise SystemExit("collection receipt source commit mismatch")
if candidate.get("profile") != "iwn-software-pmf-lab":
    raise SystemExit("collection receipt profile mismatch")
if candidate.get("staged_kext_repo_path") != (
        "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext"):
    raise SystemExit("collection receipt staged path mismatch")
for key in (
    "archive_sha256", "info_plist_sha256", "binary_sha256",
    "bundle_tree_sha256", "trace_client_sha256",
):
    if re.fullmatch(r"[0-9a-f]{64}", str(candidate.get(key, ""))) is None:
        raise SystemExit(f"collection receipt malformed {key}")
PY
}

write_collection_report() {
    local output="$1" receipt="$2"
    python3 - "$ROOT/scripts" "$output" "$receipt" "$GATE_BUILD_TOKEN" \
        "$SOURCE_HEAD" "$ARCHIVE_NAME" "$TRACE_NAME" <<'PY'
import hashlib
import json
import os
import stat
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from capture_tahoe_iwn_lab_candidate_receipt import load_direct_runtime_candidate_receipt

output = Path(sys.argv[2])
receipt = Path(sys.argv[3])
token = sys.argv[4]
source_head = sys.argv[5]
archive_name = sys.argv[6]
trace_name = sys.argv[7]
candidate = load_direct_runtime_candidate_receipt(receipt)
if candidate.get("source_commit") != source_head:
    raise SystemExit("collection report source commit mismatch")
document = {
    "schema": "itlwm-tahoe-iwn-lab-candidate-collection/v1",
    "gate_build_dir_token": token,
    "candidate_receipt_sha256": hashlib.sha256(receipt.read_bytes()).hexdigest(),
    "candidate": candidate,
    "artifacts": {
        "archive_name": archive_name,
        "trace_client_name": trace_name,
    },
    "validation": {
        "source_clean_committed": True,
        "pinned_guest_host_key": True,
        "existing_full_gate_build_dir_token_only": True,
        "local_detached_worktree_materialized": True,
        "typed_receipt_v2_created": True,
    },
    "non_claims": {
        "compiler_invoked_by_adapter": False,
        "candidate_kext_installed": False,
        "candidate_kext_loaded": False,
        "auxkc_mutated": False,
        "guest_rebooted": False,
        "runtime_experiment_performed": False,
    },
}
payload = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
try:
    os.write(fd, payload)
    os.fsync(fd)
finally:
    os.close(fd)
if stat.S_IMODE(output.stat().st_mode) != 0o600:
    raise SystemExit("collection report mode mismatch")
PY
}

collect_candidate() {
    require_clean_committed_source
    WORKTREE="$(require_new_path_outside_source "$WORKTREE" "worktree")"
    ARTIFACTS_DIR="$(require_new_path_outside_source "$ARTIFACTS_DIR" "artifacts-dir")"
    path_is_below "$ARTIFACTS_DIR" "$WORKTREE" && fail "artifacts-dir-must-not-be-inside-worktree"
    path_is_below "$WORKTREE" "$ARTIFACTS_DIR" && fail "worktree-must-not-be-inside-artifacts-dir"
    parse_gate_build_dir "$GATE_BUILD_DIR" || fail "gate-build-dir-is-not-a-safe-full-gate-token"

    if [ "$DRY_RUN" -eq 1 ]; then
        printf 'COLLECT_DRY_RUN_READY source_commit=%s gate_token=%s\n' \
            "$SOURCE_HEAD" "$GATE_BUILD_TOKEN"
        return 0
    fi

    prepare_guest_transport
    assert_pinned_guest_build
    assert_remote_gate_artifacts
    make_fresh_detached_worktree
    local artifacts_parent
    artifacts_parent="$(dirname -- "$ARTIFACTS_DIR")"
    COLLECT_STAGING="$(mktemp -d "$artifacts_parent/.aiam-iwn-lab-collect.XXXXXX")"
    chmod 700 "$COLLECT_STAGING"

    # Stream only the two fixed output paths.  The remote directory itself is
    # not copied, and its user-provided spelling was discarded in favor of the
    # source-controlled prefix plus one validated token above.
    "${SSH[@]}" "cd '$GATE_BUILD_DIR' && /usr/bin/tar -cf - '$LAB_KEXT_REL' '$LAB_TRACE_REL'" \
        > "$COLLECT_STAGING/remote-gate-artifacts.tar"
    extract_and_package_remote_artifacts \
        "$COLLECT_STAGING/remote-gate-artifacts.tar" \
        "$COLLECT_STAGING/$TRACE_NAME" "$COLLECT_STAGING/$ARCHIVE_NAME"
    rm -f -- "$COLLECT_STAGING/remote-gate-artifacts.tar"
    capture_receipt_v2 "$COLLECT_STAGING/worktree-build.exclude" \
        "$COLLECT_STAGING/$RECEIPT_NAME"
    verify_collection_receipt "$COLLECT_STAGING/$RECEIPT_NAME"
    write_collection_report "$COLLECT_STAGING/$COLLECTION_REPORT_NAME" \
        "$COLLECT_STAGING/$RECEIPT_NAME"
    rm -f -- "$COLLECT_STAGING/worktree-build.exclude"
    mv -- "$COLLECT_STAGING" "$ARTIFACTS_DIR"
    COLLECT_STAGING=""
    WORKTREE_CREATED=0
    printf 'COLLECT_READY source_commit=%s receipt=%s\n' \
        "$SOURCE_HEAD" "$ARTIFACTS_DIR/$RECEIPT_NAME"
}

validate_stage_inputs_and_write_manifest() {
    local manifest="$1"
    python3 - "$ROOT/scripts" "$ROOT" "$CANDIDATE_RECEIPT" "$ARCHIVE" \
        "$TRACE_CLIENT" "$manifest" "$SOURCE_HEAD" <<'PY'
import hashlib
import json
import os
import re
import stat
import sys
import zipfile
from pathlib import Path, PurePosixPath

sys.path.insert(0, sys.argv[1])
from capture_tahoe_iwn_lab_candidate_receipt import (
    BUNDLE_ROOT,
    archive_bundle_files,
    archive_identity,
    load_direct_runtime_candidate_receipt,
    trace_client_identity,
)
from tahoe_source_identity import source_identity

root = Path(sys.argv[2])
receipt_path = Path(sys.argv[3])
archive_path = Path(sys.argv[4])
trace_path = Path(sys.argv[5])
manifest_path = Path(sys.argv[6])
source_head = sys.argv[7]


def fail(reason: str) -> None:
    raise SystemExit(f"stage-input-{reason}")


def safe_relative(value: str) -> None:
    if not value or value.startswith("/") or "\\" in value:
        fail("unsafe-archive-member")
    parts = PurePosixPath(value).parts
    if any(part in {"", ".", ".."} for part in parts):
        fail("unsafe-archive-member")
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in value):
        fail("unsafe-archive-member")


candidate = load_direct_runtime_candidate_receipt(receipt_path)
if candidate.get("source_commit") != source_head:
    fail("receipt-source-commit-does-not-match-clean-head")
identity = source_identity(root, "HEAD")
if (candidate.get("source_identity_sha256") != identity["identity"] or
        candidate.get("source_identity_paths_count") != identity["included_paths_count"]):
    fail("receipt-source-identity-does-not-match-clean-head")
archive = archive_identity(archive_path)
for key in (
    "archive_sha256", "info_plist_sha256", "binary_sha256",
    "bundle_tree_sha256", "macho_uuid", "bundle_id",
):
    if archive.get(key) != candidate.get(key):
        fail(f"receipt-{key}-does-not-match-local-archive")
trace = trace_client_identity(trace_path)
if trace.get("trace_client_sha256") != candidate.get("trace_client_sha256"):
    fail("receipt-trace-client-does-not-match-local-trace")

files: dict[str, str] = {}
prefix = BUNDLE_ROOT + "/"
try:
    with zipfile.ZipFile(archive_path) as zipped:
        for item in zipped.infolist():
            if item.filename == BUNDLE_ROOT + "/" and item.is_dir():
                continue
            if not item.filename.startswith(prefix):
                fail("archive-has-member-outside-kext-root")
            relative = item.filename[len(prefix):]
            safe_relative(relative)
            if item.is_dir():
                continue
            mode = item.external_attr >> 16
            if stat.S_IFMT(mode) == stat.S_IFLNK:
                fail("archive-has-symlink-member")
        bundle_files = archive_bundle_files(zipped)
except zipfile.BadZipFile as error:
    raise SystemExit(f"stage-input-invalid-archive:{error}") from error
for relative, data in bundle_files.items():
    safe_relative(relative)
    if relative in files:
        fail("archive-has-duplicate-member")
    files[relative] = hashlib.sha256(data).hexdigest()
if not files:
    fail("archive-has-no-kext-files")

receipt_sha256 = hashlib.sha256(receipt_path.read_bytes()).hexdigest()
document = {
    "schema": "itlwm-tahoe-iwn-lab-bundle-manifest/v1",
    "archive_sha256": archive["archive_sha256"],
    "trace_client_sha256": trace["trace_client_sha256"],
    "bundle_tree_sha256": archive["bundle_tree_sha256"],
    "files": [
        {"path": path, "sha256": files[path]}
        for path in sorted(files)
    ],
}
payload = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
fd = os.open(manifest_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
             getattr(os, "O_NOFOLLOW", 0), 0o600)
try:
    os.write(fd, payload)
    os.fsync(fd)
finally:
    os.close(fd)
print("|".join((
    str(candidate["source_commit"]),
    str(candidate["source_identity_sha256"]),
    str(candidate["source_identity_paths_count"]),
    str(candidate["profile"]),
    str(candidate["staged_kext_repo_path"]),
    str(candidate["archive_sha256"]),
    str(candidate["info_plist_sha256"]),
    str(candidate["binary_sha256"]),
    str(candidate["bundle_tree_sha256"]),
    str(candidate["macho_uuid"]),
    str(candidate["bundle_id"]),
    str(candidate["trace_client_sha256"]),
    receipt_sha256,
)))
PY
}

stage_values_shape_valid() {
    local values="$1" field
    local -a fields=()

    IFS='|' read -r -a fields <<<"$values"
    [ "${#fields[@]}" -eq 13 ] || return 1
    for field in "${fields[@]}"; do
        [ -n "$field" ] || return 1
    done
}

create_remote_stage_directories() {
    "${SSH[@]}" "set -eu
[ -d /private ] && [ ! -L /private ]
[ -d /private/tmp ] && [ ! -L /private/tmp ]
candidate='$GUEST_CANDIDATE_DIR'
trace='$GUEST_TRACE_DIR'
[ ! -e \"\$candidate\" ] && [ ! -L \"\$candidate\" ]
[ ! -e \"\$trace\" ] && [ ! -L \"\$trace\" ]
umask 077
mkdir \"\$candidate\" \"\$trace\"
chmod 700 \"\$candidate\" \"\$trace\"
"
}

verify_remote_stage() {
    local archive_sha="$1" trace_sha="$2" info_sha="$3" binary_sha="$4"
    local tree_sha="$5" receipt_sha="$6"
    "${SSH[@]}" "exec /usr/bin/python3 - '$GUEST_CANDIDATE_DIR' '$GUEST_TRACE_DIR/$TRACE_NAME' '$ARCHIVE_NAME' '$RECEIPT_NAME' '$MANIFEST_NAME' '$archive_sha' '$trace_sha' '$info_sha' '$binary_sha' '$tree_sha' '$receipt_sha'" <<'PY'
import hashlib
import json
import os
import stat
import sys
import zipfile
from pathlib import PurePosixPath

(candidate_dir, trace_path, archive_name, receipt_name, manifest_name,
 archive_sha, trace_sha, info_sha, binary_sha, tree_sha,
 receipt_sha) = sys.argv[1:]


def fail(reason: str) -> None:
    raise SystemExit(f"remote-stage-{reason}")


def regular(path: str) -> os.stat_result:
    try:
        metadata = os.lstat(path)
    except OSError as error:
        fail(f"missing-{type(error).__name__}")
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        fail("not-regular")
    return metadata


def directory(path: str) -> os.stat_result:
    try:
        metadata = os.lstat(path)
    except OSError as error:
        fail(f"missing-{type(error).__name__}")
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
        fail("not-directory")
    return metadata


def digest(path: str) -> str:
    regular(path)
    value = hashlib.sha256()
    with open(path, "rb", buffering=0) as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                return value.hexdigest()
            value.update(chunk)


def reject_duplicate_keys(pairs):
    output = {}
    for key, value in pairs:
        if key in output:
            fail("duplicate-manifest-key")
        output[key] = value
    return output


def safe_relative(value: str) -> None:
    if not value or value.startswith("/") or "\\" in value:
        fail("unsafe-member-path")
    parts = PurePosixPath(value).parts
    if any(part in {"", ".", ".."} for part in parts):
        fail("unsafe-member-path")
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in value):
        fail("unsafe-member-path")


def safe_target(root: str, relative: str) -> str:
    safe_relative(relative)
    output = os.path.join(root, *PurePosixPath(relative).parts)
    if not output.startswith(root + os.sep):
        fail("target-escaped-root")
    return output


def make_parent(path: str, root: str) -> None:
    parent = os.path.dirname(path)
    relative = os.path.relpath(parent, root)
    if relative == ".":
        return
    if relative.startswith(".." + os.sep) or relative == "..":
        fail("target-parent-escaped-root")
    current = root
    for component in relative.split(os.sep):
        current = os.path.join(current, component)
        try:
            metadata = os.lstat(current)
        except FileNotFoundError:
            os.mkdir(current, 0o700)
            continue
        if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
            fail("unsafe-target-parent")


directory(candidate_dir)
directory(os.path.dirname(trace_path))
archive_path = os.path.join(candidate_dir, archive_name)
receipt_path = os.path.join(candidate_dir, receipt_name)
manifest_path = os.path.join(candidate_dir, manifest_name)
if digest(archive_path) != archive_sha:
    fail("archive-digest-mismatch")
if digest(trace_path) != trace_sha:
    fail("trace-digest-mismatch")
if digest(receipt_path) != receipt_sha:
    fail("receipt-digest-mismatch")
regular(manifest_path)
try:
    with open(manifest_path, encoding="utf-8") as source:
        manifest = json.load(source, object_pairs_hook=reject_duplicate_keys)
except Exception as error:
    fail(f"manifest-read-{type(error).__name__}")
if not isinstance(manifest, dict) or manifest.get("schema") != (
        "itlwm-tahoe-iwn-lab-bundle-manifest/v1"):
    fail("manifest-schema")
if (manifest.get("archive_sha256") != archive_sha or
        manifest.get("trace_client_sha256") != trace_sha or
        manifest.get("bundle_tree_sha256") != tree_sha):
    fail("manifest-identity")
items = manifest.get("files")
if not isinstance(items, list) or not items:
    fail("manifest-files")
expected: dict[str, str] = {}
for item in items:
    if not isinstance(item, dict) or set(item) != {"path", "sha256"}:
        fail("manifest-file-shape")
    path = item["path"]
    value = item["sha256"]
    if not isinstance(path, str) or not isinstance(value, str):
        fail("manifest-file-type")
    safe_relative(path)
    if path in expected or len(value) != 64 or any(ch not in "0123456789abcdef" for ch in value):
        fail("manifest-file-value")
    expected[path] = value

extract_root = os.path.join(candidate_dir, "extracted")
if os.path.lexists(extract_root):
    fail("extracted-target-not-fresh")
os.mkdir(extract_root, 0o700)
seen: set[str] = set()
prefix = "AirportItlwm.kext/"
try:
    with zipfile.ZipFile(archive_path) as archive:
        for item in archive.infolist():
            if item.filename == prefix and item.is_dir():
                continue
            if not item.filename.startswith(prefix):
                fail("archive-member-outside-kext-root")
            relative = item.filename[len(prefix):]
            safe_relative(relative)
            mode = item.external_attr >> 16
            if stat.S_IFMT(mode) == stat.S_IFLNK:
                fail("archive-member-symlink")
            if item.is_dir():
                continue
            if relative not in expected or relative in seen:
                fail("archive-member-does-not-match-manifest")
            target = safe_target(extract_root, "AirportItlwm.kext/" + relative)
            make_parent(target, extract_root)
            source = archive.open(item, "r")
            fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                         getattr(os, "O_NOFOLLOW", 0), 0o600)
            try:
                while True:
                    chunk = source.read(1024 * 1024)
                    if not chunk:
                        break
                    view = memoryview(chunk)
                    while view:
                        written = os.write(fd, view)
                        if written <= 0:
                            fail("extract-write")
                        view = view[written:]
                os.fsync(fd)
            finally:
                os.close(fd)
                source.close()
            seen.add(relative)
except zipfile.BadZipFile as error:
    fail(f"archive-read-{type(error).__name__}")
if seen != set(expected):
    fail("archive-member-set-does-not-match-manifest")
for relative, expected_sha in expected.items():
    extracted = safe_target(extract_root, "AirportItlwm.kext/" + relative)
    if digest(extracted) != expected_sha:
        fail("extracted-member-digest-mismatch")

info_path = safe_target(extract_root, "AirportItlwm.kext/Contents/Info.plist")
binary_path = safe_target(extract_root, "AirportItlwm.kext/Contents/MacOS/AirportItlwm")
if digest(info_path) != info_sha or digest(binary_path) != binary_sha:
    fail("extracted-required-identity-mismatch")
logical = hashlib.sha256()
logical.update(b"tahoe-iwn-lab-kext-logical-tree/v1\0")
for relative in sorted(expected):
    logical.update(relative.encode("utf-8", "surrogateescape"))
    logical.update(b"\0")
    logical.update(bytes.fromhex(expected[relative]))
    logical.update(b"\0")
if logical.hexdigest() != tree_sha:
    fail("extracted-tree-digest-mismatch")

for path, mode in ((archive_path, 0o600), (receipt_path, 0o600),
                   (manifest_path, 0o600), (trace_path, 0o700)):
    os.chmod(path, mode)
os.chmod(binary_path, 0o700)
print("PRIVATE_STAGE_VERIFIED")
PY
}

write_stage_report() {
    local receipt_sha="$1" archive_sha="$2" trace_sha="$3"
    python3 - "$ROOT/scripts" "$STAGE_REPORT" "$CANDIDATE_RECEIPT" \
        "$receipt_sha" "$archive_sha" "$trace_sha" "$GUEST_CANDIDATE_DIR" \
        "$GUEST_TRACE_DIR/$TRACE_NAME" <<'PY'
import json
import os
import stat
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from capture_tahoe_iwn_lab_candidate_receipt import load_direct_runtime_candidate_receipt

output = Path(sys.argv[2])
receipt = Path(sys.argv[3])
receipt_sha, archive_sha, trace_sha = sys.argv[4:7]
candidate_dir, trace_path = sys.argv[7:9]
candidate = load_direct_runtime_candidate_receipt(receipt)
document = {
    "schema": "itlwm-tahoe-iwn-lab-private-stage/v1",
    "candidate_receipt_sha256": receipt_sha,
    "candidate": candidate,
    "guest_stage": {
        "candidate_dir": candidate_dir,
        "archive_path": candidate_dir + "/AirportItlwm-iwn-software-pmf-lab.kext.zip",
        "receipt_path": candidate_dir + "/iwn-lab-candidate-receipt-v2.json",
        "manifest_path": candidate_dir + "/iwn-lab-bundle-manifest.json",
        "extracted_kext_path": candidate_dir + "/extracted/AirportItlwm.kext",
        "trace_tool_path": trace_path,
    },
    "validation": {
        "source_clean_committed": True,
        "receipt_v2_bound_to_clean_head": True,
        "local_archive_rehashed": True,
        "local_trace_rehashed": True,
        "pinned_guest_host_key": True,
        "remote_archive_rehashed": True,
        "remote_trace_rehashed": True,
        "remote_extracted_candidate_rehashed": True,
    },
    "artifact_digests": {
        "archive_sha256": archive_sha,
        "trace_client_sha256": trace_sha,
    },
    "non_claims": {
        "candidate_kext_installed": False,
        "candidate_kext_loaded": False,
        "auxkc_mutated": False,
        "guest_rebooted": False,
        "runtime_experiment_performed": False,
    },
}
payload = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
             getattr(os, "O_NOFOLLOW", 0), 0o600)
try:
    os.write(fd, payload)
    os.fsync(fd)
finally:
    os.close(fd)
if stat.S_IMODE(output.stat().st_mode) != 0o600:
    raise SystemExit("private stage report mode mismatch")
PY
}

candidate_receipt_has_private_mode() {
    python3 - "$1" <<'PY'
import os
import stat
import sys

value = os.lstat(sys.argv[1])
if (stat.S_ISLNK(value.st_mode) or not stat.S_ISREG(value.st_mode) or
        value.st_nlink != 1 or stat.S_IMODE(value.st_mode) != 0o600):
    raise SystemExit(1)
PY
}

stage_candidate() {
    require_clean_committed_source
    CANDIDATE_RECEIPT="$(canonical_existing_regular_file "$CANDIDATE_RECEIPT")" ||
        fail "candidate-receipt-must-be-absolute-regular-non-symlink-file"
    candidate_receipt_has_private_mode "$CANDIDATE_RECEIPT" ||
        fail "candidate-receipt-must-be-private-0600"
    ARCHIVE="$(canonical_existing_regular_file "$ARCHIVE")" ||
        fail "archive-must-be-absolute-regular-non-symlink-file"
    TRACE_CLIENT="$(canonical_existing_regular_file "$TRACE_CLIENT")" ||
        fail "trace-client-must-be-absolute-regular-non-symlink-file"
    STAGE_REPORT="$(require_new_path_outside_source "$STAGE_REPORT" "stage-report")"
    parse_guest_pair "$GUEST_CANDIDATE_DIR" "$GUEST_TRACE_DIR" ||
        fail "guest-private-paths-must-be-fresh-matching-safe-token-pair"
    local stage_parent values
    local -a stage_fields=()
    stage_parent="$(dirname -- "$STAGE_REPORT")"
    STAGE_STAGING="$(mktemp -d "$stage_parent/.aiam-iwn-lab-stage.XXXXXX")"
    chmod 700 "$STAGE_STAGING"
    values="$(validate_stage_inputs_and_write_manifest "$STAGE_STAGING/$MANIFEST_NAME")"
    stage_values_shape_valid "$values" || fail "stage-input-value-shape"
    IFS='|' read -r -a stage_fields <<<"$values"
    _source_commit="${stage_fields[0]}"
    _source_identity="${stage_fields[1]}"
    _source_path_count="${stage_fields[2]}"
    _profile="${stage_fields[3]}"
    _staged_path="${stage_fields[4]}"
    _archive_sha="${stage_fields[5]}"
    _info_sha="${stage_fields[6]}"
    _binary_sha="${stage_fields[7]}"
    _tree_sha="${stage_fields[8]}"
    _macho_uuid="${stage_fields[9]}"
    _bundle_id="${stage_fields[10]}"
    _trace_sha="${stage_fields[11]}"
    _receipt_sha="${stage_fields[12]}"
    if [ "$DRY_RUN" -eq 1 ]; then
        printf 'STAGE_DRY_RUN_READY source_commit=%s stage_token=%s\n' \
            "$SOURCE_HEAD" "$GUEST_TOKEN"
        return 0
    fi

    prepare_guest_transport
    assert_pinned_guest_build
    create_remote_stage_directories
    "${SCP[@]}" "$ARCHIVE" "$CANDIDATE_RECEIPT" \
        "$STAGE_STAGING/$MANIFEST_NAME" \
        "$PINNED_GUEST:$GUEST_CANDIDATE_DIR/"
    "${SCP[@]}" "$TRACE_CLIENT" \
        "$PINNED_GUEST:$GUEST_TRACE_DIR/$TRACE_NAME"
    verify_remote_stage "$_archive_sha" "$_trace_sha" "$_info_sha" \
        "$_binary_sha" "$_tree_sha" "$_receipt_sha"
    write_stage_report "$_receipt_sha" "$_archive_sha" "$_trace_sha"
    printf 'PRIVATE_STAGE_READY source_commit=%s report=%s\n' \
        "$SOURCE_HEAD" "$STAGE_REPORT"
}

self_test() {
    local original_gate="$GATE_BUILD_DIR" original_candidate="$GUEST_CANDIDATE_DIR"
    local original_trace="$GUEST_TRACE_DIR"
    local valid_stage_values='one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|thirteen'
    GATE_BUILD_DIR="${GATE_DIR_PREFIX}Ab9_-.z"
    parse_gate_build_dir "$GATE_BUILD_DIR" || fail "self-test-safe-gate-token"
    [ "$GATE_BUILD_TOKEN" = "Ab9_-.z" ] || fail "self-test-gate-token-round-trip"
    if parse_gate_build_dir "/tmp/not-a-gate-token"; then
        fail "self-test-unsafe-gate-token-accepted"
    fi
    GUEST_CANDIDATE_DIR="${GUEST_CANDIDATE_PREFIX}candidate.17"
    GUEST_TRACE_DIR="${GUEST_TRACE_PREFIX}candidate.17"
    parse_guest_pair "$GUEST_CANDIDATE_DIR" "$GUEST_TRACE_DIR" ||
        fail "self-test-safe-private-pair"
    if parse_guest_pair "${GUEST_CANDIDATE_PREFIX}one" "${GUEST_TRACE_PREFIX}two"; then
        fail "self-test-mismatched-private-token-accepted"
    fi
    stage_values_shape_valid "$valid_stage_values" ||
        fail "self-test-valid-stage-values-rejected"
    if stage_values_shape_valid 'one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve'; then
        fail "self-test-short-stage-values-accepted"
    fi
    if stage_values_shape_valid 'one|two||four|five|six|seven|eight|nine|ten|eleven|twelve|thirteen'; then
        fail "self-test-empty-stage-value-accepted"
    fi
    safe_leaf "fresh-artifacts.1" || fail "self-test-safe-leaf"
    if safe_leaf "../not-safe"; then
        fail "self-test-unsafe-leaf-accepted"
    fi
    GATE_BUILD_DIR="$original_gate"
    GUEST_CANDIDATE_DIR="$original_candidate"
    GUEST_TRACE_DIR="$original_trace"
    printf 'PASS: Tahoe IWN lab candidate-stage self-test\n'
}

seen_collect=0
seen_stage=0
seen_dry_run=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --collect)
            [ "$seen_collect" -eq 0 ] || { usage; exit 2; }
            seen_collect=1; MODE="collect"; shift
            ;;
        --stage)
            [ "$seen_stage" -eq 0 ] || { usage; exit 2; }
            seen_stage=1; MODE="stage"; shift
            ;;
        --dry-run)
            [ "$seen_dry_run" -eq 0 ] || { usage; exit 2; }
            seen_dry_run=1; DRY_RUN=1; shift
            ;;
        --self-test)
            SELF_TEST=1; shift
            ;;
        --gate-build-dir|--worktree|--artifacts-dir|--candidate-receipt|--archive|--trace-client|--guest-candidate-dir|--guest-trace-dir|--stage-report)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --gate-build-dir) [ -z "$GATE_BUILD_DIR" ] || { usage; exit 2; }; GATE_BUILD_DIR="$2" ;;
                --worktree) [ -z "$WORKTREE" ] || { usage; exit 2; }; WORKTREE="$2" ;;
                --artifacts-dir) [ -z "$ARTIFACTS_DIR" ] || { usage; exit 2; }; ARTIFACTS_DIR="$2" ;;
                --candidate-receipt) [ -z "$CANDIDATE_RECEIPT" ] || { usage; exit 2; }; CANDIDATE_RECEIPT="$2" ;;
                --archive) [ -z "$ARCHIVE" ] || { usage; exit 2; }; ARCHIVE="$2" ;;
                --trace-client) [ -z "$TRACE_CLIENT" ] || { usage; exit 2; }; TRACE_CLIENT="$2" ;;
                --guest-candidate-dir) [ -z "$GUEST_CANDIDATE_DIR" ] || { usage; exit 2; }; GUEST_CANDIDATE_DIR="$2" ;;
                --guest-trace-dir) [ -z "$GUEST_TRACE_DIR" ] || { usage; exit 2; }; GUEST_TRACE_DIR="$2" ;;
                --stage-report) [ -z "$STAGE_REPORT" ] || { usage; exit 2; }; STAGE_REPORT="$2" ;;
            esac
            shift 2
            ;;
        -h|--help)
            usage; exit 0
            ;;
        *)
            usage; exit 2
            ;;
    esac
done

if [ "$SELF_TEST" -eq 1 ]; then
    [ -z "$MODE" ] && [ "$DRY_RUN" -eq 0 ] && [ -z "$GATE_BUILD_DIR$WORKTREE$ARTIFACTS_DIR$CANDIDATE_RECEIPT$ARCHIVE$TRACE_CLIENT$GUEST_CANDIDATE_DIR$GUEST_TRACE_DIR$STAGE_REPORT" ] || {
        usage; exit 2;
    }
    self_test
    exit 0
fi

[ "$seen_collect" -eq 1 ] && [ "$seen_stage" -eq 0 ] || {
    [ "$seen_stage" -eq 1 ] && [ "$seen_collect" -eq 0 ] || { usage; exit 2; }
}

case "$MODE" in
    collect)
        [ -n "$GATE_BUILD_DIR" ] && [ -n "$WORKTREE" ] && [ -n "$ARTIFACTS_DIR" ] || {
            usage; exit 2;
        }
        [ -z "$CANDIDATE_RECEIPT$ARCHIVE$TRACE_CLIENT$GUEST_CANDIDATE_DIR$GUEST_TRACE_DIR$STAGE_REPORT" ] || {
            usage; exit 2;
        }
        collect_candidate
        ;;
    stage)
        [ -n "$CANDIDATE_RECEIPT" ] && [ -n "$ARCHIVE" ] && [ -n "$TRACE_CLIENT" ] && \
            [ -n "$GUEST_CANDIDATE_DIR" ] && [ -n "$GUEST_TRACE_DIR" ] && [ -n "$STAGE_REPORT" ] || {
            usage; exit 2;
        }
        [ -z "$GATE_BUILD_DIR$WORKTREE$ARTIFACTS_DIR" ] || { usage; exit 2; }
        stage_candidate
        ;;
    *)
        usage; exit 2
        ;;
esac
