#!/usr/bin/env bash
# Collect and stage the bounded public-recovery helper as a sidecar artifact.
# It consumes a prior Tahoe gate by a safe token and never builds, installs,
# loads, invokes the helper, changes networking, or reboots either machine.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
RECEIPT_TOOL_NAME="capture_tahoe_iwn_lab_public_recovery_receipt.py"
HELPER_REPO_PATH="Build/Debug/Tahoe/airport_itlwm_lab_public_recovery"
HELPER_NAME="airport_itlwm_lab_public_recovery"
PUBLIC_RECEIPT_NAME="iwn-public-recovery-receipt-v1.json"
COLLECTION_REPORT_NAME="iwn-public-recovery-collection-attestation.json"

PINNED_GUEST="devops@127.0.0.1"
PINNED_PORT=3322
PINNED_GUEST_BUILD="25C56"
PINNED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
PINNED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"
GATE_DIR_PREFIX="/tmp/aiam-tahoe-sae-layer-gate."
GUEST_DIR_PREFIX="/private/tmp/aiam-iwn-public-recovery-"

MODE=""
DRY_RUN=0
SELF_TEST=0
GATE_BUILD_DIR=""
GATE_BUILD_TOKEN=""
WORKTREE=""
ARTIFACTS_DIR=""
CANDIDATE_RECEIPT=""
PUBLIC_RECEIPT=""
HELPER=""
GUEST_DIR=""
GUEST_TOKEN=""
STAGE_REPORT=""
VALIDATE_STAGE_REPORT=""
SOURCE_HEAD=""
KNOWN_HOSTS=""
COLLECT_STAGING=""
declare -a SSH
declare -a SCP

usage() {
    cat >&2 <<'EOF'
usage:
  prepare_tahoe_iwn_public_recovery_stage.sh --collect \
    --gate-build-dir /tmp/aiam-tahoe-sae-layer-gate.TOKEN \
    --candidate-receipt /absolute/iwn-lab-candidate-receipt-v2.json \
    --worktree /absolute/fresh/detached-worktree \
    --artifacts-dir /absolute/fresh/local-artifacts-dir [--dry-run]

  prepare_tahoe_iwn_public_recovery_stage.sh --stage \
    --candidate-receipt /absolute/iwn-lab-candidate-receipt-v2.json \
    --public-recovery-receipt /absolute/iwn-public-recovery-receipt-v1.json \
    --helper /absolute/airport_itlwm_lab_public_recovery \
    --guest-dir /private/tmp/aiam-iwn-public-recovery-TOKEN \
    --stage-report /absolute/fresh/private-stage-attestation.json [--dry-run]

  prepare_tahoe_iwn_public_recovery_stage.sh --self-test

  prepare_tahoe_iwn_public_recovery_stage.sh --validate-stage-report \
    /absolute/iwn-public-recovery-stage-attestation.json
EOF
}

fail() {
    printf 'PUBLIC_RECOVERY_STAGE_FAIL:%s\n' "$1" >&2
    exit 1
}

cleanup() {
    local status="$?"
    trap - EXIT HUP INT TERM
    # On failure retain every newly created path for explicit inspection.  The
    # bridge never recursively deletes a worktree, staging directory, or guest
    # target behind the operator's back.
    exit "$status"
}

trap cleanup EXIT
trap 'exit 1' HUP INT TERM

safe_leaf() {
    local value="$1"
    [[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,119}$ ]] && [ "$value" != "." ] && [ "$value" != ".." ]
}

safe_token() {
    local value="$1"
    [[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]]
}

