#!/usr/bin/python3 -Es
"""Strict guest-only activation/reboot bridge for one staged IWN lab candidate.

This bridge is the destructive boundary between a receipt-bound private stage
and the separate read-only loaded-identity capture.  It accepts no endpoint,
guest path, wireless identifier, credential, hash, UUID, or helper path from
the caller.  Every such value is derived from the typed candidate receipt,
private-stage attestation, and source-pinned constants.

A success establishes only READY_FOR_LOADED_IDENTITY: private AuxKC preflight,
transactional next-boot activation, and one observed guest reboot completed.
It never claims that the candidate is loaded, associated, or usable on air.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import shlex
import stat
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Optional, Sequence

SCHEMA_VERSION = "itlwm-tahoe-iwn-candidate-activation-reboot/v1"
STAGE_SCHEMA = "itlwm-tahoe-iwn-lab-private-stage/v1"
LAB_PROFILE = "iwn-software-pmf-lab"

SELF_RELATIVE = "scripts/run_tahoe_iwn_candidate_activation_reboot.py"
PREFLIGHT_RELATIVE = "scripts/tahoe_auxkc_admission_preflight.sh"
ACTIVATION_RELATIVE = "scripts/tahoe_auxkc_activate_release.sh"

# This executable intentionally has no repository-local imports.  The bridge
# is the authorization boundary between a locally collected receipt and a
# guest mutation, so its receipt parser, source-identity calculation, and
# pinned transport facts are self-contained.  A clean committed source guard
# still byte-binds this file and both guest helpers before any SSH connection.
GIT = "/usr/bin/git"
SSH = "/usr/bin/ssh"
SSH_KEYGEN = "/usr/bin/ssh-keygen"
LOCAL_ENV = {
    "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
    "LC_ALL": "C",
    "LANG": "C",
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_OPTIONAL_LOCKS": "0",
}

PINNED_QEMU_GUEST = "devops@127.0.0.1"
PINNED_QEMU_PORT = 3322
PINNED_QEMU_BUILD = "25C56"
PINNED_QEMU_HOST_KEY = (
    "[127.0.0.1]:3322 ssh-ed25519 "
    "AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
)
PINNED_QEMU_HOST_KEY_SHA256 = "SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"

RECEIPT_SCHEMA = "itlwm-tahoe-iwn-lab-candidate-receipt/v2"
RECEIPT_KIND = "local-unpublished-iwn-lab-candidate"
PROFILE_STAGED_KEXT = "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext"
BUNDLE_ID = "com.zxystd.AirportItlwm"

# Keep this v2 algorithm literal and independent from a mutable checkout.
# If tahoe_source_identity.py evolves, a receipt built under its new contract
# fails closed here until this bridge is intentionally updated and rebuilt.
SOURCE_IDENTITY_PATHS_V2 = (
    "AirportItlwm",
    "AirportItlwmIwnDirectSaeLabClient",
    "AirportItlwmPostPltiTrace",
    "include",
    "itl80211",
    "itlwm",
    "itlwm.xcodeproj",
    "scripts/build_post_plti_trace.sh",
    "scripts/build_tahoe_iwn_direct_sae_lab_client.sh",
    "scripts/build_tahoe.sh",
    "scripts/tahoe_source_identity.py",
)
SOURCE_IDENTITY_DOMAIN_V2 = b"tahoe-airportitlwm-source-identity-v2\0"
TRUSTED_SOURCE_RELATIVES = (SELF_RELATIVE, PREFLIGHT_RELATIVE, ACTIVATION_RELATIVE)

MAX_INPUT_BYTES = 2 * 1024 * 1024

CANDIDATE_PREFIX = "/private/tmp/aiam-iwn-lab-candidate-"
TRACE_PREFIX = "/private/tmp/aiam-post-plti-trace-"
# Tahoe clears /private/tmp during boot.  The activation root carries the
# reboot marker and must survive long enough to bind the returned boot session.
ACTIVATION_PREFIX = "/private/var/tmp/aiam-iwn-activation-"
ARCHIVE_NAME = "AirportItlwm-iwn-software-pmf-lab.kext.zip"
RECEIPT_NAME = "iwn-lab-candidate-receipt-v2.json"
MANIFEST_NAME = "iwn-lab-bundle-manifest.json"
TRACE_TOOL_NAME = "airport_itlwm_post_plti_trace"

TOKEN_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
HEX64_RE = re.compile(r"[0-9a-f]{64}")
UUID_RE = re.compile(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}")
SAFE_LEAF_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,119}")

PRECHECK_TIMEOUT = 45
MUTATION_TIMEOUT = 420
DOWN_ATTEMPTS = 20
RETURN_ATTEMPTS = 90
POLL_SECONDS = 2


class BridgeFailure(Exception):
    def __init__(self, phase: str, rollback_verified: Optional[bool] = None):
        super().__init__(phase)
        self.phase = phase
        self.rollback_verified = rollback_verified


def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def reject_nonfinite_json_constant(value: str) -> None:
    raise ValueError("non-finite JSON constant")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def thin_kext_uuid(data: bytes) -> str:
    if len(data) < 32:
        raise ValueError("thin kext Mach-O is truncated")
    magic, cpu_type, _subtype, file_type, count, command_bytes, _flags, _reserved = struct.unpack_from(
        "<IiiIIIII", data, 0)
    if magic != 0xFEEDFACF or cpu_type not in (0x01000007, 0x0100000C) or file_type != 0xB:
        raise ValueError("Mach-O is not a thin kext")
    cursor, end, found = 32, 32 + command_bytes, None
    if end > len(data):
        raise ValueError("thin kext load-command range is invalid")
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("thin kext load command is truncated")
        command, size = struct.unpack_from("<II", data, cursor)
        if size < 8 or cursor + size > end:
            raise ValueError("thin kext load command is invalid")
        if command == 0x1B:
            if size < 24 or found is not None:
                raise ValueError("thin kext UUID command is invalid")
            found = str(uuid.UUID(bytes=data[cursor + 8:cursor + 24])).upper()
        cursor += size
    if found is None:
        raise ValueError("thin kext UUID is missing")
    return found


def require_regular_file(path: Path, label: str) -> Path:
    if not path.is_absolute():
        raise ValueError(label + " is not absolute")
    try:
        metadata = path.lstat()
    except OSError as error:
        raise ValueError(label + " is missing") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise ValueError(label + " is not a regular non-symlink file")
    try:
        parent = path.parent.resolve(strict=True)
    except OSError as error:
        raise ValueError(label + " parent is unavailable") from error
    result = parent / path.name
    try:
        metadata = result.lstat()
    except OSError as error:
        raise ValueError(label + " changed while resolving") from error
    if (stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode) or
            metadata.st_nlink != 1):
        raise ValueError(label + " is not a stable regular file")
    return result


def _stable_metadata(value: os.stat_result) -> tuple[int, int, int, int, int, int, int]:
    return (
        value.st_dev,
        value.st_ino,
        value.st_mode,
        value.st_nlink,
        value.st_size,
        getattr(value, "st_mtime_ns", int(value.st_mtime * 1_000_000_000)),
        getattr(value, "st_ctime_ns", int(value.st_ctime * 1_000_000_000)),
    )


@dataclass(frozen=True)
class JsonSnapshot:
    payload: bytes
    sha256: str
    document: dict[str, object]


def snapshot_private_json(
    path: Path, label: str, *, required_mode: int = 0o600,
    _read_chunk: Any = os.read,
) -> JsonSnapshot:
    """Read exactly one no-follow private JSON object and bind its digest.

    The document and digest must come from the same stable descriptor.  A
    later pathname replacement is intentionally irrelevant: authorization is
    carried only by this immutable in-memory snapshot.
    """
    if not path.is_absolute():
        raise ValueError(label + " is not absolute")
    nofollow = getattr(os, "O_NOFOLLOW", None)
    cloexec = getattr(os, "O_CLOEXEC", None)
    if nofollow is None or cloexec is None:
        raise ValueError(label + " lacks no-follow descriptor support")
    descriptor = os.open(path, os.O_RDONLY | nofollow | cloexec)
    try:
        before = os.fstat(descriptor)
        if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or
                stat.S_IMODE(before.st_mode) != required_mode or before.st_size > MAX_INPUT_BYTES):
            raise ValueError(label + " is not a private stable regular file")
        chunks: list[bytes] = []
        total = 0
        while True:
            chunk = _read_chunk(descriptor, 64 * 1024)
            if not chunk:
                break
            total += len(chunk)
            if total > MAX_INPUT_BYTES:
                raise ValueError(label + " exceeds bounded input size")
            chunks.append(chunk)
        after = os.fstat(descriptor)
    finally:
        os.close(descriptor)
    try:
        pathname = path.lstat()
    except OSError as error:
        raise ValueError(label + " changed while reading") from error
    if (_stable_metadata(before) != _stable_metadata(after) or
            _stable_metadata(before) != _stable_metadata(pathname) or
            stat.S_ISLNK(pathname.st_mode) or not stat.S_ISREG(pathname.st_mode)):
        raise ValueError(label + " changed while reading")
    payload = b"".join(chunks)
    try:
        document = json.loads(
            payload.decode("utf-8"), object_pairs_hook=reject_duplicate_keys,
            parse_constant=reject_nonfinite_json_constant,
        )
    except (UnicodeDecodeError, ValueError, json.JSONDecodeError) as error:
        raise ValueError(label + " is not strict JSON") from error
    if not isinstance(document, dict):
        raise ValueError(label + " JSON is not an object")
    return JsonSnapshot(payload=payload, sha256=sha256_bytes(payload), document=document)


def is_below(child: Path, parent: Path) -> bool:
    try:
        child.relative_to(parent)
    except ValueError:
        return False
    return True


def prepare_output(root: Path, supplied: Path) -> Path:
    if not supplied.is_absolute():
        raise ValueError("output is not absolute")
    if supplied.exists() or supplied.is_symlink():
        raise ValueError("output already exists")
    if SAFE_LEAF_RE.fullmatch(supplied.name) is None:
        raise ValueError("output leaf is unsafe")
    try:
        parent = supplied.parent.resolve(strict=True)
    except OSError as error:
        raise ValueError("output parent is unavailable") from error
    if not parent.is_dir() or parent.is_symlink():
        raise ValueError("output parent is unsafe")
    output = parent / supplied.name
    if output.exists() or output.is_symlink() or is_below(output, root):
        raise ValueError("output is not a fresh external path")
    return output


def write_private_json(path: Path, document: dict[str, object]) -> None:
    payload = (json.dumps(document, sort_keys=True, indent=2) + "\n").encode("utf-8")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o600)
    try:
        offset = 0
        while offset < len(payload):
            written = os.write(descriptor, payload[offset:])
            if written <= 0:
                raise OSError("short report write")
            offset += written
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    metadata = path.lstat()
    if (stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode) or
            metadata.st_nlink != 1 or stat.S_IMODE(metadata.st_mode) != 0o600):
        raise OSError("report post-write verification failed")


def canonical_direct_candidate(document: dict[str, object]) -> dict[str, Any]:
    """Strict self-contained v2 receipt reader used at the mutation boundary."""
    expected_top = {
        "schema", "receipt_kind", "created_at_utc", "candidate", "validation",
        "non_claims", "verdict",
    }
    if set(document) != expected_top or document.get("schema") != RECEIPT_SCHEMA:
        raise ValueError("candidate receipt schema is invalid")
    if document.get("receipt_kind") != RECEIPT_KIND or not isinstance(document.get("created_at_utc"), str):
        raise ValueError("candidate receipt identity is invalid")

    candidate = document.get("candidate")
    candidate_keys = {
        "source_commit", "source_identity_sha256", "source_identity_paths_count", "profile",
        "staged_kext_repo_path", "trace_client_sha256", "archive_sha256", "info_plist_sha256",
        "binary_sha256", "macho_uuid", "bundle_tree_sha256", "bundle_id", "bundle_version",
        "short_version",
    }
    if not isinstance(candidate, dict) or set(candidate) != candidate_keys:
        raise ValueError("candidate receipt candidate is invalid")
    if (not isinstance(candidate.get("source_commit"), str) or
            re.fullmatch(r"[0-9a-f]{40}", candidate["source_commit"]) is None):
        raise ValueError("candidate receipt source commit is invalid")
    for key in (
        "source_identity_sha256", "trace_client_sha256", "archive_sha256", "info_plist_sha256",
        "binary_sha256", "bundle_tree_sha256",
    ):
        if not isinstance(candidate.get(key), str) or HEX64_RE.fullmatch(candidate[key]) is None:
            raise ValueError("candidate receipt digest is invalid")
    count = candidate.get("source_identity_paths_count")
    if type(count) is not int or count < 1:
        raise ValueError("candidate receipt source path count is invalid")
    if (candidate.get("profile") != LAB_PROFILE or
            candidate.get("staged_kext_repo_path") != PROFILE_STAGED_KEXT or
            candidate.get("bundle_id") != BUNDLE_ID):
        raise ValueError("candidate receipt profile is invalid")
    if (not isinstance(candidate.get("macho_uuid"), str) or
            UUID_RE.fullmatch(candidate["macho_uuid"]) is None):
        raise ValueError("candidate receipt Mach-O UUID is invalid")
    for key in ("bundle_version", "short_version"):
        if not isinstance(candidate.get(key), str):
            raise ValueError("candidate receipt bundle version is invalid")

    required_validation = {
        "source_tree_clean_before_capture", "source_tree_clean_after_capture",
        "source_head_stable_during_capture", "staged_kext_path_matches_profile",
        "archive_validated", "staged_bundle_validated", "archive_and_staged_bundle_match",
        "artifact_stable_during_capture", "trace_client_regular_executable",
        "trace_client_stable_during_capture",
    }
    validation = document.get("validation")
    if (not isinstance(validation, dict) or set(validation) != required_validation or
            any(value is not True for value in validation.values())):
        raise ValueError("candidate receipt validation is invalid")
    required_non_claims = {
        "release_tag_claimed", "compiler_invocation_inspected", "candidate_kext_installed",
        "candidate_kext_loaded", "auxkc_admission", "guest_rebooted", "association_tested",
        "authentication_tested", "dhcp_tested", "data_transfer_tested",
    }
    non_claims = document.get("non_claims")
    if (not isinstance(non_claims, dict) or set(non_claims) != required_non_claims or
            any(value is not False for value in non_claims.values())):
        raise ValueError("candidate receipt non-claims are invalid")
    verdict = document.get("verdict")
    if verdict != {
        "local_candidate_identity_captured": True,
        "suitable_for_later_identity_verification": True,
        "runtime_experiment_performed": False,
    }:
        raise ValueError("candidate receipt verdict is invalid")
    return {key: candidate[key] for key in candidate_keys}


def host_key_fingerprint() -> str:
    fields = PINNED_QEMU_HOST_KEY.split()
    if len(fields) != 3:
        raise ValueError("pinned host key is malformed")
    encoded = fields[2]
    key_blob = base64.b64decode(encoded + "=" * (-len(encoded) % 4), validate=True)
    digest = base64.b64encode(hashlib.sha256(key_blob).digest()).decode("ascii")
    return "SHA256:" + digest.rstrip("=")


def token_from_path(value: object, prefix: str) -> str:
    if not isinstance(value, str) or not value.startswith(prefix):
        raise ValueError("guest path prefix is invalid")
    token = value[len(prefix):]
    if TOKEN_RE.fullmatch(token) is None or value != prefix + token:
        raise ValueError("guest path token is invalid")
    return token


@dataclass(frozen=True)
class StageBinding:
    token: str
    candidate_dir: str
    extracted_kext: str
    trace_tool: str


def validate_stage_report(
    document: dict[str, object],
    receipt_candidate: dict[str, Any],
    canonical_candidate: dict[str, Any],
    receipt_digest: str,
) -> StageBinding:
    expected_top = {
        "schema", "candidate_receipt_sha256", "candidate", "guest_stage",
        "validation", "artifact_digests", "non_claims",
    }
    if set(document) != expected_top or document.get("schema") != STAGE_SCHEMA:
        raise ValueError("private stage schema is invalid")
    if document.get("candidate_receipt_sha256") != receipt_digest:
        raise ValueError("private stage receipt binding is invalid")
    if document.get("candidate") != receipt_candidate:
        raise ValueError("private stage candidate binding is invalid")

    guest = document.get("guest_stage")
    expected_guest_keys = {
        "candidate_dir", "archive_path", "receipt_path", "manifest_path",
        "extracted_kext_path", "trace_tool_path",
    }
    if not isinstance(guest, dict) or set(guest) != expected_guest_keys:
        raise ValueError("private stage guest paths are invalid")
    candidate_dir = guest.get("candidate_dir")
    token = token_from_path(candidate_dir, CANDIDATE_PREFIX)
    if not isinstance(candidate_dir, str):
        raise ValueError("private stage candidate path is invalid")
    expected_paths = {
        "archive_path": candidate_dir + "/" + ARCHIVE_NAME,
        "receipt_path": candidate_dir + "/" + RECEIPT_NAME,
        "manifest_path": candidate_dir + "/" + MANIFEST_NAME,
        "extracted_kext_path": candidate_dir + "/extracted/AirportItlwm.kext",
        "trace_tool_path": TRACE_PREFIX + token + "/" + TRACE_TOOL_NAME,
    }
    for key, expected in expected_paths.items():
        if guest.get(key) != expected:
            raise ValueError("private stage path reconstruction failed")

    validation = document.get("validation")
    expected_validation = {
        "source_clean_committed", "receipt_v2_bound_to_clean_head",
        "local_archive_rehashed", "local_trace_rehashed", "pinned_guest_host_key",
        "remote_archive_rehashed", "remote_trace_rehashed",
        "remote_extracted_candidate_rehashed",
    }
    if (not isinstance(validation, dict) or set(validation) != expected_validation or
            any(value is not True for value in validation.values())):
        raise ValueError("private stage validation facts are invalid")

    artifacts = document.get("artifact_digests")
    if (not isinstance(artifacts, dict) or
            set(artifacts) != {"archive_sha256", "trace_client_sha256"} or
            artifacts.get("archive_sha256") != canonical_candidate.get("archive_sha256") or
            artifacts.get("trace_client_sha256") != canonical_candidate.get("trace_client_sha256")):
        raise ValueError("private stage artifact binding is invalid")

    non_claims = document.get("non_claims")
    expected_non_claims = {
        "candidate_kext_installed", "candidate_kext_loaded", "auxkc_mutated",
        "guest_rebooted", "runtime_experiment_performed",
    }
    if (not isinstance(non_claims, dict) or set(non_claims) != expected_non_claims or
            any(value is not False for value in non_claims.values())):
        raise ValueError("private stage non-claims are invalid")

    return StageBinding(
        token=token,
        candidate_dir=candidate_dir,
        extracted_kext=expected_paths["extracted_kext_path"],
        trace_tool=expected_paths["trace_tool_path"],
    )


@dataclass(frozen=True)
class SourceGuard:
    root: Path
    head: str
    committed_bytes: dict[str, bytes]

    def assert_unchanged(self) -> None:
        if clean_committed_head(self.root) != self.head:
            raise BridgeFailure("source-integrity")
        for relative, expected in self.committed_bytes.items():
            try:
                current = require_regular_file(self.root / relative, "source helper").read_bytes()
            except (OSError, ValueError):
                raise BridgeFailure("source-integrity") from None
            if current != expected:
                raise BridgeFailure("source-integrity")

    def assert_candidate_bound(self, candidate: dict[str, Any]) -> None:
        self.assert_unchanged()
        if candidate.get("source_commit") != self.head:
            raise BridgeFailure("candidate-source-binding")
        identity, count = source_identity_v2(self.root, self.head)
        if (candidate.get("source_identity_sha256") != identity or
                candidate.get("source_identity_paths_count") != count):
            raise BridgeFailure("candidate-source-binding")


def fixed_git(root: Path, arguments: Sequence[str]) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            [GIT, "-c", "diff.external=", "-c", "core.fsmonitor=false",
             "-c", "core.excludesFile=/dev/null", "-C", str(root), *arguments],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            check=False, timeout=PRECHECK_TIMEOUT, env=LOCAL_ENV, close_fds=True,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BridgeFailure("source-integrity") from error


def fixed_git_bytes(root: Path, arguments: Sequence[str]) -> bytes:
    result = fixed_git(root, arguments)
    if result.returncode != 0:
        raise BridgeFailure("source-integrity")
    return result.stdout


def clean_committed_head(root: Path) -> str:
    try:
        expected_root = root.resolve(strict=True)
        reported_root = Path(fixed_git_bytes(root, ["rev-parse", "--show-toplevel"]).decode("utf-8").strip())
        reported_root = reported_root.resolve(strict=True)
    except (OSError, UnicodeDecodeError) as error:
        raise BridgeFailure("source-integrity") from error
    if reported_root != expected_root:
        raise BridgeFailure("source-integrity")
    if fixed_git_bytes(root, ["status", "--porcelain=v1", "--untracked-files=all"]):
        raise BridgeFailure("source-integrity")
    for arguments in (("diff", "--quiet", "--exit-code"),
                      ("diff", "--cached", "--quiet", "--exit-code")):
        if fixed_git(root, arguments).returncode != 0:
            raise BridgeFailure("source-integrity")
    try:
        head = fixed_git_bytes(root, ["rev-parse", "--verify", "HEAD^{commit}"]).decode("ascii").strip()
    except UnicodeDecodeError as error:
        raise BridgeFailure("source-integrity") from error
    if re.fullmatch(r"[0-9a-f]{40}", head) is None:
        raise BridgeFailure("source-integrity")
    return head


def source_identity_v2(root: Path, head: str) -> tuple[str, int]:
    raw = fixed_git_bytes(root, ["ls-tree", "-r", "-z", head, "--", *SOURCE_IDENTITY_PATHS_V2])
    records: list[tuple[str, str, str]] = []
    try:
        for entry in raw.split(b"\0"):
            if not entry:
                continue
            metadata, path = entry.split(b"\t", 1)
            mode, object_type, object_id = metadata.split(b" ", 2)
            if object_type == b"blob":
                records.append((
                    mode.decode("ascii"), object_id.decode("ascii"),
                    path.decode("utf-8", "surrogateescape"),
                ))
    except (UnicodeDecodeError, ValueError) as error:
        raise BridgeFailure("source-integrity") from error
    records.sort(key=lambda record: record[2])
    digest = hashlib.sha256(SOURCE_IDENTITY_DOMAIN_V2)
    for mode, object_id, path in records:
        digest.update(f"{mode} {object_id} {path}\0".encode("utf-8", "surrogateescape"))
    if not records:
        raise BridgeFailure("source-integrity")
    return digest.hexdigest(), len(records)


def make_source_guard(root: Path) -> SourceGuard:
    head = clean_committed_head(root)

    committed_bytes: dict[str, bytes] = {}
    for relative in TRUSTED_SOURCE_RELATIVES:
        try:
            local = require_regular_file(root / relative, "source helper").read_bytes()
        except (OSError, ValueError):
            raise BridgeFailure("source-integrity") from None
        shown = fixed_git_bytes(root, ["show", head + ":" + relative])
        if shown != local:
            raise BridgeFailure("source-integrity")
        committed_bytes[relative] = local
    return SourceGuard(root=root, head=head, committed_bytes=committed_bytes)


class StrictTransport:
    def __init__(self) -> None:
        self.known_hosts: Optional[Path] = None
        self.known_hosts_dir: Optional[Path] = None
        self.base: list[str] = []
        try:
            self.known_hosts_dir = Path(tempfile.mkdtemp(
                prefix="aiam-iwn-activation-known-hosts-", dir="/tmp"))
            directory = self.known_hosts_dir.lstat()
            if (stat.S_ISLNK(directory.st_mode) or not stat.S_ISDIR(directory.st_mode) or
                    directory.st_uid != os.getuid() or stat.S_IMODE(directory.st_mode) != 0o700):
                raise OSError("unsafe known-hosts directory")
            self.known_hosts = self.known_hosts_dir / "known_hosts"
            nofollow = getattr(os, "O_NOFOLLOW", None)
            cloexec = getattr(os, "O_CLOEXEC", None)
            if nofollow is None or cloexec is None:
                raise OSError("missing no-follow descriptor support")
            descriptor = os.open(
                self.known_hosts, os.O_WRONLY | os.O_CREAT | os.O_EXCL | nofollow | cloexec, 0o600)
            try:
                payload = (PINNED_QEMU_HOST_KEY + "\n").encode("ascii")
                offset = 0
                while offset < len(payload):
                    written = os.write(descriptor, payload[offset:])
                    if written <= 0:
                        raise OSError("short known-hosts write")
                    offset += written
            finally:
                os.close(descriptor)
        except OSError:
            self.close()
            raise BridgeFailure("transport-pinning") from None
        try:
            os.chmod(self.known_hosts, 0o600)
            if host_key_fingerprint() != PINNED_QEMU_HOST_KEY_SHA256:
                raise BridgeFailure("transport-pinning")
            checked = subprocess.run(
                [SSH_KEYGEN, "-lf", str(self.known_hosts), "-E", "sha256"],
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                check=False, timeout=PRECHECK_TIMEOUT, env=LOCAL_ENV, close_fds=True,
            )
            fields = checked.stdout.decode("ascii", "ignore").split()
            if (checked.returncode != 0 or len(fields) < 2 or
                    fields[1] != PINNED_QEMU_HOST_KEY_SHA256):
                raise BridgeFailure("transport-pinning")
        except (OSError, subprocess.SubprocessError, BridgeFailure):
            self.close()
            raise BridgeFailure("transport-pinning") from None
        self.base = [
            SSH, "-F", "/dev/null", "-T",
            "-o", "BatchMode=yes",
            "-o", "ConnectTimeout=8",
            "-o", "StrictHostKeyChecking=yes",
            "-o", "UserKnownHostsFile=" + str(self.known_hosts),
            "-o", "GlobalKnownHostsFile=/dev/null",
            "-o", "UpdateHostKeys=no",
            "-o", "LogLevel=ERROR",
            "-p", str(PINNED_QEMU_PORT), PINNED_QEMU_GUEST,
        ]

    def run(
        self, args: Sequence[str], *, input_bytes: Optional[bytes] = None,
        timeout: int = PRECHECK_TIMEOUT,
    ) -> subprocess.CompletedProcess[bytes]:
        if not self.base:
            raise BridgeFailure("transport-pinning")
        if (not args or any(not isinstance(argument, str) or "\x00" in argument
                            for argument in args)):
            raise BridgeFailure("transport-pinning")
        # ssh serializes the command portion through the guest shell instead
        # of preserving a local argv vector.  Make that one command explicitly
        # shell-quoted so the verifier's empty work-slot reaches Python as
        # a real empty argv element rather than disappearing before activation.
        remote_command = " ".join(shlex.quote(argument) for argument in args)
        options: dict[str, Any] = {
            "stdout": subprocess.PIPE,
            "stderr": subprocess.DEVNULL,
            "check": False,
            "timeout": timeout,
            "env": LOCAL_ENV,
            "close_fds": True,
        }
        if input_bytes is None:
            options["stdin"] = subprocess.DEVNULL
        else:
            options["input"] = input_bytes
        return subprocess.run([*self.base, remote_command], **options)

    def close(self) -> None:
        if self.known_hosts is not None:
            try:
                self.known_hosts.unlink(missing_ok=True)
            except AttributeError:
                if self.known_hosts.exists():
                    self.known_hosts.unlink()
        if self.known_hosts_dir is not None:
            try:
                self.known_hosts_dir.rmdir()
            except OSError:
                pass
        self.known_hosts = None
        self.known_hosts_dir = None
        self.base = []


REMOTE_VERIFIER = r'''
import base64
import hashlib
import json
import os
import re
import stat
import struct
import subprocess
import sys
import uuid
from pathlib import PurePosixPath

(mode, root, candidate_dir, trace_tool, preflight_dir, work, binary_sha,
 expected_uuid, receipt_sha, archive_sha, trace_sha, info_sha, tree_sha,
 expected_candidate_b64) = sys.argv[1:]

required_ids = {
    "com.zxystd.AirportItlwm",
    "com.apple.nke.rvi",
    "com.apple.driver.AppleMobileDevice",
    "com.highpoint-tech.kext.HighPointIOP",
    "com.highpoint-tech.kext.HighPointRR",
}
token_re = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
candidate_prefix = "/private/tmp/aiam-iwn-lab-candidate-"
trace_prefix = "/private/tmp/aiam-post-plti-trace-"
activation_prefix = "/private/var/tmp/aiam-iwn-activation-"

def fail():
    raise SystemExit(1)

def directory(path, mode=None, owner=None):
    try:
        value = os.lstat(path)
    except OSError:
        fail()
    if stat.S_ISLNK(value.st_mode) or not stat.S_ISDIR(value.st_mode):
        fail()
    if mode is not None and stat.S_IMODE(value.st_mode) != mode:
        fail()
    if owner is not None and value.st_uid != owner:
        fail()
    return value

def regular(path, mode=None, owner=None):
    try:
        value = os.lstat(path)
    except OSError:
        fail()
    if (stat.S_ISLNK(value.st_mode) or not stat.S_ISREG(value.st_mode) or
            value.st_nlink != 1):
        fail()
    if mode is not None and stat.S_IMODE(value.st_mode) != mode:
        fail()
    if owner is not None and value.st_uid != owner:
        fail()
    return value

def digest(path):
    regular(path)
    value = hashlib.sha256()
    with open(path, "rb", buffering=0) as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                return value.hexdigest()
            value.update(chunk)

def duplicate_reject(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail()
        result[key] = value
    return result

def reject_nonfinite(value):
    raise ValueError("non-finite JSON")

candidate_fields = {
    "source_commit", "source_identity_sha256", "source_identity_paths_count", "profile",
    "staged_kext_repo_path", "trace_client_sha256", "archive_sha256", "info_plist_sha256",
    "binary_sha256", "macho_uuid", "bundle_tree_sha256", "bundle_id", "bundle_version",
    "short_version",
}
digest_fields = {
    "source_identity_sha256", "trace_client_sha256", "archive_sha256", "info_plist_sha256",
    "binary_sha256", "bundle_tree_sha256",
}
receipt_validation = {
    "source_tree_clean_before_capture", "source_tree_clean_after_capture",
    "source_head_stable_during_capture", "staged_kext_path_matches_profile",
    "archive_validated", "staged_bundle_validated", "archive_and_staged_bundle_match",
    "artifact_stable_during_capture", "trace_client_regular_executable",
    "trace_client_stable_during_capture",
}
receipt_non_claims = {
    "release_tag_claimed", "compiler_invocation_inspected", "candidate_kext_installed",
    "candidate_kext_loaded", "auxkc_admission", "guest_rebooted", "association_tested",
    "authentication_tested", "dhcp_tested", "data_transfer_tested",
}

def canonical_candidate(value):
    if not isinstance(value, dict) or set(value) != candidate_fields:
        fail()
    if (not isinstance(value.get("source_commit"), str) or
            re.fullmatch(r"[0-9a-f]{40}", value["source_commit"]) is None):
        fail()
    for key in digest_fields:
        if not isinstance(value.get(key), str) or re.fullmatch(r"[0-9a-f]{64}", value[key]) is None:
            fail()
    if type(value.get("source_identity_paths_count")) is not int or value["source_identity_paths_count"] < 1:
        fail()
    if (value.get("profile") != "iwn-software-pmf-lab" or
            value.get("staged_kext_repo_path") != "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext" or
            value.get("bundle_id") != "com.zxystd.AirportItlwm"):
        fail()
    if (not isinstance(value.get("macho_uuid"), str) or
            re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", value["macho_uuid"]) is None):
        fail()
    if not isinstance(value.get("bundle_version"), str) or not isinstance(value.get("short_version"), str):
        fail()
    return {key: value[key] for key in candidate_fields}

def strict_receipt_candidate(path):
    regular(path, 0o600)
    try:
        document = json.loads(open(path, "rb").read().decode("utf-8"),
                              object_pairs_hook=duplicate_reject, parse_constant=reject_nonfinite)
    except Exception:
        fail()
    if (not isinstance(document, dict) or
            set(document) != {"schema", "receipt_kind", "created_at_utc", "candidate", "validation", "non_claims", "verdict"} or
            document.get("schema") != "itlwm-tahoe-iwn-lab-candidate-receipt/v2" or
            document.get("receipt_kind") != "local-unpublished-iwn-lab-candidate" or
            not isinstance(document.get("created_at_utc"), str)):
        fail()
    validation = document.get("validation")
    non_claims = document.get("non_claims")
    if (not isinstance(validation, dict) or set(validation) != receipt_validation or
            any(value is not True for value in validation.values()) or
            not isinstance(non_claims, dict) or set(non_claims) != receipt_non_claims or
            any(value is not False for value in non_claims.values()) or
            document.get("verdict") != {
                "local_candidate_identity_captured": True,
                "suitable_for_later_identity_verification": True,
                "runtime_experiment_performed": False,
            }):
        fail()
    return canonical_candidate(document.get("candidate"))

def expected_candidate():
    try:
        padded = expected_candidate_b64 + "=" * (-len(expected_candidate_b64) % 4)
        value = json.loads(base64.urlsafe_b64decode(padded).decode("utf-8"),
                           object_pairs_hook=duplicate_reject, parse_constant=reject_nonfinite)
    except Exception:
        fail()
    return canonical_candidate(value)

expected_candidate_value = expected_candidate()

def safe_relative(value):
    if not isinstance(value, str) or not value or value.startswith("/") or "\\" in value:
        fail()
    if any(part in {"", ".", ".."} for part in PurePosixPath(value).parts):
        fail()

def expected_manifest(directory_path):
    manifest = os.path.join(directory_path, "iwn-lab-bundle-manifest.json")
    regular(manifest, 0o600)
    try:
        document = json.loads(
            open(manifest, encoding="utf-8").read(),
            object_pairs_hook=duplicate_reject,
        )
    except Exception:
        fail()
    if (not isinstance(document, dict) or
            document.get("schema") != "itlwm-tahoe-iwn-lab-bundle-manifest/v1" or
            document.get("archive_sha256") != archive_sha or
            document.get("trace_client_sha256") != trace_sha or
            document.get("bundle_tree_sha256") != tree_sha):
        fail()
    files = document.get("files")
    if not isinstance(files, list) or not files:
        fail()
    result = {}
    for item in files:
        if not isinstance(item, dict) or set(item) != {"path", "sha256"}:
            fail()
        relative, item_sha = item.get("path"), item.get("sha256")
        safe_relative(relative)
        if (relative in result or not isinstance(item_sha, str) or
                re.fullmatch(r"[0-9a-f]{64}", item_sha) is None):
            fail()
        result[relative] = item_sha
    return result

def binary_uuid(path):
    data = open(path, "rb").read()
    if len(data) < 32:
        fail()
    magic, cpu_type, _subtype, file_type, count, command_bytes, _flags, _reserved = struct.unpack_from(
        "<IiiIIIII", data, 0)
    if magic != 0xFEEDFACF or cpu_type not in (0x01000007, 0x0100000C) or file_type != 0xB:
        fail()
    cursor, end, found = 32, 32 + command_bytes, None
    if end > len(data):
        fail()
    for _ in range(count):
        if cursor + 8 > end:
            fail()
        command, size = struct.unpack_from("<II", data, cursor)
        if size < 8 or cursor + size > end:
            fail()
        if command == 0x1B:
            if size < 24 or found is not None:
                fail()
            found = str(uuid.UUID(bytes=data[cursor + 8:cursor + 24])).upper()
        cursor += size
    if found is None:
        fail()
    return found

def verify_tree(bundle, expected, owner=None):
    directory(bundle, owner=owner)
    observed = {}
    for current, names, files in os.walk(bundle, followlinks=False):
        for name in names:
            value = os.lstat(os.path.join(current, name))
            if stat.S_ISLNK(value.st_mode) or not stat.S_ISDIR(value.st_mode):
                fail()
            if owner is not None and value.st_uid != owner:
                fail()
        for name in files:
            path = os.path.join(current, name)
            regular(path, owner=owner)
            relative = os.path.relpath(path, bundle).replace(os.sep, "/")
            safe_relative(relative)
            observed[relative] = digest(path)
    if observed != expected:
        fail()
    logical = hashlib.sha256()
    logical.update(b"tahoe-iwn-lab-kext-logical-tree/v1\0")
    for relative in sorted(observed):
        logical.update(relative.encode("utf-8", "surrogateescape"))
        logical.update(b"\0")
        logical.update(bytes.fromhex(observed[relative]))
        logical.update(b"\0")
    if logical.hexdigest() != tree_sha:
        fail()
    info = os.path.join(bundle, "Contents", "Info.plist")
    binary = os.path.join(bundle, "Contents", "MacOS", "AirportItlwm")
    if digest(info) != info_sha or digest(binary) != binary_sha:
        fail()
    if binary_uuid(binary) != expected_uuid:
        fail()

def scrub_untrusted_metadata(path):
    for command in (
        ["/bin/chmod", "-RN", path],
        ["/usr/bin/xattr", "-rc", path],
        ["/usr/bin/chflags", "-R", "nouchg", path],
    ):
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode != 0:
            fail()

def validate_no_follow_tree(path):
    directory(path)
    for current, names, files in os.walk(path, followlinks=False):
        directory(current)
        for name in names:
            directory(os.path.join(current, name))
        for name in files:
            regular(os.path.join(current, name))

def harden_tree(path, root_mode=None):
    value = directory(path)
    if root_mode is not None:
        os.chmod(path, root_mode)
    for current, names, files in os.walk(path, followlinks=False):
        current_value = directory(current)
        os.chown(current, 0, 0)
        os.chmod(current, stat.S_IMODE(current_value.st_mode) & ~0o7022)
        for name in names:
            child = os.path.join(current, name)
            child_value = directory(child)
            os.chown(child, 0, 0)
            os.chmod(child, stat.S_IMODE(child_value.st_mode) & ~0o7022)
        for name in files:
            child = os.path.join(current, name)
            child_value = regular(child)
            os.chown(child, 0, 0)
            os.chmod(child, stat.S_IMODE(child_value.st_mode) & ~0o7022)
    if root_mode is not None:
        os.chmod(path, root_mode)

def member_rows():
    result = subprocess.run(
        ["/usr/bin/kmutil", "inspect", "--show-kext-uuids", "-A",
         "/Library/KernelCollections/AuxiliaryKernelExtensions.kc"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        fail()
    rows, active = [], False
    for line in result.stdout.decode("utf-8", "replace").splitlines():
        if line == "Extension Information:":
            active = True
            continue
        if active and not line:
            break
        if active:
            fields = line.split("\t")
            if len(fields) < 3 or not fields[0]:
                fail()
            rows.append(line)
    identifiers = [line.split("\t", 1)[0] for line in rows]
    if len(rows) != 5 or set(identifiers) != required_ids or len(set(identifiers)) != 5:
        fail()
    airport = next((line for line in rows if line.startswith("com.zxystd.AirportItlwm\t")), "")
    if not airport:
        fail()
    companions = sorted(line for line in rows if line != airport)
    return airport, hashlib.sha256("\n".join(companions).encode("utf-8")).hexdigest()

def baseline_path():
    return os.path.join(root, "canonical-baseline.json")

def load_baseline():
    path = baseline_path()
    regular(path, 0o600, 0)
    try:
        value = json.loads(open(path, encoding="utf-8").read(), object_pairs_hook=duplicate_reject)
    except Exception:
        fail()
    if (not isinstance(value, dict) or
            set(value) != {"airport_sha256", "auxkc_sha256", "companion_members_sha256"} or
            any(re.fullmatch(r"[0-9a-f]{64}", str(item)) is None for item in value.values())):
        fail()
    return value

def parse_summary(path, expected_keys):
    regular(path, 0o600, 0)
    values = {}
    try:
        lines = open(path, encoding="utf-8").read().splitlines()
    except OSError:
        fail()
    for line in lines:
        if line.count("=") != 1:
            fail()
        key, value = line.split("=", 1)
        if re.fullmatch(r"[a-z0-9_]+", key) is None or key in values:
            fail()
        values[key] = value
    if set(values) != expected_keys:
        fail()
    return values

def verify_preflight_summary():
    expected_keys = {
        "candidate_uuid", "candidate_source_sha256", "candidate_sha256",
        "candidate_source_codesign_verify_exit", "candidate_private_codesign_verify_exit",
        "canonical_airport_sha256_before", "canonical_auxkc_sha256_before",
        "canonical_before_member_extract_exit", "canonical_before_member_validation_exit",
        "canonical_before_member_normalize_exit", "kmutil_create_exit",
        "private_auxkc_inspect_exit", "private_auxkc_bootkc_inspect_exit",
        "private_member_extract_exit", "private_member_validation_exit",
        "private_member_normalize_exit", "canonical_after_inspect_exit",
        "canonical_after_member_extract_exit", "canonical_after_member_validation_exit",
        "canonical_after_member_normalize_exit", "canonical_airport_sha256_after",
        "canonical_auxkc_sha256_after", "canonical_postflight", "canonical_mutation",
        "auxkc_members", "private_admission_result", "preflight_command_exit", "exit_status",
    }
    values = parse_summary(os.path.join(preflight_dir, "summary.txt"), expected_keys)
    if values["candidate_uuid"] != expected_uuid:
        fail()
    for key in (
        "candidate_source_sha256", "candidate_sha256", "canonical_airport_sha256_before",
        "canonical_auxkc_sha256_before", "canonical_airport_sha256_after",
        "canonical_auxkc_sha256_after",
    ):
        if re.fullmatch(r"[0-9a-f]{64}", values[key]) is None:
            fail()
    if values["candidate_source_sha256"] != binary_sha or values["candidate_sha256"] != binary_sha:
        fail()
    if (values["canonical_airport_sha256_before"] != values["canonical_airport_sha256_after"] or
            values["canonical_auxkc_sha256_before"] != values["canonical_auxkc_sha256_after"]):
        fail()
    for key in (
        "canonical_before_member_extract_exit", "canonical_before_member_validation_exit",
        "canonical_before_member_normalize_exit", "kmutil_create_exit",
        "private_auxkc_inspect_exit", "private_auxkc_bootkc_inspect_exit",
        "private_member_extract_exit", "private_member_validation_exit",
        "private_member_normalize_exit", "canonical_after_inspect_exit",
        "canonical_after_member_extract_exit", "canonical_after_member_validation_exit",
        "canonical_after_member_normalize_exit", "preflight_command_exit", "exit_status",
    ):
        if values[key] != "0":
            fail()
    for key in ("candidate_source_codesign_verify_exit", "candidate_private_codesign_verify_exit"):
        if re.fullmatch(r"[0-9]+", values[key]) is None:
            fail()
    if (values["canonical_postflight"] != "PASS" or values["canonical_mutation"] != "none" or
            values["auxkc_members"] != "5" or values["private_admission_result"] != "PASS"):
        fail()
    manifest = os.path.join(preflight_dir, "canonical-members-before.tsv")
    regular(manifest, owner=0)
    try:
        rows = [line for line in open(manifest, encoding="utf-8").read().splitlines() if line]
    except Exception:
        fail()
    identifiers = [line.split("\t", 1)[0] for line in rows]
    if len(rows) != 5 or set(identifiers) != required_ids or len(set(identifiers)) != 5:
        fail()
    airport = next((line for line in rows if line.startswith("com.zxystd.AirportItlwm\t")), "")
    if not airport:
        fail()
    companion = hashlib.sha256("\n".join(sorted(line for line in rows if line != airport)).encode("utf-8")).hexdigest()
    return values["canonical_airport_sha256_before"], values["canonical_auxkc_sha256_before"], companion

for parent in ("/private", "/private/tmp", "/private/var", "/private/var/tmp"):
    directory(parent)
if not root.startswith(activation_prefix):
    fail()
token = root[len(activation_prefix):]
if token_re.fullmatch(token) is None or root != activation_prefix + token:
    fail()
if candidate_dir != candidate_prefix + token:
    fail()
if trace_tool != trace_prefix + token + "/airport_itlwm_post_plti_trace":
    fail()
frozen_dir = os.path.join(root, "frozen")

if mode == "create":
    if os.path.lexists(root):
        fail()
    os.mkdir(root, 0o700)
    scrub_untrusted_metadata(root)
    os.chmod(root, 0o700)
    directory(root, 0o700, 0)
    print("ACTIVATION_ROOT_READY")
elif mode == "freeze":
    directory(root, 0o700, 0)
    directory(candidate_dir, 0o700)
    trace_parent = os.path.dirname(trace_tool)
    directory(trace_parent, 0o700)
    archive = os.path.join(candidate_dir, "AirportItlwm-iwn-software-pmf-lab.kext.zip")
    receipt = os.path.join(candidate_dir, "iwn-lab-candidate-receipt-v2.json")
    manifest = os.path.join(candidate_dir, "iwn-lab-bundle-manifest.json")
    expected_names = {os.path.basename(archive), os.path.basename(receipt), os.path.basename(manifest), "extracted"}
    if set(os.listdir(candidate_dir)) != expected_names:
        fail()
    if set(os.listdir(trace_parent)) != {"airport_itlwm_post_plti_trace"}:
        fail()
    if digest(archive) != archive_sha or digest(receipt) != receipt_sha or digest(trace_tool) != trace_sha:
        fail()
    if strict_receipt_candidate(receipt) != expected_candidate_value:
        fail()
    regular(trace_tool, 0o700)
    if os.path.lexists(frozen_dir):
        fail()
    copied = subprocess.run(["/usr/bin/ditto", "--norsrc", "--noacl", "--noextattr", "--noqtn", candidate_dir, frozen_dir],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if copied.returncode != 0:
        fail()
    directory(frozen_dir)
    if set(os.listdir(frozen_dir)) != expected_names:
        fail()
    validate_no_follow_tree(frozen_dir)
    scrub_untrusted_metadata(frozen_dir)
    harden_tree(frozen_dir, 0o700)
    directory(frozen_dir, 0o700, 0)
    frozen_archive = os.path.join(frozen_dir, os.path.basename(archive))
    frozen_receipt = os.path.join(frozen_dir, os.path.basename(receipt))
    if digest(frozen_archive) != archive_sha or digest(frozen_receipt) != receipt_sha:
        fail()
    if strict_receipt_candidate(frozen_receipt) != expected_candidate_value:
        fail()
    verify_tree(os.path.join(frozen_dir, "extracted", "AirportItlwm.kext"), expected_manifest(frozen_dir), 0)
    print("FROZEN_CANDIDATE_VERIFIED")
elif mode == "preflight":
    directory(root, 0o700, 0)
    directory(preflight_dir, 0o700, 0)
    verify_preflight_summary()
    verify_tree(os.path.join(preflight_dir, "AirportItlwm.kext"), expected_manifest(frozen_dir), 0)
    print("AUXKC_PREFLIGHT_VERIFIED")
elif mode == "baseline":
    directory(root, 0o700, 0)
    output = baseline_path()
    if os.path.lexists(output):
        fail()
    preflight_airport, preflight_auxkc, preflight_companion = verify_preflight_summary()
    airport, companion = member_rows()
    current_airport = digest("/Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm")
    current_auxkc = digest("/Library/KernelCollections/AuxiliaryKernelExtensions.kc")
    if (current_airport != preflight_airport or current_auxkc != preflight_auxkc or
            companion != preflight_companion):
        fail()
    document = {
        "airport_sha256": current_airport,
        "auxkc_sha256": current_auxkc,
        "companion_members_sha256": companion,
    }
    payload = (json.dumps(document, sort_keys=True) + "\n").encode("utf-8")
    descriptor = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
    try:
        os.write(descriptor, payload)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    regular(output, 0o600, 0)
    print("CANONICAL_BASELINE_CAPTURED")
elif mode == "baseline-current":
    directory(root, 0o700, 0)
    baseline = load_baseline()
    _airport, companion = member_rows()
    if (digest("/Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm") != baseline["airport_sha256"] or
            digest("/Library/KernelCollections/AuxiliaryKernelExtensions.kc") != baseline["auxkc_sha256"] or
            companion != baseline["companion_members_sha256"]):
        fail()
    print("CANONICAL_BASELINE_CURRENT")
elif mode == "activation":
    directory(root, 0o700, 0)
    if not work.startswith(root + "/activation-"):
        fail()
    suffix = work[len(root + "/activation-"):]
    if re.fullmatch(r"[0-9]{8}T[0-9]{6}Z", suffix) is None:
        fail()
    directory(work, 0o700, 0)
    values = parse_summary(
        os.path.join(work, "activation-summary.txt"),
        {"candidate_sha256", "candidate_uuid", "canonical_member_set", "auxkc_member_set", "activation_state"},
    )
    if (values["candidate_sha256"] != binary_sha or values["candidate_uuid"] != expected_uuid or
            values["canonical_member_set"] != "PASS" or values["auxkc_member_set"] != "PASS" or
            values["activation_state"] != "READY_FOR_GUEST_REBOOT"):
        fail()
    baseline = load_baseline()
    binary = "/Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm"
    if digest(binary) != binary_sha or binary_uuid(binary) != expected_uuid:
        fail()
    airport, companion = member_rows()
    if expected_uuid not in airport.upper() or companion != baseline["companion_members_sha256"]:
        fail()
    print("ACTIVATION_REMOTE_VERIFIED")
elif mode == "rollback":
    directory(root, 0o700, 0)
    baseline = load_baseline()
    _airport, companion = member_rows()
    if (digest("/Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm") != baseline["airport_sha256"] or
            digest("/Library/KernelCollections/AuxiliaryKernelExtensions.kc") != baseline["auxkc_sha256"] or
            companion != baseline["companion_members_sha256"]):
        fail()
    print("ACTIVATION_ROLLBACK_VERIFIED")
else:
    fail()
'''


@dataclass
class Facts:
    candidate_receipt_valid: bool = False
    private_stage_valid: bool = False
    source_head_bound: bool = False
    helper_bytes_from_head: bool = False
    strict_transport_pinned: bool = False
    pinned_guest_build: bool = False
    staged_candidate_reverified: bool = False
    private_preflight_passed: bool = False
    canonical_baseline_captured: bool = False
    canonical_baseline_current: bool = False
    activation_attempted: bool = False
    activation_completion_confirmed: bool = False
    activation_ready: bool = False
    activation_rollback_verified: bool = False
    reboot_dispatch_acknowledged: bool = False
    reboot_dispatch_uncertain: bool = False
    reboot_requested: bool = False
    reboot_down_observed: bool = False
    reboot_returned: bool = False
    boot_session_changed: bool = False


class PinnedRemote:
    def __init__(
        self,
        transport: StrictTransport,
        candidate: dict[str, Any],
        stage: StageBinding,
        receipt_digest: str,
        preflight_bytes: bytes,
        activation_bytes: bytes,
    ) -> None:
        self.transport = transport
        self.candidate = candidate
        self.stage = stage
        self.receipt_digest = receipt_digest
        self.preflight_bytes = preflight_bytes
        self.activation_bytes = activation_bytes
        self.candidate_wire = base64.urlsafe_b64encode(
            json.dumps(candidate, sort_keys=True, separators=(",", ":")).encode("utf-8")
        ).decode("ascii").rstrip("=")
        self.root = ACTIVATION_PREFIX + stage.token
        self.frozen_dir = self.root + "/frozen"
        self.frozen_candidate = self.frozen_dir + "/extracted/AirportItlwm.kext"
        self.preflight_dir = self.root + "/preflight"
        self.private_candidate = self.preflight_dir + "/AirportItlwm.kext"
        self.work = ""
        self.activation_completion_confirmed = False
        self.reboot_dispatch_acknowledged = False
        self.reboot_dispatch_uncertain = False

    def _verify(
        self, mode: str, phase: str, expected: str, *, timeout: int = PRECHECK_TIMEOUT
    ) -> None:
        args = [
            "/usr/bin/sudo", "-n", "/usr/bin/python3", "-I", "-", mode, self.root,
            self.stage.candidate_dir, self.stage.trace_tool, self.preflight_dir,
            self.work, str(self.candidate["binary_sha256"]),
            str(self.candidate["macho_uuid"]), self.receipt_digest,
            str(self.candidate["archive_sha256"]), str(self.candidate["trace_client_sha256"]),
            str(self.candidate["info_plist_sha256"]), str(self.candidate["bundle_tree_sha256"]),
            self.candidate_wire,
        ]
        try:
            result = self.transport.run(
                args, input_bytes=REMOTE_VERIFIER.encode("utf-8"), timeout=timeout)
        except (OSError, subprocess.TimeoutExpired):
            raise BridgeFailure(phase) from None
        if result.returncode != 0 or result.stdout != (expected + "\n").encode("ascii"):
            raise BridgeFailure(phase)

    def pinned_build(self) -> None:
        try:
            result = self.transport.run(
                ["/usr/bin/sw_vers", "-buildVersion"], timeout=PRECHECK_TIMEOUT)
        except (OSError, subprocess.TimeoutExpired):
            raise BridgeFailure("pinned-guest-build") from None
        if result.returncode != 0 or result.stdout.decode("ascii", "ignore").strip() != PINNED_QEMU_BUILD:
            raise BridgeFailure("pinned-guest-build")

    def create_root(self) -> None:
        self._verify("create", "activation-root", "ACTIVATION_ROOT_READY")

    def freeze_stage(self) -> None:
        self._verify("freeze", "frozen-candidate", "FROZEN_CANDIDATE_VERIFIED", timeout=MUTATION_TIMEOUT)

    def preflight(self) -> None:
        try:
            result = self.transport.run(
                [
                    "/usr/bin/sudo", "-n", "/bin/bash", "-s", "--", "--candidate",
                    self.frozen_candidate, "--out", self.preflight_dir,
                ],
                input_bytes=self.preflight_bytes, timeout=MUTATION_TIMEOUT,
            )
        except (OSError, subprocess.TimeoutExpired):
            raise BridgeFailure("private-preflight") from None
        if result.returncode != 0:
            raise BridgeFailure("private-preflight")
        self._verify("preflight", "private-preflight", "AUXKC_PREFLIGHT_VERIFIED")

    def baseline(self) -> None:
        self._verify("baseline", "canonical-baseline", "CANONICAL_BASELINE_CAPTURED")

    def baseline_current(self) -> None:
        self._verify("baseline-current", "canonical-baseline-current", "CANONICAL_BASELINE_CURRENT")

    def rollback_verified(self) -> bool:
        try:
            args = [
                "/usr/bin/sudo", "-n", "/usr/bin/python3", "-I", "-", "rollback", self.root,
                self.stage.candidate_dir, self.stage.trace_tool, self.preflight_dir,
                "", str(self.candidate["binary_sha256"]), str(self.candidate["macho_uuid"]),
                self.receipt_digest, str(self.candidate["archive_sha256"]),
                str(self.candidate["trace_client_sha256"]), str(self.candidate["info_plist_sha256"]),
                str(self.candidate["bundle_tree_sha256"]), self.candidate_wire,
            ]
            result = self.transport.run(
                args, input_bytes=REMOTE_VERIFIER.encode("utf-8"), timeout=PRECHECK_TIMEOUT)
        except (OSError, subprocess.TimeoutExpired):
            return False
        return result.returncode == 0 and result.stdout == b"ACTIVATION_ROLLBACK_VERIFIED\n"

    def activate(self) -> str:
        self.activation_completion_confirmed = False
        try:
            result = self.transport.run(
                [
                    "/usr/bin/sudo", "-n", "/bin/bash", "-s", "--", "--candidate",
                    self.private_candidate, "--work-root", self.root,
                    "--baseline", self.root + "/canonical-baseline.json",
                    "--expected-sha256", str(self.candidate["binary_sha256"]),
                    "--expected-uuid", str(self.candidate["macho_uuid"]),
                ],
                input_bytes=self.activation_bytes, timeout=MUTATION_TIMEOUT,
            )
        except (OSError, subprocess.TimeoutExpired):
            raise BridgeFailure("activation") from None
        if result.returncode == 255:
            # A disconnected SSH channel does not establish that the remote
            # transactional helper exited or completed its rollback trap.
            raise BridgeFailure("activation")
        self.activation_completion_confirmed = True
        if result.returncode != 0:
            raise BridgeFailure("activation")
        try:
            output = result.stdout.decode("ascii")
        except UnicodeDecodeError:
            raise BridgeFailure("activation") from None
        match = re.fullmatch(
            re.escape("ACTIVATION_READY:" + self.root + "/activation-") +
            r"[0-9]{8}T[0-9]{6}Z\n?", output)
        if match is None:
            raise BridgeFailure("activation")
        self.work = output.strip().split(":", 1)[1]
        self._verify("activation", "activation-verification", "ACTIVATION_REMOTE_VERIFIED")
        return self.work

    def _boot_token(self) -> str:
        try:
            result = self.transport.run(
                ["/usr/bin/sudo", "-n", "/usr/sbin/sysctl", "-n", "kern.boottime"],
                timeout=PRECHECK_TIMEOUT,
            )
        except (OSError, subprocess.TimeoutExpired):
            raise BridgeFailure("boot-session") from None
        token = result.stdout.decode("ascii", "ignore").strip()
        if result.returncode != 0 or not token or len(token) > 256:
            raise BridgeFailure("boot-session")
        return token

    def reboot_and_wait(self) -> tuple[bool, bool, bool]:
        before = self._boot_token()
        self.reboot_dispatch_acknowledged = False
        self.reboot_dispatch_uncertain = False
        try:
            marker = self.transport.run(
                ["/usr/bin/sudo", "-n", "/usr/bin/touch", self.work + "/reboot-requested"],
                timeout=PRECHECK_TIMEOUT,
            )
            synced = self.transport.run(["/usr/bin/sudo", "-n", "/bin/sync"], timeout=PRECHECK_TIMEOUT)
            if marker.returncode != 0 or synced.returncode != 0:
                raise BridgeFailure("reboot-request")
        except (OSError, subprocess.TimeoutExpired):
            raise BridgeFailure("reboot-request") from None
        try:
            dispatched = self.transport.run(
                ["/usr/bin/sudo", "-n", "/sbin/shutdown", "-r", "now"], timeout=15)
        except (OSError, subprocess.TimeoutExpired):
            self.reboot_dispatch_uncertain = True
        else:
            if dispatched.returncode == 0:
                self.reboot_dispatch_acknowledged = True
            elif dispatched.returncode == 255:
                # ssh commonly reports the guest's expected shutdown disconnect
                # as 255.  The bounded down/up/new-boot witness below is the
                # only accepted confirmation for this uncertain dispatch.
                self.reboot_dispatch_uncertain = True
            else:
                raise BridgeFailure("reboot-request")

        down = False
        for _ in range(DOWN_ATTEMPTS):
            try:
                probe = self.transport.run(["/usr/bin/true"], timeout=8)
            except (OSError, subprocess.TimeoutExpired):
                down = True
                break
            if probe.returncode != 0:
                down = True
                break
            time.sleep(POLL_SECONDS)
        if not down:
            raise BridgeFailure("reboot-down")

        for _ in range(RETURN_ATTEMPTS):
            try:
                build = self.transport.run(["/usr/bin/sw_vers", "-buildVersion"], timeout=8)
            except (OSError, subprocess.TimeoutExpired):
                time.sleep(POLL_SECONDS)
                continue
            if build.returncode != 0 or build.stdout.decode("ascii", "ignore").strip() != PINNED_QEMU_BUILD:
                time.sleep(POLL_SECONDS)
                continue
            try:
                after = self._boot_token()
                marker = self.transport.run(
                    ["/usr/bin/sudo", "-n", "/usr/bin/test", "-f", self.work + "/reboot-requested"],
                    timeout=PRECHECK_TIMEOUT,
                )
            except (OSError, subprocess.TimeoutExpired):
                raise BridgeFailure("reboot-return") from None
            if marker.returncode != 0:
                raise BridgeFailure("reboot-return")
            if after == before:
                raise BridgeFailure("boot-session")
            return True, True, True
        raise BridgeFailure("reboot-return")


def run_phases(remote: Any, facts: Facts, source: Any) -> None:
    remote.create_root()
    remote.freeze_stage()
    facts.staged_candidate_reverified = True
    source.assert_unchanged()
    remote.preflight()
    facts.private_preflight_passed = True
    remote.baseline()
    facts.canonical_baseline_captured = True
    source.assert_unchanged()
    remote.baseline_current()
    facts.canonical_baseline_current = True
    facts.activation_attempted = True
    try:
        remote.activate()
    except BridgeFailure as error:
        facts.activation_completion_confirmed = bool(
            getattr(remote, "activation_completion_confirmed", False))
        if facts.activation_completion_confirmed:
            facts.activation_rollback_verified = remote.rollback_verified()
        raise BridgeFailure(error.phase, facts.activation_rollback_verified) from None
    facts.activation_completion_confirmed = True
    facts.activation_ready = True
    try:
        down, returned, changed = remote.reboot_and_wait()
    except BridgeFailure:
        facts.reboot_dispatch_acknowledged = bool(
            getattr(remote, "reboot_dispatch_acknowledged", False))
        facts.reboot_dispatch_uncertain = bool(
            getattr(remote, "reboot_dispatch_uncertain", False))
        facts.reboot_requested = facts.reboot_dispatch_acknowledged
        raise
    facts.reboot_dispatch_acknowledged = bool(
        getattr(remote, "reboot_dispatch_acknowledged", False))
    facts.reboot_dispatch_uncertain = bool(
        getattr(remote, "reboot_dispatch_uncertain", False))
    facts.reboot_down_observed = down
    facts.reboot_returned = returned
    facts.boot_session_changed = changed
    facts.reboot_requested = (facts.reboot_dispatch_acknowledged or
                              (facts.reboot_dispatch_uncertain and down and returned and changed))


def report(result: str, phase: str, facts: Facts) -> dict[str, object]:
    ready = result == "READY_FOR_LOADED_IDENTITY"
    return {
        "schema": SCHEMA_VERSION,
        "result": result,
        "failure_phase": phase,
        "validation": {
            "candidate_receipt_valid": facts.candidate_receipt_valid,
            "private_stage_valid": facts.private_stage_valid,
            "source_head_bound": facts.source_head_bound,
            "helper_bytes_from_head": facts.helper_bytes_from_head,
            "strict_transport_pinned": facts.strict_transport_pinned,
            "pinned_guest_build": facts.pinned_guest_build,
            "staged_candidate_reverified": facts.staged_candidate_reverified,
            "private_preflight_passed": facts.private_preflight_passed,
            "canonical_baseline_captured": facts.canonical_baseline_captured,
            "canonical_baseline_current_immediately_before_activation": facts.canonical_baseline_current,
        },
        "activation": {
            "attempted": facts.activation_attempted,
            "helper_completion_confirmed": facts.activation_completion_confirmed,
            "ready_for_guest_reboot": facts.activation_ready,
            "rollback_verified_after_activation_failure": facts.activation_rollback_verified,
        },
        "reboot": {
            "guest_only_dispatch_acknowledged": facts.reboot_dispatch_acknowledged,
            "guest_only_dispatch_uncertain": facts.reboot_dispatch_uncertain,
            "guest_only_requested": facts.reboot_requested,
            "strict_transport_down_observed": facts.reboot_down_observed,
            "strict_transport_returned": facts.reboot_returned,
            "boot_session_changed": facts.boot_session_changed,
        },
        "overlay": {
            "discard_required": facts.activation_attempted and not ready,
        },
        "non_claims": {
            "direct_kext_load_or_unload": False,
            "host_rebooted": False,
            "qemu_control_used": False,
            "network_configuration_changed": False,
            "association_tested": False,
            "credential_collected": False,
            "raw_guest_output_retained": False,
            "raw_identity_or_path_retained": False,
            "candidate_loaded_claimed": False,
            "runtime_experiment_performed": False,
        },
        "verdict": {
            "ready_for_loaded_identity_capture": ready,
            "candidate_loaded_claimed": False,
            "runtime_experiment_performed": False,
        },
    }


def run_bridge(candidate_input: Path, stage_input: Path, output: Path) -> tuple[int, str]:
    root = Path(__file__).resolve().parent.parent
    facts = Facts()
    phase = "input-validation"
    transport: Optional[StrictTransport] = None
    try:
        candidate_snapshot = snapshot_private_json(candidate_input, "candidate receipt")
        stage_snapshot = snapshot_private_json(stage_input, "private stage report")
        candidate = canonical_direct_candidate(candidate_snapshot.document)
        facts.candidate_receipt_valid = True
        receipt_digest = candidate_snapshot.sha256
        stage = validate_stage_report(
            stage_snapshot.document, candidate, candidate, receipt_digest)
        facts.private_stage_valid = True
        source = make_source_guard(root)
        source.assert_candidate_bound(candidate)
        facts.source_head_bound = True
        facts.helper_bytes_from_head = True
        transport = StrictTransport()
        facts.strict_transport_pinned = True
        remote = PinnedRemote(
            transport, candidate, stage, receipt_digest,
            source.committed_bytes[PREFLIGHT_RELATIVE],
            source.committed_bytes[ACTIVATION_RELATIVE],
        )
        remote.pinned_build()
        facts.pinned_guest_build = True
        run_phases(remote, facts, source)
        result = "READY_FOR_LOADED_IDENTITY"
        phase = "none"
        status = 0
    except BridgeFailure as error:
        result = "INCONCLUSIVE"
        phase = error.phase
        status = 1
    except (OSError, ValueError, subprocess.SubprocessError):
        result = "INCONCLUSIVE"
        phase = "input-validation"
        status = 1
    finally:
        if transport is not None:
            transport.close()

    try:
        write_private_json(output, report(result, phase, facts))
    except OSError:
        return 2, "IWN_CANDIDATE_ACTIVATION_REBOOT=INCONCLUSIVE phase=report-write"
    if status == 0:
        return 0, "IWN_CANDIDATE_ACTIVATION_REBOOT=READY_FOR_LOADED_IDENTITY"
    return 1, "IWN_CANDIDATE_ACTIVATION_REBOOT=INCONCLUSIVE phase=" + phase


class FixtureRemote:
    def __init__(
        self, fail_at: Optional[str] = None, reboot_dispatch: bool = True,
        reboot_disconnect: bool = False,
    ):
        self.fail_at = fail_at
        self.reboot_dispatch = reboot_dispatch
        self.reboot_disconnect = reboot_disconnect
        self.events: list[str] = []
        self.activation_completion_confirmed = True
        self.reboot_dispatch_acknowledged = False
        self.reboot_dispatch_uncertain = False

    def _event(self, value: str) -> None:
        self.events.append(value)
        if self.fail_at == value:
            raise BridgeFailure(value)

    def create_root(self) -> None:
        self._event("create")

    def freeze_stage(self) -> None:
        self._event("freeze")

    def preflight(self) -> None:
        self._event("preflight")

    def baseline(self) -> None:
        self._event("baseline")

    def baseline_current(self) -> None:
        self._event("baseline-current")

    def activate(self) -> None:
        if self.fail_at in ("activation-timeout", "activation-disconnect"):
            self.events.append(self.fail_at)
            self.activation_completion_confirmed = False
            raise BridgeFailure("activation")
        self._event("activation")

    def rollback_verified(self) -> bool:
        self.events.append("rollback")
        return True

    def reboot_and_wait(self) -> tuple[bool, bool, bool]:
        self._event("reboot")
        if not self.reboot_dispatch:
            raise BridgeFailure("reboot-request")
        if self.reboot_disconnect:
            self.reboot_dispatch_uncertain = True
            return True, True, True
        self.reboot_dispatch_acknowledged = True
        return True, True, True


class FixtureSource:
    def assert_unchanged(self) -> None:
        return None


def self_test() -> int:
    compile(REMOTE_VERIFIER, "<remote-activation-verifier>", "exec")
    fixture_uuid = uuid.UUID("01234567-89ab-cdef-0123-456789abcdef")
    def macho_fixture(file_type: int) -> bytes:
        header = struct.pack("<IiiIIIII", 0xFEEDFACF, 0x01000007, 3,
                             file_type, 1, 24, 0, 0)
        return header + struct.pack("<II", 0x1B, 24) + fixture_uuid.bytes
    if thin_kext_uuid(macho_fixture(0xB)) != str(fixture_uuid).upper():
        raise SystemExit("self-test: thin kext Mach-O UUID was rejected")
    try:
        thin_kext_uuid(macho_fixture(2))
    except ValueError:
        pass
    else:
        raise SystemExit("self-test: user executable Mach-O was accepted as a kext")
    raw_candidate = {
        "source_commit": "a" * 40,
        "source_identity_sha256": "b" * 64,
        "source_identity_paths_count": 2,
        "profile": LAB_PROFILE,
        "staged_kext_repo_path": "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext",
        "archive_sha256": "c" * 64,
        "info_plist_sha256": "d" * 64,
        "bundle_tree_sha256": "e" * 64,
        "binary_sha256": "f" * 64,
        "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
        "bundle_id": "com.zxystd.AirportItlwm",
        "bundle_version": "fixture",
        "short_version": "fixture",
        "trace_client_sha256": "1" * 64,
    }
    receipt_document = {
        "schema": RECEIPT_SCHEMA,
        "receipt_kind": RECEIPT_KIND,
        "created_at_utc": "2000-01-01T00:00:00+00:00",
        "candidate": raw_candidate,
        "validation": {
            "source_tree_clean_before_capture": True,
            "source_tree_clean_after_capture": True,
            "source_head_stable_during_capture": True,
            "staged_kext_path_matches_profile": True,
            "archive_validated": True,
            "staged_bundle_validated": True,
            "archive_and_staged_bundle_match": True,
            "artifact_stable_during_capture": True,
            "trace_client_regular_executable": True,
            "trace_client_stable_during_capture": True,
        },
        "non_claims": {
            "release_tag_claimed": False,
            "compiler_invocation_inspected": False,
            "candidate_kext_installed": False,
            "candidate_kext_loaded": False,
            "auxkc_admission": False,
            "guest_rebooted": False,
            "association_tested": False,
            "authentication_tested": False,
            "dhcp_tested": False,
            "data_transfer_tested": False,
        },
        "verdict": {
            "local_candidate_identity_captured": True,
            "suitable_for_later_identity_verification": True,
            "runtime_experiment_performed": False,
        },
    }
    candidate = canonical_direct_candidate(receipt_document)
    remote_prefix, marker, _remote_tail = REMOTE_VERIFIER.partition(
        '\nfor parent in ("/private", "/private/tmp", "/private/var", "/private/var/tmp"):')
    if not marker:
        raise SystemExit("self-test: remote verifier bootstrap marker is missing")
    remote_wire = base64.urlsafe_b64encode(
        json.dumps(candidate, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).decode("ascii").rstrip("=")
    original_argv = sys.argv
    sys.argv = ["fixture", "stage", "/private/tmp/fixture", CANDIDATE_PREFIX + "fixture-1",
                TRACE_PREFIX + "fixture-1/" + TRACE_TOOL_NAME, "/private/tmp/fixture/preflight", "",
                candidate["binary_sha256"], candidate["macho_uuid"], "2" * 64,
                candidate["archive_sha256"], candidate["trace_client_sha256"],
                candidate["info_plist_sha256"], candidate["bundle_tree_sha256"], remote_wire]
    remote_namespace: dict[str, Any] = {"__name__": "remote_fixture"}
    try:
        exec(compile(remote_prefix, "<remote-activation-verifier-fixture>", "exec"), remote_namespace)
    finally:
        sys.argv = original_argv
    with tempfile.TemporaryDirectory(prefix="aiam-iwn-remote-macho-") as temporary:
        remote_binary = Path(temporary) / "AirportItlwm"
        remote_binary.write_bytes(macho_fixture(0xB))
        if remote_namespace["binary_uuid"](str(remote_binary)) != str(fixture_uuid).upper():
            raise SystemExit("self-test: remote kext Mach-O parser rejected MH_KEXT")
        remote_binary.write_bytes(macho_fixture(2))
        try:
            remote_namespace["binary_uuid"](str(remote_binary))
        except SystemExit:
            pass
        else:
            raise SystemExit("self-test: remote kext Mach-O parser accepted MH_EXECUTE")
    with tempfile.TemporaryDirectory(prefix="aiam-iwn-activation-snapshot-") as temporary:
        receipt_path = Path(temporary) / "receipt.json"
        receipt_path.write_text(json.dumps(receipt_document), encoding="utf-8")
        receipt_path.chmod(0o600)
        snapshot = snapshot_private_json(receipt_path, "fixture receipt")
        if snapshot.document != receipt_document or snapshot.sha256 != sha256_bytes(snapshot.payload):
            raise SystemExit("self-test: stable receipt snapshot changed")
        for name, payload in (("duplicate.json", b'{"x":1,"x":2}'),
                              ("nonfinite.json", b'{"x":NaN}')):
            malformed_path = Path(temporary) / name
            malformed_path.write_bytes(payload)
            malformed_path.chmod(0o600)
            try:
                snapshot_private_json(malformed_path, "fixture malformed")
            except ValueError:
                pass
            else:
                raise SystemExit("self-test: ambiguous JSON snapshot accepted")
        receipt_path.chmod(0o644)
        try:
            snapshot_private_json(receipt_path, "fixture mode")
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: non-private receipt snapshot accepted")
        receipt_path.chmod(0o600)
        changed = False
        def mutate_after_read(descriptor: int, size: int) -> bytes:
            nonlocal changed
            chunk = os.read(descriptor, size)
            if not changed:
                receipt_path.write_text('{"changed":true}', encoding="utf-8")
                receipt_path.chmod(0o600)
                changed = True
            return chunk
        try:
            snapshot_private_json(receipt_path, "fixture changed", _read_chunk=mutate_after_read)
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: changed receipt snapshot accepted")
    token = "fixture-1"
    receipt_digest = "2" * 64
    stage = {
        "schema": STAGE_SCHEMA,
        "candidate_receipt_sha256": receipt_digest,
        "candidate": raw_candidate,
        "guest_stage": {
            "candidate_dir": CANDIDATE_PREFIX + token,
            "archive_path": CANDIDATE_PREFIX + token + "/" + ARCHIVE_NAME,
            "receipt_path": CANDIDATE_PREFIX + token + "/" + RECEIPT_NAME,
            "manifest_path": CANDIDATE_PREFIX + token + "/" + MANIFEST_NAME,
            "extracted_kext_path": CANDIDATE_PREFIX + token + "/extracted/AirportItlwm.kext",
            "trace_tool_path": TRACE_PREFIX + token + "/" + TRACE_TOOL_NAME,
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
            "archive_sha256": candidate["archive_sha256"],
            "trace_client_sha256": candidate["trace_client_sha256"],
        },
        "non_claims": {
            "candidate_kext_installed": False,
            "candidate_kext_loaded": False,
            "auxkc_mutated": False,
            "guest_rebooted": False,
            "runtime_experiment_performed": False,
        },
    }
    stage_binding = validate_stage_report(stage, raw_candidate, candidate, receipt_digest)
    if stage_binding.token != token:
        raise SystemExit("self-test: stage token mismatch")
    malformed = dict(stage)
    malformed["validation"] = dict(stage["validation"])
    malformed["validation"]["pinned_guest_host_key"] = False
    try:
        validate_stage_report(malformed, raw_candidate, candidate, receipt_digest)
    except ValueError:
        pass
    else:
        raise SystemExit("self-test: malformed stage accepted")

    class ActivationDisconnectTransport:
        def run(
            self, arguments: Sequence[str], *, input_bytes: Optional[bytes] = None,
            timeout: int = PRECHECK_TIMEOUT,
        ) -> subprocess.CompletedProcess[bytes]:
            del input_bytes, timeout
            return subprocess.CompletedProcess(arguments, 255, b"", b"")

    disconnected_activation = PinnedRemote(
        ActivationDisconnectTransport(), candidate, stage_binding, receipt_digest, b"", b"")
    try:
        disconnected_activation.activate()
    except BridgeFailure as error:
        if error.phase != "activation":
            raise SystemExit("self-test: activation disconnect phase changed")
    else:
        raise SystemExit("self-test: SSH-255 activation disconnect accepted")
    if disconnected_activation.activation_completion_confirmed:
        raise SystemExit("self-test: SSH-255 activation marked helper completion")

    class RebootDisconnectTransport:
        def __init__(self) -> None:
            self.boot_tokens = iter((b"before\n", b"after\n"))
            self.calls: list[tuple[str, ...]] = []

        def run(
            self, arguments: Sequence[str], *, input_bytes: Optional[bytes] = None,
            timeout: int = PRECHECK_TIMEOUT,
        ) -> subprocess.CompletedProcess[bytes]:
            del input_bytes, timeout
            command = tuple(arguments)
            self.calls.append(command)
            if command == ("/usr/bin/sudo", "-n", "/usr/sbin/sysctl", "-n", "kern.boottime"):
                return subprocess.CompletedProcess(arguments, 0, next(self.boot_tokens), b"")
            if command == ("/usr/bin/sudo", "-n", "/sbin/shutdown", "-r", "now"):
                return subprocess.CompletedProcess(arguments, 255, b"", b"")
            if command == ("/usr/bin/true",):
                return subprocess.CompletedProcess(arguments, 255, b"", b"")
            if command == ("/usr/bin/sw_vers", "-buildVersion"):
                return subprocess.CompletedProcess(arguments, 0, PINNED_QEMU_BUILD.encode("ascii") + b"\n", b"")
            if command[:3] in (("/usr/bin/sudo", "-n", "/usr/bin/touch"),
                               ("/usr/bin/sudo", "-n", "/bin/sync"),
                               ("/usr/bin/sudo", "-n", "/usr/bin/test")):
                return subprocess.CompletedProcess(arguments, 0, b"", b"")
            raise SystemExit("self-test: unexpected reboot transport command")

    reboot_transport = RebootDisconnectTransport()
    disconnected_reboot = PinnedRemote(
        reboot_transport, candidate, stage_binding, receipt_digest, b"", b"")
    disconnected_reboot.work = disconnected_reboot.root + "/activation-20000101T000000Z"
    if disconnected_reboot.reboot_and_wait() != (True, True, True):
        raise SystemExit("self-test: SSH-255 reboot witness failed")
    if (disconnected_reboot.reboot_dispatch_acknowledged or
            not disconnected_reboot.reboot_dispatch_uncertain):
        raise SystemExit("self-test: SSH-255 reboot dispatch classification changed")

    facts = Facts()
    remote = FixtureRemote()
    run_phases(remote, facts, FixtureSource())
    if remote.events != ["create", "freeze", "preflight", "baseline", "baseline-current", "activation", "reboot"]:
        raise SystemExit("self-test: phase order changed")
    if not (facts.activation_ready and facts.canonical_baseline_current and
            facts.reboot_dispatch_acknowledged and facts.reboot_requested and
            facts.reboot_down_observed and facts.boot_session_changed):
        raise SystemExit("self-test: success facts incomplete")

    facts = Facts()
    remote = FixtureRemote(reboot_disconnect=True)
    run_phases(remote, facts, FixtureSource())
    if (facts.reboot_dispatch_acknowledged or not facts.reboot_dispatch_uncertain or
            not facts.reboot_requested or not facts.reboot_down_observed or
            not facts.reboot_returned or not facts.boot_session_changed):
        raise SystemExit("self-test: observed SSH-255 reboot was not classified conservatively")

    for phase, forbidden in (("preflight", "activation"), ("baseline-current", "activation"),
                             ("baseline", "activation"), ("activation", "reboot"), ("reboot", "loaded")):
        facts = Facts()
        remote = FixtureRemote(phase)
        try:
            run_phases(remote, facts, FixtureSource())
        except BridgeFailure:
            pass
        else:
            raise SystemExit("self-test: injected failure accepted")
        if forbidden in remote.events:
            raise SystemExit("self-test: failure crossed forbidden phase")
        if phase == "activation" and not facts.activation_rollback_verified:
            raise SystemExit("self-test: rollback fact missing")

    facts = Facts()
    remote = FixtureRemote("activation-timeout")
    try:
        run_phases(remote, facts, FixtureSource())
    except BridgeFailure:
        pass
    else:
        raise SystemExit("self-test: uncertain activation completion accepted")
    if facts.activation_completion_confirmed or facts.activation_rollback_verified:
        raise SystemExit("self-test: uncertain activation received a rollback claim")
    if not report("INCONCLUSIVE", "activation", facts)["overlay"]["discard_required"]:
        raise SystemExit("self-test: uncertain activation did not require overlay discard")

    facts = Facts()
    remote = FixtureRemote("activation-disconnect")
    try:
        run_phases(remote, facts, FixtureSource())
    except BridgeFailure:
        pass
    else:
        raise SystemExit("self-test: disconnected activation completion accepted")
    if facts.activation_completion_confirmed or facts.activation_rollback_verified:
        raise SystemExit("self-test: disconnected activation received a rollback claim")

    facts = Facts()
    remote = FixtureRemote(reboot_dispatch=False)
    try:
        run_phases(remote, facts, FixtureSource())
    except BridgeFailure:
        pass
    else:
        raise SystemExit("self-test: rejected reboot dispatch accepted")
    if facts.reboot_dispatch_acknowledged or facts.reboot_requested:
        raise SystemExit("self-test: rejected reboot dispatch was reported as requested")

    ready = report("READY_FOR_LOADED_IDENTITY", "none", Facts(
        candidate_receipt_valid=True, private_stage_valid=True, source_head_bound=True,
        helper_bytes_from_head=True, strict_transport_pinned=True,
        pinned_guest_build=True, staged_candidate_reverified=True,
        private_preflight_passed=True, canonical_baseline_captured=True,
        canonical_baseline_current=True, activation_attempted=True, activation_ready=True,
        reboot_dispatch_acknowledged=True, reboot_requested=True,
        reboot_down_observed=True, reboot_returned=True, boot_session_changed=True,
    ))
    if ready["verdict"] != {
        "ready_for_loaded_identity_capture": True,
        "candidate_loaded_claimed": False,
        "runtime_experiment_performed": False,
    }:
        raise SystemExit("self-test: report verdict changed")
    if host_key_fingerprint() != PINNED_QEMU_HOST_KEY_SHA256:
        raise SystemExit("self-test: host-key self-check failed")
    calls: list[tuple[Sequence[str], dict[str, Any]]] = []
    original_run = subprocess.run
    def fake_run(arguments: Sequence[str], **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
        calls.append((arguments, kwargs))
        if arguments[0] == SSH_KEYGEN:
            return subprocess.CompletedProcess(
                arguments, 0, b"256 " + PINNED_QEMU_HOST_KEY_SHA256.encode("ascii") + b" fixture\n", b"")
        if arguments[0] == SSH:
            return subprocess.CompletedProcess(arguments, 0, b"", b"")
        raise SystemExit("self-test: strict transport invoked an unexpected binary")
    subprocess.run = fake_run  # type: ignore[assignment]
    try:
        fixture_transport = StrictTransport()
        fixture_transport.run(["/usr/bin/true"])
        fixture_transport.run(["/usr/bin/python3", "-"], input_bytes=b"fixture\n")
        fixture_transport.run(["/usr/bin/printf", "%s", ""])
        fixture_transport.close()
    finally:
        subprocess.run = original_run  # type: ignore[assignment]
    if len(calls) != 4 or calls[0][0][0] != SSH_KEYGEN:
        raise SystemExit("self-test: strict transport command shape changed")
    if (calls[0][1].get("stdin") is not subprocess.DEVNULL or
            calls[0][1].get("env") != LOCAL_ENV or not calls[0][1].get("close_fds")):
        raise SystemExit("self-test: host-key check inherited controller state")
    no_payload, payload, empty_argument = calls[1], calls[2], calls[3]
    if (no_payload[0][0] != SSH or no_payload[1].get("stdin") is not subprocess.DEVNULL or
            no_payload[1].get("env") != LOCAL_ENV or not no_payload[1].get("close_fds")):
        raise SystemExit("self-test: read-only SSH inherited controller stdin")
    if (payload[0][0] != SSH or payload[1].get("input") != b"fixture\n" or
            "stdin" in payload[1] or payload[1].get("env") != LOCAL_ENV or
            not payload[1].get("close_fds")):
        raise SystemExit("self-test: explicit verifier payload transport changed")
    if (no_payload[0][-1] != "/usr/bin/true" or
            payload[0][-1] != "/usr/bin/python3 -" or
            empty_argument[0][0] != SSH or
            empty_argument[0][-1] != "/usr/bin/printf %s ''"):
        raise SystemExit("self-test: strict transport lost an empty remote argv element")
    print("PASS: Tahoe IWN candidate activation/reboot bridge self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-receipt", type=Path)
    parser.add_argument("--candidate-stage-report", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--activate-and-reboot", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        if any((args.candidate_receipt, args.candidate_stage_report, args.output, args.activate_and_reboot)):
            parser.error("--self-test accepts no activation inputs")
        return self_test()
    if not args.activate_and_reboot:
        parser.error("--activate-and-reboot is required for the guest-only destructive bridge")
    if args.candidate_receipt is None or args.candidate_stage_report is None or args.output is None:
        parser.error("--candidate-receipt, --candidate-stage-report, and --output are required")
    root = Path(__file__).resolve().parent.parent
    try:
        output = prepare_output(root, args.output)
    except ValueError:
        print("IWN_CANDIDATE_ACTIVATION_REBOOT=INCONCLUSIVE phase=output-validation")
        return 2
    status, message = run_bridge(args.candidate_receipt, args.candidate_stage_report, output)
    print(message)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