canonical_directory() {
    local input="$1" resolved
    case "$input" in /*) ;; *) return 1 ;; esac
    [ -d "$input" ] && [ ! -L "$input" ] || return 1
    resolved="$(CDPATH= cd -- "$input" && pwd -P)" || return 1
    [ -d "$resolved" ] && [ ! -L "$resolved" ] || return 1
    printf '%s\n' "$resolved"
}

canonical_existing_regular_file() {
    local input="$1" parent leaf resolved_parent resolved
    case "$input" in /*) ;; *) return 1 ;; esac
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
    case "$input" in /*) ;; *) return 1 ;; esac
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
    case "$child" in "$parent"|"$parent"/*) return 0 ;; *) return 1 ;; esac
}

require_existing_outside_source() {
    local input="$1" label="$2" resolved
    resolved="$(canonical_existing_regular_file "$input")" || fail "$label-must-be-absolute-regular-non-symlink-file"
    path_is_below "$resolved" "$ROOT" && fail "$label-must-be-outside-source-worktree"
    printf '%s\n' "$resolved"
}

require_new_outside_source() {
    local input="$1" label="$2" resolved
    resolved="$(canonical_new_path "$input")" || fail "$label-must-be-fresh-absolute-non-symlink-path"
    path_is_below "$resolved" "$ROOT" && fail "$label-must-be-outside-source-worktree"
    printf '%s\n' "$resolved"
}

parse_gate_build_dir() {
    local value="$1" token
    case "$value" in "$GATE_DIR_PREFIX"*) token="${value#"$GATE_DIR_PREFIX"}" ;; *) return 1 ;; esac
    safe_token "$token" || return 1
    [ "$value" = "$GATE_DIR_PREFIX$token" ] || return 1
    GATE_BUILD_TOKEN="$token"
    GATE_BUILD_DIR="$GATE_DIR_PREFIX$token"
}

parse_guest_dir() {
    local value="$1" token
    case "$value" in "$GUEST_DIR_PREFIX"*) token="${value#"$GUEST_DIR_PREFIX"}" ;; *) return 1 ;; esac
    safe_token "$token" || return 1
    [ "$value" = "$GUEST_DIR_PREFIX$token" ] || return 1
    GUEST_TOKEN="$token"
    GUEST_DIR="$GUEST_DIR_PREFIX$token"
}

require_clean_committed_source() {
    local head
    # User untracked files are deliberately outside this bridge's identity
    # boundary.  The detached worktree at HEAD is the clean source view; only
    # tracked or staged modifications can change that view's provenance.
    /usr/bin/git -C "$ROOT" diff --quiet || fail "source-worktree-has-unstaged-changes"
    /usr/bin/git -C "$ROOT" diff --cached --quiet || fail "source-worktree-has-staged-changes"
    head="$(/usr/bin/git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || true)"
    [[ "$head" =~ ^[0-9a-f]{40}$ ]] || fail "source-head-is-not-a-full-commit"
    SOURCE_HEAD="$head"
}

prepare_guest_transport() {
    KNOWN_HOSTS="$(/usr/bin/mktemp /tmp/aiam-iwn-public-recovery-known-hosts.XXXXXX)" || fail "known-hosts-create-failed"
    /bin/chmod 600 "$KNOWN_HOSTS"
    printf '%s\n' "$PINNED_GUEST_HOSTKEY_LINE" > "$KNOWN_HOSTS"
    local observed
    observed="$(/usr/bin/ssh-keygen -lf "$KNOWN_HOSTS" -E sha256 2>/dev/null | /usr/bin/awk 'NR == 1 { print $2; exit }')"
    [ "$observed" = "$PINNED_GUEST_HOSTKEY_SHA256" ] || fail "pinned-guest-host-key-fingerprint-mismatch"
    SSH=(/usr/bin/ssh -F /dev/null -T -o BatchMode=yes -o ConnectTimeout=8
        -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$KNOWN_HOSTS"
        -o GlobalKnownHostsFile=/dev/null -o UpdateHostKeys=no -o LogLevel=ERROR
        -p "$PINNED_PORT" "$PINNED_GUEST")
    SCP=(/usr/bin/scp -F /dev/null -o BatchMode=yes -o ConnectTimeout=8
        -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$KNOWN_HOSTS"
        -o GlobalKnownHostsFile=/dev/null -o UpdateHostKeys=no -o LogLevel=ERROR
        -P "$PINNED_PORT")
}

assert_pinned_guest_build() {
    local observed
    observed="$("${SSH[@]}" /usr/bin/sw_vers -buildVersion 2>/dev/null || true)"
    [ "$observed" = "$PINNED_GUEST_BUILD" ] || fail "pinned-guest-build-mismatch"
}

assert_remote_gate_helper() {
    "${SSH[@]}" /bin/sh -s -- "$GATE_BUILD_DIR" >/dev/null 2>&1 <<'SH' || fail "remote-gate-helper-is-not-safe"
set -eu
gate="$1"
prefix="/tmp/aiam-tahoe-sae-layer-gate."
case "$gate" in "$prefix"*) token="${gate#"$prefix"}" ;; *) exit 1 ;; esac
case "$token" in ""|*[!A-Za-z0-9._-]*|[!A-Za-z0-9]*) exit 1 ;; esac
[ "$gate" = "$prefix$token" ] || exit 1
for path in "$gate" "$gate/Build" "$gate/Build/Debug" "$gate/Build/Debug/Tahoe"; do
  test -d "$path" && test ! -L "$path"
done
helper="$gate/Build/Debug/Tahoe/airport_itlwm_lab_public_recovery"
test -f "$helper" && test ! -L "$helper" && test -x "$helper"
SH
}

copy_remote_gate_helper() {
    local output="$1"
    "${SCP[@]}" "$PINNED_GUEST:$GATE_BUILD_DIR/$HELPER_REPO_PATH" "$output" >/dev/null 2>&1 || fail "remote-gate-helper-copy-failed"
    [ -f "$output" ] && [ ! -L "$output" ] && [ -s "$output" ] || fail "remote-gate-helper-copy-produced-no-regular-file"
    /bin/chmod 700 "$output"
}

make_fresh_detached_worktree() {
    /usr/bin/git -C "$ROOT" worktree add --detach "$WORKTREE" "$SOURCE_HEAD" >/dev/null 2>&1 || fail "detached-worktree-create-failed"
    [ "$(/usr/bin/git -C "$WORKTREE" rev-parse HEAD 2>/dev/null || true)" = "$SOURCE_HEAD" ] || fail "detached-worktree-head-mismatch"
    [ -z "$(/usr/bin/git -C "$WORKTREE" status --porcelain=v1 --untracked-files=all)" ] || fail "detached-worktree-is-not-clean"
}

copy_helper_into_worktree() {
    local source="$1" destination="$WORKTREE/$HELPER_REPO_PATH"
    /bin/mkdir -p "$(dirname -- "$destination")"
    [ ! -L "$WORKTREE/Build" ] && [ ! -L "$WORKTREE/Build/Debug" ] && [ ! -L "$WORKTREE/Build/Debug/Tahoe" ] || fail "worktree-helper-parent-is-symlink"
    /bin/cp -pP "$source" "$destination"
    [ -f "$destination" ] && [ ! -L "$destination" ] && [ -x "$destination" ] || fail "worktree-helper-copy-failed"
}

capture_public_recovery_receipt() {
    local exclude_file="$COLLECT_STAGING/worktree-build.exclude"
    printf 'Build/\n' > "$exclude_file"
    /bin/chmod 600 "$exclude_file"
    GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_COUNT=1 \
    GIT_CONFIG_KEY_0=core.excludesFile GIT_CONFIG_VALUE_0="$exclude_file" LC_ALL=C \
        /usr/bin/python3 "$WORKTREE/scripts/$RECEIPT_TOOL_NAME" \
            --candidate-receipt "$CANDIDATE_RECEIPT" \
            --helper "$WORKTREE/$HELPER_REPO_PATH" \
            --gate-token "$GATE_BUILD_TOKEN" \
            --output "$COLLECT_STAGING/$PUBLIC_RECEIPT_NAME" >/dev/null || fail "public-recovery-receipt-capture-failed"
    [ -f "$COLLECT_STAGING/$PUBLIC_RECEIPT_NAME" ] && [ ! -L "$COLLECT_STAGING/$PUBLIC_RECEIPT_NAME" ] || fail "public-recovery-receipt-output-missing"
}

write_collection_report() {
    local output="$1" helper_path="$2" receipt_path="$3"
    /usr/bin/python3 - "$ROOT/scripts" "$CANDIDATE_RECEIPT" "$helper_path" "$receipt_path" "$output" <<'PY'
import hashlib
import json
import os
import stat
import sys
from pathlib import Path

scripts = Path(sys.argv[1])
candidate_path = Path(sys.argv[2])
helper_path = Path(sys.argv[3])
receipt_path = Path(sys.argv[4])
output = Path(sys.argv[5])
sys.path.insert(0, str(scripts))
from capture_tahoe_iwn_lab_public_recovery_receipt import (
    macho_uuid,
    parse_json_document,
    read_typed_candidate_receipt,
    require_regular_file,
    require_regular_executable,
    validate_public_recovery_receipt_document,
)

candidate, candidate_sha256 = read_typed_candidate_receipt(candidate_path)
receipt = require_regular_file(receipt_path, "collection public recovery receipt")
receipt_bytes = receipt.read_bytes()
binding = validate_public_recovery_receipt_document(
    parse_json_document(receipt_bytes, "collection public recovery receipt")
)
if receipt_bytes != require_regular_file(receipt_path, "collection public recovery receipt").read_bytes():
    raise SystemExit(1)
helper = require_regular_executable(helper_path, "collection helper")
helper_bytes = helper.read_bytes()
if helper_bytes != require_regular_executable(helper_path, "collection helper").read_bytes():
    raise SystemExit(1)
helper_sha256 = hashlib.sha256(helper_bytes).hexdigest()
if binding["candidate_receipt_sha256"] != candidate_sha256:
    raise SystemExit(1)
if binding["candidate_profile"] != candidate.get("profile"):
    raise SystemExit(1)
if binding["helper_sha256"] != helper_sha256:
    raise SystemExit(1)
if binding["helper_macho_uuid"] != macho_uuid(helper_bytes):
    raise SystemExit(1)
document = {
    "schema": "itlwm-tahoe-iwn-public-recovery-collection/v1",
    "candidate_receipt_sha256": candidate_sha256,
    "public_recovery_receipt_sha256": hashlib.sha256(receipt_bytes).hexdigest(),
    "gate_build_dir_token": binding["gate_build_dir_token"],
    "source": {
        "commit": binding["source_commit"],
        "identity_sha256": binding["source_identity_sha256"],
        "identity_paths_count": binding["source_identity_paths_count"],
        "helper_source_identity_sha256": binding["helper_source_identity_sha256"],
        "helper_source_paths_count": binding["helper_source_paths_count"],
    },
    "helper": {"sha256": helper_sha256, "macho_uuid": binding["helper_macho_uuid"]},
    "validation": {
        "typed_v2_candidate_receipt_matches_sidecar_receipt": True,
        "existing_safe_token_gate_consumed": True,
        "helper_hash_matches_sidecar_receipt": True,
        "helper_macho_uuid_matches_sidecar_receipt": True,
        "local_detached_worktree_materialized": True,
    },
    "non_claims": {
        "compiler_invoked_by_bridge": False,
        "helper_staged_to_guest": False,
        "helper_invoked": False,
        "target_identity_collected": False,
        "credential_collected": False,
        "remote_output_collected": False,
        "runtime_experiment_performed": False,
    },
}
payload = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
try:
    offset = 0
    while offset < len(payload):
        written = os.write(fd, payload[offset:])
        if written <= 0:
            raise OSError("write failed")
        offset += written
    os.fsync(fd)
finally:
    os.close(fd)
if stat.S_IMODE(output.stat().st_mode) != 0o600:
    raise SystemExit(1)
PY
}

collect_public_recovery() {
    require_clean_committed_source
    CANDIDATE_RECEIPT="$(require_existing_outside_source "$CANDIDATE_RECEIPT" "candidate-receipt")"
    WORKTREE="$(require_new_outside_source "$WORKTREE" "worktree")"
    ARTIFACTS_DIR="$(require_new_outside_source "$ARTIFACTS_DIR" "artifacts-dir")"
    path_is_below "$ARTIFACTS_DIR" "$WORKTREE" && fail "artifacts-dir-must-not-be-inside-worktree"
    path_is_below "$WORKTREE" "$ARTIFACTS_DIR" && fail "worktree-must-not-be-inside-artifacts-dir"
    parse_gate_build_dir "$GATE_BUILD_DIR" || fail "gate-build-dir-is-not-a-safe-full-gate-token"
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%s\n' 'COLLECT_DRY_RUN_READY'
        return 0
    fi
    prepare_guest_transport
    assert_pinned_guest_build
    assert_remote_gate_helper
    make_fresh_detached_worktree
    local artifacts_parent
    artifacts_parent="$(dirname -- "$ARTIFACTS_DIR")"
    COLLECT_STAGING="$(/usr/bin/mktemp -d "$artifacts_parent/.aiam-iwn-public-recovery-collect.XXXXXX")" || fail "collection-staging-create-failed"
    /bin/chmod 700 "$COLLECT_STAGING"
    copy_remote_gate_helper "$COLLECT_STAGING/$HELPER_NAME"
    copy_helper_into_worktree "$COLLECT_STAGING/$HELPER_NAME"
    capture_public_recovery_receipt
    write_collection_report "$COLLECT_STAGING/$COLLECTION_REPORT_NAME" "$COLLECT_STAGING/$HELPER_NAME" "$COLLECT_STAGING/$PUBLIC_RECEIPT_NAME" || fail "collection-report-write-failed"
    /bin/mv -- "$COLLECT_STAGING" "$ARTIFACTS_DIR"
    COLLECT_STAGING=""
    printf '%s\n' 'COLLECT_READY'
}

validate_stage_inputs() {
    /usr/bin/python3 - "$ROOT/scripts" "$ROOT" "$CANDIDATE_RECEIPT" "$PUBLIC_RECEIPT" "$HELPER" "$SOURCE_HEAD" <<'PY'
import hashlib
import sys
from pathlib import Path

scripts = Path(sys.argv[1])
root = Path(sys.argv[2])
candidate_path = Path(sys.argv[3])
public_receipt_path = Path(sys.argv[4])
helper_path = Path(sys.argv[5])
source_head = sys.argv[6]
sys.path.insert(0, str(scripts))
from capture_tahoe_iwn_lab_public_recovery_receipt import (
    current_source_identity,
    helper_source_identity,
    macho_uuid,
    parse_json_document,
    read_typed_candidate_receipt,
    require_regular_file,
    require_regular_executable,
    validate_public_recovery_receipt_document,
)

import subprocess
head = subprocess.run(
    ["git", "-C", str(root), "rev-parse", "--verify", "HEAD"],
    check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
).stdout.strip().lower()
if head != source_head:
    raise SystemExit(1)
source = current_source_identity(root, head)
helper_source = helper_source_identity(root)
candidate, candidate_sha256 = read_typed_candidate_receipt(candidate_path)
public_receipt = require_regular_file(public_receipt_path, "stage public recovery receipt")
receipt_bytes = public_receipt.read_bytes()
binding = validate_public_recovery_receipt_document(
    parse_json_document(receipt_bytes, "stage public recovery receipt")
)
if receipt_bytes != require_regular_file(public_receipt_path, "stage public recovery receipt").read_bytes():
    raise SystemExit(1)
helper = require_regular_executable(helper_path, "stage helper")
helper_bytes = helper.read_bytes()
if helper_bytes != require_regular_executable(helper_path, "stage helper").read_bytes():
    raise SystemExit(1)
helper_sha256 = hashlib.sha256(helper_bytes).hexdigest()
helper_uuid = macho_uuid(helper_bytes)
if (candidate.get("source_commit") != source["commit"] or
        candidate.get("source_identity_sha256") != source["identity_sha256"] or
        candidate.get("source_identity_paths_count") != source["paths_count"]):
    raise SystemExit(1)
if binding["candidate_receipt_sha256"] != candidate_sha256:
    raise SystemExit(1)
if binding["candidate_profile"] != candidate.get("profile"):
    raise SystemExit(1)
if (binding["source_commit"] != source["commit"] or
        binding["source_identity_sha256"] != source["identity_sha256"] or
        binding["source_identity_paths_count"] != source["paths_count"]):
    raise SystemExit(1)
if (binding["helper_source_identity_sha256"] != helper_source["sha256"] or
        binding["helper_source_paths_count"] != helper_source["paths_count"]):
    raise SystemExit(1)
if binding["helper_sha256"] != helper_sha256 or binding["helper_macho_uuid"] != helper_uuid:
    raise SystemExit(1)
receipt_sha256 = hashlib.sha256(receipt_bytes).hexdigest()
print("|".join((
    source["commit"], source["identity_sha256"], str(source["paths_count"]),
    candidate_sha256, receipt_sha256, helper_sha256, helper_uuid,
    binding["gate_build_dir_token"],
)))
PY
}

stage_fields_shape_valid() {
    local values="$1" field
    local -a fields=()
    IFS='|' read -r -a fields <<<"$values"
    [ "${#fields[@]}" -eq 8 ] || return 1
    for field in "${fields[@]}"; do [ -n "$field" ] || return 1; done
}

create_remote_stage_directory() {
    "${SSH[@]}" /bin/sh -s -- "$GUEST_DIR" >/dev/null 2>&1 <<'SH' || fail "remote-stage-directory-create-failed"
set -eu
stage="$1"
prefix="/private/tmp/aiam-iwn-public-recovery-"
case "$stage" in "$prefix"*) token="${stage#"$prefix"}" ;; *) exit 1 ;; esac
case "$token" in ""|*[!A-Za-z0-9._-]*|[!A-Za-z0-9]*) exit 1 ;; esac
[ "$stage" = "$prefix$token" ] || exit 1
for path in /private /private/tmp; do
  test -d "$path" && test ! -L "$path"
done
test ! -e "$stage" && test ! -L "$stage"
umask 077
/bin/mkdir -m 700 "$stage"
test -d "$stage" && test ! -L "$stage"
test "$(/usr/bin/stat -f '%Lp' "$stage")" = 700
SH
}

copy_stage_artifacts() {
    "${SCP[@]}" -- "$HELPER" "$PINNED_GUEST:$GUEST_DIR/$HELPER_NAME" >/dev/null 2>&1 || fail "remote-stage-helper-copy-failed"
    "${SCP[@]}" -- "$PUBLIC_RECEIPT" "$PINNED_GUEST:$GUEST_DIR/$PUBLIC_RECEIPT_NAME" >/dev/null 2>&1 || fail "remote-stage-receipt-copy-failed"
}

verify_remote_stage() {
    local helper_sha256="$1" helper_uuid="$2" receipt_sha256="$3" observed
    observed="$("${SSH[@]}" /usr/bin/python3 - "$GUEST_DIR" "$helper_sha256" "$helper_uuid" "$receipt_sha256" 2>/dev/null <<'PY'
import hashlib
import os
import re
import stat
import struct
import sys
import uuid

stage, expected_helper_sha256, expected_uuid, expected_receipt_sha256 = sys.argv[1:]
prefix = "/private/tmp/aiam-iwn-public-recovery-"
if not stage.startswith(prefix):
    raise SystemExit(1)
token = stage[len(prefix):]
if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", token) is None:
    raise SystemExit(1)
for parent in ("/private", "/private/tmp", stage):
    metadata = os.lstat(parent)
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
        raise SystemExit(1)
if stat.S_IMODE(os.lstat(stage).st_mode) != 0o700:
    raise SystemExit(1)
helper = os.path.join(stage, "airport_itlwm_lab_public_recovery")
receipt = os.path.join(stage, "iwn-public-recovery-receipt-v1.json")
if set(os.listdir(stage)) != {os.path.basename(helper), os.path.basename(receipt)}:
    raise SystemExit(1)

def regular(path, executable=False):
    metadata = os.lstat(path)
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise SystemExit(1)
    if executable and metadata.st_mode & 0o111 == 0:
        raise SystemExit(1)
    with open(path, "rb") as source:
        return source.read()

helper_bytes = regular(helper, executable=True)
receipt_bytes = regular(receipt)
os.chmod(helper, 0o700)
os.chmod(receipt, 0o600)
if stat.S_IMODE(os.lstat(helper).st_mode) != 0o700 or stat.S_IMODE(os.lstat(receipt).st_mode) != 0o600:
    raise SystemExit(1)
if helper_bytes != regular(helper, executable=True) or receipt_bytes != regular(receipt):
    raise SystemExit(1)
if len(helper_bytes) < 32:
    raise SystemExit(1)
magic, cpu_type, _subtype, file_type, count, command_bytes, _flags, _reserved = struct.unpack_from("<IiiIIIII", helper_bytes, 0)
if magic != 0xFEEDFACF or cpu_type not in (0x01000007, 0x0100000C) or file_type != 2:
    raise SystemExit(1)
cursor = 32
end = cursor + command_bytes
if end > len(helper_bytes):
    raise SystemExit(1)
found = None
for _ in range(count):
    if cursor + 8 > end:
        raise SystemExit(1)
    command, size = struct.unpack_from("<II", helper_bytes, cursor)
    if size < 8 or cursor + size > end:
        raise SystemExit(1)
    if command == 0x1B:
        if size < 24 or found is not None:
            raise SystemExit(1)
        found = str(uuid.UUID(bytes=helper_bytes[cursor + 8:cursor + 24])).upper()
    cursor += size
if found is None:
    raise SystemExit(1)
helper_sha256 = hashlib.sha256(helper_bytes).hexdigest()
receipt_sha256 = hashlib.sha256(receipt_bytes).hexdigest()
if helper_sha256 != expected_helper_sha256 or found != expected_uuid or receipt_sha256 != expected_receipt_sha256:
    raise SystemExit(1)
print("REMOTE_PUBLIC_RECOVERY_STAGE helper_sha256=%s helper_macho_uuid=%s receipt_sha256=%s" % (helper_sha256, found, receipt_sha256))
PY
)" || fail "remote-stage-rehash-or-macho-verification-failed"
case "$observed" in
    "REMOTE_PUBLIC_RECOVERY_STAGE helper_sha256=$helper_sha256 helper_macho_uuid=$helper_uuid receipt_sha256=$receipt_sha256") ;;
    *) fail "remote-stage-verification-output-malformed" ;;
esac
}

validate_stage_report() {
    local report="$1"
    /usr/bin/python3 - "$report" <<'PY'
import json
import re
import stat
import sys
from pathlib import Path

path = Path(sys.argv[1])
try:
    metadata = path.lstat()
except OSError:
    raise SystemExit(1)
if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
    raise SystemExit(1)
if stat.S_IMODE(metadata.st_mode) != 0o600:
    raise SystemExit(1)

def reject_duplicate_object_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result

def reject_nonfinite_json_constant(value):
    raise ValueError("non-finite JSON constant")

try:
    document = json.loads(
        path.read_text(encoding="utf-8"),
        object_pairs_hook=reject_duplicate_object_keys,
        parse_constant=reject_nonfinite_json_constant,
    )
except (OSError, UnicodeDecodeError, ValueError, json.JSONDecodeError):
    raise SystemExit(1)
if not isinstance(document, dict) or set(document) != {
        "schema", "candidate_receipt_sha256", "public_recovery_receipt_sha256",
        "gate_build_dir_token", "guest_dir_token", "source", "helper",
        "validation", "non_claims"}:
    raise SystemExit(1)
if document["schema"] != "itlwm-tahoe-iwn-public-recovery-stage-attestation/v1":
    raise SystemExit(1)
hex64 = re.compile(r"[0-9a-f]{64}")
if any(not isinstance(document[key], str) or hex64.fullmatch(document[key]) is None
       for key in ("candidate_receipt_sha256", "public_recovery_receipt_sha256")):
    raise SystemExit(1)
for key in ("gate_build_dir_token", "guest_dir_token"):
    if not isinstance(document[key], str) or re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", document[key]) is None:
        raise SystemExit(1)
source = document["source"]
if not isinstance(source, dict) or set(source) != {"commit", "identity_sha256", "identity_paths_count"}:
    raise SystemExit(1)
if not isinstance(source["commit"], str) or re.fullmatch(r"[0-9a-f]{40}", source["commit"]) is None:
    raise SystemExit(1)
if not isinstance(source["identity_sha256"], str) or hex64.fullmatch(source["identity_sha256"]) is None:
    raise SystemExit(1)
if type(source["identity_paths_count"]) is not int or source["identity_paths_count"] < 1:
    raise SystemExit(1)
helper = document["helper"]
if not isinstance(helper, dict) or set(helper) != {"sha256", "macho_uuid"}:
    raise SystemExit(1)
if not isinstance(helper["sha256"], str) or hex64.fullmatch(helper["sha256"]) is None:
    raise SystemExit(1)
if not isinstance(helper["macho_uuid"], str) or re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", helper["macho_uuid"]) is None:
    raise SystemExit(1)
validation = document["validation"]
required_validation = {
    "pinned_guest_host_key", "pinned_guest_build", "fresh_restricted_guest_directory",
    "helper_hash_matches_local_sidecar_receipt", "helper_macho_uuid_matches_local_sidecar_receipt",
    "guest_rehash_matches_local_bytes", "guest_macho_uuid_matches_local_bytes",
}
if not isinstance(validation, dict) or set(validation) != required_validation or any(value is not True for value in validation.values()):
    raise SystemExit(1)
non_claims = document["non_claims"]
required_non_claims = {
    "helper_invoked", "target_identity_collected", "credential_collected",
    "remote_output_collected", "association_tested", "rebooted",
    "runtime_experiment_performed",
}
if not isinstance(non_claims, dict) or set(non_claims) != required_non_claims or any(value is not False for value in non_claims.values()):
    raise SystemExit(1)
print("STAGE_REPORT_VALID")
PY
}

write_stage_report() {
    local values="$1"
    /usr/bin/python3 - "$STAGE_REPORT" "$GUEST_TOKEN" "$values" <<'PY'
import json
import os
import re
import stat
import sys
from pathlib import Path

output = Path(sys.argv[1])
guest_token = sys.argv[2]
fields = sys.argv[3].split("|")
if len(fields) != 8:
    raise SystemExit(1)
(source_commit, source_identity_sha256, source_paths_count, candidate_receipt_sha256,
 public_receipt_sha256, helper_sha256, helper_macho_uuid, gate_token) = fields
if any(re.fullmatch(r"[0-9a-f]{64}", value) is None for value in (
        source_identity_sha256, candidate_receipt_sha256, public_receipt_sha256, helper_sha256)):
    raise SystemExit(1)
if re.fullmatch(r"[0-9a-f]{40}", source_commit) is None:
    raise SystemExit(1)
if not source_paths_count.isdecimal() or int(source_paths_count) < 1:
    raise SystemExit(1)
if re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", helper_macho_uuid) is None:
    raise SystemExit(1)
if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", gate_token) is None:
    raise SystemExit(1)
if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", guest_token) is None:
    raise SystemExit(1)
document = {
    "schema": "itlwm-tahoe-iwn-public-recovery-stage-attestation/v1",
    "candidate_receipt_sha256": candidate_receipt_sha256,
    "public_recovery_receipt_sha256": public_receipt_sha256,
    "gate_build_dir_token": gate_token,
    "guest_dir_token": guest_token,
    "source": {
        "commit": source_commit,
        "identity_sha256": source_identity_sha256,
        "identity_paths_count": int(source_paths_count),
    },
    "helper": {"sha256": helper_sha256, "macho_uuid": helper_macho_uuid},
    "validation": {
        "pinned_guest_host_key": True,
        "pinned_guest_build": True,
        "fresh_restricted_guest_directory": True,
        "helper_hash_matches_local_sidecar_receipt": True,
        "helper_macho_uuid_matches_local_sidecar_receipt": True,
        "guest_rehash_matches_local_bytes": True,
        "guest_macho_uuid_matches_local_bytes": True,
    },
    "non_claims": {
        "helper_invoked": False,
        "target_identity_collected": False,
        "credential_collected": False,
        "remote_output_collected": False,
        "association_tested": False,
        "rebooted": False,
        "runtime_experiment_performed": False,
    },
}
payload = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
try:
    offset = 0
    while offset < len(payload):
        written = os.write(fd, payload[offset:])
        if written <= 0:
            raise OSError("write failed")
        offset += written
    os.fsync(fd)
finally:
    os.close(fd)
if stat.S_IMODE(output.stat().st_mode) != 0o600:
    raise SystemExit(1)
PY
    validate_stage_report "$STAGE_REPORT" >/dev/null || exit 1
}

stage_public_recovery() {
    require_clean_committed_source
    local source_head_before="$SOURCE_HEAD"
    CANDIDATE_RECEIPT="$(require_existing_outside_source "$CANDIDATE_RECEIPT" "candidate-receipt")"
    PUBLIC_RECEIPT="$(require_existing_outside_source "$PUBLIC_RECEIPT" "public-recovery-receipt")"
    HELPER="$(require_existing_outside_source "$HELPER" "helper")"
    STAGE_REPORT="$(require_new_outside_source "$STAGE_REPORT" "stage-report")"
    parse_guest_dir "$GUEST_DIR" || fail "guest-dir-is-not-a-safe-full-token-path"
    local values
    values="$(validate_stage_inputs 2>/dev/null || true)"
    stage_fields_shape_valid "$values" || fail "local-sidecar-input-verification-failed"
    require_clean_committed_source
    [ "$SOURCE_HEAD" = "$source_head_before" ] || fail "source-head-changed-during-local-verification"
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%s\n' 'STAGE_DRY_RUN_READY'
        return 0
    fi
    local -a fields=()
    IFS='|' read -r -a fields <<<"$values"
    prepare_guest_transport
    assert_pinned_guest_build
    create_remote_stage_directory
    copy_stage_artifacts
    verify_remote_stage "${fields[5]}" "${fields[6]}" "${fields[4]}"
    write_stage_report "$values" || fail "stage-report-write-failed"
    printf '%s\n' 'STAGE_READY'
}

self_test() {
    safe_token "Ab9_-.z" || fail "self-test-safe-token-rejected"
    if safe_token "../unsafe"; then fail "self-test-unsafe-token-accepted"; fi
    GATE_BUILD_DIR="${GATE_DIR_PREFIX}Ab9_-.z"
    parse_gate_build_dir "$GATE_BUILD_DIR" || fail "self-test-gate-token-rejected"
    [ "$GATE_BUILD_TOKEN" = "Ab9_-.z" ] || fail "self-test-gate-token-round-trip"
    if parse_gate_build_dir "/tmp/not-a-gate-token"; then fail "self-test-unsafe-gate-path-accepted"; fi
    GUEST_DIR="${GUEST_DIR_PREFIX}Z9_.-a"
    parse_guest_dir "$GUEST_DIR" || fail "self-test-guest-token-rejected"
    [ "$GUEST_TOKEN" = "Z9_.-a" ] || fail "self-test-guest-token-round-trip"
    if parse_guest_dir "/private/tmp/not-a-public-recovery-token"; then fail "self-test-unsafe-guest-path-accepted"; fi
    stage_fields_shape_valid "a|b|c|d|e|f|g|h" || fail "self-test-stage-fields-rejected"
    if stage_fields_shape_valid "a|b|c"; then fail "self-test-short-stage-fields-accepted"; fi
    printf '%s\n' 'PASS: Tahoe IWN public recovery stage self-test'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --collect|--stage)
            [ -z "$MODE" ] || { usage; exit 2; }
            MODE="${1#--}"
            ;;
        --dry-run) DRY_RUN=1 ;;
        --help|-h) usage; exit 0 ;;
        --self-test) SELF_TEST=1 ;;
        --gate-build-dir|--worktree|--artifacts-dir|--candidate-receipt|--public-recovery-receipt|--helper|--guest-dir|--stage-report|--validate-stage-report)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --gate-build-dir) [ -z "$GATE_BUILD_DIR" ] || { usage; exit 2; }; GATE_BUILD_DIR="$2" ;;
                --worktree) [ -z "$WORKTREE" ] || { usage; exit 2; }; WORKTREE="$2" ;;
                --artifacts-dir) [ -z "$ARTIFACTS_DIR" ] || { usage; exit 2; }; ARTIFACTS_DIR="$2" ;;
                --candidate-receipt) [ -z "$CANDIDATE_RECEIPT" ] || { usage; exit 2; }; CANDIDATE_RECEIPT="$2" ;;
                --public-recovery-receipt) [ -z "$PUBLIC_RECEIPT" ] || { usage; exit 2; }; PUBLIC_RECEIPT="$2" ;;
                --helper) [ -z "$HELPER" ] || { usage; exit 2; }; HELPER="$2" ;;
                --guest-dir) [ -z "$GUEST_DIR" ] || { usage; exit 2; }; GUEST_DIR="$2" ;;
                --stage-report) [ -z "$STAGE_REPORT" ] || { usage; exit 2; }; STAGE_REPORT="$2" ;;
                --validate-stage-report) [ -z "$VALIDATE_STAGE_REPORT" ] || { usage; exit 2; }; VALIDATE_STAGE_REPORT="$2" ;;
            esac
            shift
            ;;
        *) usage; exit 2 ;;
    esac
    shift
done

if [ "$SELF_TEST" -eq 1 ]; then
    [ -z "$MODE$GATE_BUILD_DIR$WORKTREE$ARTIFACTS_DIR$CANDIDATE_RECEIPT$PUBLIC_RECEIPT$HELPER$GUEST_DIR$STAGE_REPORT$VALIDATE_STAGE_REPORT" ] && [ "$DRY_RUN" -eq 0 ] || { usage; exit 2; }
    self_test
    exit 0
fi

if [ -n "$VALIDATE_STAGE_REPORT" ]; then
    [ -z "$MODE$GATE_BUILD_DIR$WORKTREE$ARTIFACTS_DIR$CANDIDATE_RECEIPT$PUBLIC_RECEIPT$HELPER$GUEST_DIR$STAGE_REPORT" ] && [ "$DRY_RUN" -eq 0 ] || { usage; exit 2; }
    VALIDATE_STAGE_REPORT="$(require_existing_outside_source "$VALIDATE_STAGE_REPORT" "stage-report")"
    validate_stage_report "$VALIDATE_STAGE_REPORT"
    exit 0
fi

case "$MODE" in
    collect)
        [ -n "$GATE_BUILD_DIR" ] && [ -n "$CANDIDATE_RECEIPT" ] && [ -n "$WORKTREE" ] && [ -n "$ARTIFACTS_DIR" ] || { usage; exit 2; }
        [ -z "$PUBLIC_RECEIPT$HELPER$GUEST_DIR$STAGE_REPORT" ] || { usage; exit 2; }
        collect_public_recovery
        ;;
    stage)
        [ -n "$CANDIDATE_RECEIPT" ] && [ -n "$PUBLIC_RECEIPT" ] && [ -n "$HELPER" ] && [ -n "$GUEST_DIR" ] && [ -n "$STAGE_REPORT" ] || { usage; exit 2; }
        [ -z "$GATE_BUILD_DIR$WORKTREE$ARTIFACTS_DIR" ] || { usage; exit 2; }
        stage_public_recovery
        ;;
    *) usage; exit 2 ;;
esac
