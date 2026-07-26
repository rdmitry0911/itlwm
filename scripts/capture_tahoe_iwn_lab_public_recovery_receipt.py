#!/usr/bin/env python3
"""Capture a local-only identity receipt for the public recovery sidecar.

The recovery sidecar is deliberately a separate artifact from the IWN kext
candidate.  This tool binds four local facts without opening a transport or
running the helper: a typed v2 candidate receipt, the clean committed source
identity that receipt names, one opaque gate token, and the fixed-output
public recovery helper binary.  The resulting receipt contains hashes and a
Mach-O UUID only; it does not serialize input paths, target identities,
credentials, or any remote command output.

This is an identity boundary, not a build, staging, association, or recovery
runner.  A later sidecar stage must independently compare the candidate
receipt bytes and helper bytes it actually transfers to the values captured
here.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import stat
import struct
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path
from typing import Any

from capture_tahoe_iwn_lab_candidate_receipt import (
    RECEIPT_SCHEMA_V2 as CANDIDATE_RECEIPT_SCHEMA_V2,
    direct_runtime_candidate_from_receipt,
)
from tahoe_source_identity import source_identity


SCHEMA_VERSION = "itlwm-tahoe-iwn-lab-public-recovery-receipt/v1"
RECEIPT_KIND = "local-unpublished-iwn-lab-public-recovery-helper"
EXPECTED_CANDIDATE_RECEIPT_SCHEMA_V2 = "itlwm-tahoe-iwn-lab-candidate-receipt/v2"
LAB_PROFILE = "iwn-software-pmf-lab"
HELPER_REPO_PATH = "Build/Debug/Tahoe/airport_itlwm_lab_public_recovery"
HELPER_SOURCE_PATHS = (
    "AirportItlwmLabPublicRecovery/airport_itlwm_lab_public_recovery.m",
    "scripts/build_tahoe_lab_public_recovery.sh",
)
HELPER_SOURCE_IDENTITY_DOMAIN = b"tahoe-iwn-lab-public-recovery-source/v1\0"

LC_UUID = 0x1B
MACHO_64_LE_MAGIC = 0xFEEDFACF
MH_EXECUTE = 0x2
CPU_TYPE_X86_64 = 0x01000007
CPU_TYPE_ARM64 = 0x0100000C

SOURCE_COMMIT_RE = re.compile(r"[0-9a-f]{40}")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
UUID_RE = re.compile(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}")
GATE_TOKEN_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")

VALIDATION_KEYS = (
    "source_tree_clean_before_capture",
    "source_tree_clean_after_capture",
    "source_head_stable_during_capture",
    "candidate_receipt_typed_v2",
    "candidate_receipt_matches_current_source",
    "candidate_receipt_stable_during_capture",
    "gate_token_syntax_valid",
    "helper_repo_path_matches_build_output",
    "helper_regular_executable",
    "helper_macho_uuid_present",
    "helper_stable_during_capture",
    "helper_source_matches_current_head",
)


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_regular_file(path: Path, label: str) -> Path:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise ValueError(f"{label} is missing") from error
    if stat.S_ISLNK(metadata.st_mode):
        raise ValueError(f"{label} must not be a symlink")
    if not stat.S_ISREG(metadata.st_mode):
        raise ValueError(f"{label} must be a regular file")
    return path.resolve(strict=True)


def require_regular_executable(path: Path, label: str) -> Path:
    resolved = require_regular_file(path, label)
    if not os.access(resolved, os.X_OK):
        raise ValueError(f"{label} must be executable")
    return resolved


def repository_root() -> Path:
    script_root = Path(__file__).resolve().parent.parent
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=str(script_root),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return Path(result.stdout.strip()).resolve(strict=True)


def git_text(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=str(root),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def git_bytes(root: Path, *args: str) -> bytes:
    result = subprocess.run(
        ["git", *args],
        cwd=str(root),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout


def require_clean_head(root: Path) -> str:
    status = git_text(root, "status", "--porcelain=v1", "--untracked-files=all")
    if status:
        raise ValueError("public recovery source worktree is not clean")
    head = git_text(root, "rev-parse", "HEAD").lower()
    if SOURCE_COMMIT_RE.fullmatch(head) is None:
        raise ValueError("public recovery source HEAD is not a full commit")
    return head


def require_gate_token(value: object) -> str:
    if not isinstance(value, str) or GATE_TOKEN_RE.fullmatch(value) is None:
        raise ValueError("gate token is malformed")
    return value


def reject_duplicate_object_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    document: dict[str, object] = {}
    for key, value in pairs:
        if key in document:
            raise ValueError("duplicate JSON key")
        document[key] = value
    return document


def reject_nonfinite_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON constant: {value}")


def parse_json_document(data: bytes, label: str) -> object:
    try:
        return json.loads(
            data.decode("utf-8"),
            object_pairs_hook=reject_duplicate_object_keys,
            parse_constant=reject_nonfinite_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"{label} is not a safe JSON document") from error


def read_typed_candidate_receipt(path: Path) -> tuple[dict[str, Any], str]:
    """Read one v2 candidate receipt twice without retaining its contents."""
    receipt = require_regular_file(path, "candidate receipt")
    if CANDIDATE_RECEIPT_SCHEMA_V2 != EXPECTED_CANDIDATE_RECEIPT_SCHEMA_V2:
        raise ValueError("typed candidate receipt schema helper is inconsistent")
    first = receipt.read_bytes()
    document = parse_json_document(first, "candidate receipt")
    try:
        candidate = direct_runtime_candidate_from_receipt(document)
    except ValueError as error:
        raise ValueError("candidate receipt is not an accepted typed v2 receipt") from error
    if not isinstance(candidate, dict):
        raise ValueError("candidate receipt candidate section is malformed")
    second_receipt = require_regular_file(path, "candidate receipt")
    second = second_receipt.read_bytes()
    if first != second:
        raise ValueError("candidate receipt changed during receipt capture")
    return candidate.copy(), sha256_bytes(first)


def macho_uuid(binary: bytes) -> str:
    """Read LC_UUID only from a thin 64-bit little-endian executable Mach-O."""
    if len(binary) < 32:
        raise ValueError("public recovery helper Mach-O header is truncated")
    magic, cpu_type, _cpu_subtype, file_type, command_count, command_bytes, _flags, _ = (
        struct.unpack_from("<IiiIIIII", binary, 0)
    )
    if magic != MACHO_64_LE_MAGIC:
        raise ValueError("public recovery helper must be a thin 64-bit little-endian Mach-O")
    if cpu_type not in (CPU_TYPE_X86_64, CPU_TYPE_ARM64):
        raise ValueError("public recovery helper Mach-O CPU type is unsupported")
    if file_type != MH_EXECUTE:
        raise ValueError("public recovery helper Mach-O is not an executable")
    cursor = 32
    command_end = cursor + command_bytes
    if command_end > len(binary):
        raise ValueError("public recovery helper Mach-O load commands are truncated")
    found_uuid: str | None = None
    for _index in range(command_count):
        if cursor + 8 > command_end:
            raise ValueError("public recovery helper Mach-O load command is truncated")
        command, command_size = struct.unpack_from("<II", binary, cursor)
        if command_size < 8 or cursor + command_size > command_end:
            raise ValueError("public recovery helper Mach-O load command has an invalid size")
        if command == LC_UUID:
            if command_size < 24:
                raise ValueError("public recovery helper LC_UUID command is truncated")
            if found_uuid is not None:
                raise ValueError("public recovery helper Mach-O has multiple LC_UUID commands")
            found_uuid = str(uuid.UUID(bytes=binary[cursor + 8:cursor + 24])).upper()
        cursor += command_size
    if found_uuid is None:
        raise ValueError("public recovery helper Mach-O has no LC_UUID command")
    return found_uuid


def helper_identity(root: Path, helper: Path) -> dict[str, str]:
    root = root.resolve(strict=True)
    helper = require_regular_executable(helper, "public recovery helper")
    try:
        relative = helper.relative_to(root)
    except ValueError as error:
        raise ValueError("public recovery helper must resolve below the source repository") from error
    if relative.as_posix() != HELPER_REPO_PATH:
        raise ValueError("public recovery helper does not match the fixed build output path")
    binary = helper.read_bytes()
    return {
        "sha256": sha256_bytes(binary),
        "macho_uuid": macho_uuid(binary),
    }


def helper_source_identity(root: Path) -> dict[str, object]:
    """Bind the helper source and its fixed build recipe at the committed HEAD."""
    root = root.resolve(strict=True)
    digest = hashlib.sha256()
    digest.update(HELPER_SOURCE_IDENTITY_DOMAIN)
    for relative in HELPER_SOURCE_PATHS:
        entry = git_bytes(root, "ls-tree", "-z", "HEAD", "--", relative)
        if not entry.endswith(b"\0") or entry.count(b"\0") != 1:
            raise ValueError("public recovery helper source tree entry is malformed")
        payload = entry[:-1]
        try:
            metadata, path = payload.split(b"\t", 1)
            mode, object_type, object_id = metadata.split(b" ", 2)
        except ValueError as error:
            raise ValueError("public recovery helper source tree entry is malformed") from error
        if (path != relative.encode("utf-8") or object_type != b"blob" or
                mode not in {b"100644", b"100755"} or
                re.fullmatch(rb"[0-9a-f]{40}", object_id) is None):
            raise ValueError("public recovery helper source tree entry is unsafe")
        source_path = require_regular_file(root / relative, "public recovery helper source")
        committed = git_bytes(root, "show", f"HEAD:{relative}")
        if source_path.read_bytes() != committed:
            raise ValueError("public recovery helper source does not match HEAD")
        digest.update(mode + b" " + object_id + b" " + path + b"\0")
    return {
        "sha256": digest.hexdigest(),
        "paths_count": len(HELPER_SOURCE_PATHS),
    }


def current_source_identity(root: Path, head: str) -> dict[str, object]:
    identity = source_identity(root, "HEAD")
    value = identity.get("identity")
    count = identity.get("included_paths_count")
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise ValueError("current Tahoe source identity is malformed")
    if type(count) is not int or count < 1:
        raise ValueError("current Tahoe source identity path count is malformed")
    return {
        "commit": head,
        "identity_sha256": value,
        "paths_count": count,
    }


def candidate_matches_current_source(candidate: dict[str, Any], source: dict[str, object]) -> bool:
    return (
        candidate.get("source_commit") == source["commit"]
        and candidate.get("source_identity_sha256") == source["identity_sha256"]
        and candidate.get("source_identity_paths_count") == source["paths_count"]
    )


def make_receipt(
    root: Path, candidate_receipt: Path, helper: Path, gate_token: str
) -> dict[str, object]:
    """Return one receipt after stable local double reads of every boundary."""
    root = root.resolve(strict=True)
    gate_token = require_gate_token(gate_token)

    head_before = require_clean_head(root)
    source_before = current_source_identity(root, head_before)
    helper_source_before = helper_source_identity(root)
    candidate_before, candidate_receipt_sha256 = read_typed_candidate_receipt(candidate_receipt)
    if not candidate_matches_current_source(candidate_before, source_before):
        raise ValueError("candidate receipt does not match the current clean source identity")
    helper_before = helper_identity(root, helper)

    # Re-read every input and the complete source boundary.  This makes a
    # receipt fail closed if an artifact, receipt, source identity, or HEAD
    # changes while capture is in progress.
    candidate_after, candidate_receipt_sha256_after = read_typed_candidate_receipt(
        candidate_receipt
    )
    helper_after = helper_identity(root, helper)
    head_after = require_clean_head(root)
    source_after = current_source_identity(root, head_after)
    helper_source_after = helper_source_identity(root)
    if head_before != head_after or source_before != source_after:
        raise ValueError("public recovery source identity changed during receipt capture")
    if helper_source_before != helper_source_after:
        raise ValueError("public recovery helper source changed during receipt capture")
    if candidate_before != candidate_after or candidate_receipt_sha256 != candidate_receipt_sha256_after:
        raise ValueError("candidate receipt changed during receipt capture")
    if helper_before != helper_after:
        raise ValueError("public recovery helper changed during receipt capture")
    if not candidate_matches_current_source(candidate_after, source_after):
        raise ValueError("candidate receipt no longer matches the current clean source identity")

    return {
        "schema": SCHEMA_VERSION,
        "receipt_kind": RECEIPT_KIND,
        "created_at_utc": utc_now(),
        "candidate_binding": {
            "receipt_schema": CANDIDATE_RECEIPT_SCHEMA_V2,
            "receipt_sha256": candidate_receipt_sha256_after,
            "profile": candidate_after["profile"],
            "source_commit": source_after["commit"],
            "source_identity_sha256": source_after["identity_sha256"],
            "source_identity_paths_count": source_after["paths_count"],
        },
        "source": {
            "commit": source_after["commit"],
            "identity_sha256": source_after["identity_sha256"],
            "identity_paths_count": source_after["paths_count"],
            "helper_source_identity_sha256": helper_source_after["sha256"],
            "helper_source_paths_count": helper_source_after["paths_count"],
        },
        "gate_build_dir_token": gate_token,
        "helper": {
            "repo_output_path": HELPER_REPO_PATH,
            "sha256": helper_after["sha256"],
            "macho_uuid": helper_after["macho_uuid"],
        },
        "validation": {key: True for key in VALIDATION_KEYS},
        "non_claims": {
            "helper_build_invocation_inspected": False,
            "helper_staged_to_guest": False,
            "helper_invoked": False,
            "target_identity_collected": False,
            "credential_collected": False,
            "remote_output_collected": False,
            "association_tested": False,
            "recovery_tested": False,
        },
        "verdict": {
            "local_sidecar_identity_captured": True,
            "suitable_for_later_sidecar_staging_verification": True,
            "runtime_experiment_performed": False,
        },
    }


def require_digest(value: object, label: str) -> str:
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise ValueError(f"public recovery receipt {label} is malformed")
    return value


def require_commit(value: object, label: str) -> str:
    if not isinstance(value, str) or SOURCE_COMMIT_RE.fullmatch(value) is None:
        raise ValueError(f"public recovery receipt {label} is malformed")
    return value


def require_positive_int(value: object, label: str) -> int:
    if type(value) is not int or value < 1:
        raise ValueError(f"public recovery receipt {label} is malformed")
    return value


def validate_public_recovery_receipt_document(document: object) -> dict[str, object]:
    """Validate the safe, path-free subset needed by a later sidecar stage."""
    if not isinstance(document, dict):
        raise ValueError("public recovery receipt document is malformed")
    if document.get("schema") != SCHEMA_VERSION:
        raise ValueError("public recovery receipt schema is malformed")
    if document.get("receipt_kind") != RECEIPT_KIND:
        raise ValueError("public recovery receipt kind is malformed")
    candidate = document.get("candidate_binding")
    source = document.get("source")
    helper = document.get("helper")
    validation = document.get("validation")
    non_claims = document.get("non_claims")
    verdict = document.get("verdict")
    if not all(isinstance(item, dict) for item in (candidate, source, helper, validation, non_claims, verdict)):
        raise ValueError("public recovery receipt section is malformed")
    assert isinstance(candidate, dict)
    assert isinstance(source, dict)
    assert isinstance(helper, dict)
    assert isinstance(validation, dict)
    assert isinstance(non_claims, dict)
    assert isinstance(verdict, dict)
    if candidate.get("receipt_schema") != CANDIDATE_RECEIPT_SCHEMA_V2:
        raise ValueError("public recovery receipt candidate schema is malformed")
    candidate_receipt_sha256 = require_digest(candidate.get("receipt_sha256"), "candidate digest")
    candidate_commit = require_commit(candidate.get("source_commit"), "candidate source commit")
    candidate_identity = require_digest(
        candidate.get("source_identity_sha256"), "candidate source identity"
    )
    candidate_count = require_positive_int(
        candidate.get("source_identity_paths_count"), "candidate source path count"
    )
    if candidate.get("profile") != LAB_PROFILE:
        raise ValueError("public recovery receipt candidate profile is malformed")
    source_commit = require_commit(source.get("commit"), "source commit")
    source_identity = require_digest(source.get("identity_sha256"), "source identity")
    source_count = require_positive_int(source.get("identity_paths_count"), "source path count")
    helper_source_sha256 = require_digest(
        source.get("helper_source_identity_sha256"), "helper source identity"
    )
    helper_source_count = require_positive_int(
        source.get("helper_source_paths_count"), "helper source path count"
    )
    if (candidate_commit != source_commit or candidate_identity != source_identity or
            candidate_count != source_count):
        raise ValueError("public recovery receipt candidate and source identities differ")
    gate_token = require_gate_token(document.get("gate_build_dir_token"))
    if helper.get("repo_output_path") != HELPER_REPO_PATH:
        raise ValueError("public recovery receipt helper output path is malformed")
    helper_sha256 = require_digest(helper.get("sha256"), "helper digest")
    helper_uuid = helper.get("macho_uuid")
    if not isinstance(helper_uuid, str) or UUID_RE.fullmatch(helper_uuid) is None:
        raise ValueError("public recovery receipt helper Mach-O UUID is malformed")
    if any(validation.get(key) is not True for key in VALIDATION_KEYS):
        raise ValueError("public recovery receipt validation is malformed")
    if any(non_claims.get(key) is not False for key in (
        "helper_build_invocation_inspected",
        "helper_staged_to_guest",
        "helper_invoked",
        "target_identity_collected",
        "credential_collected",
        "remote_output_collected",
        "association_tested",
        "recovery_tested",
    )):
        raise ValueError("public recovery receipt non-claims are malformed")
    if (verdict.get("local_sidecar_identity_captured") is not True or
            verdict.get("suitable_for_later_sidecar_staging_verification") is not True or
            verdict.get("runtime_experiment_performed") is not False):
        raise ValueError("public recovery receipt verdict is malformed")
    return {
        "candidate_receipt_sha256": candidate_receipt_sha256,
        "candidate_profile": candidate["profile"],
        "source_commit": source_commit,
        "source_identity_sha256": source_identity,
        "source_identity_paths_count": source_count,
        "helper_source_identity_sha256": helper_source_sha256,
        "helper_source_paths_count": helper_source_count,
        "gate_build_dir_token": gate_token,
        "helper_sha256": helper_sha256,
        "helper_macho_uuid": helper_uuid,
    }


def load_public_recovery_receipt(path: Path) -> dict[str, object]:
    receipt = require_regular_file(path, "public recovery receipt")
    return validate_public_recovery_receipt_document(
        parse_json_document(receipt.read_bytes(), "public recovery receipt")
    )


def output_path_is_inside_root(root: Path, output_parent: Path) -> bool:
    try:
        output_parent.relative_to(root.resolve(strict=True))
    except ValueError:
        return False
    return True


def write_new_json(root: Path, document: dict[str, object], destination: str) -> None:
    rendered = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
    if destination == "-":
        sys.stdout.buffer.write(rendered)
        return
    path = Path(destination)
    if not path.is_absolute() or path.name in {"", ".", ".."}:
        raise ValueError("receipt output must be an absolute file path")
    if path.exists() or path.is_symlink():
        raise ValueError("receipt output must be a new non-symlink path")
    try:
        parent_stat = path.parent.lstat()
    except OSError as error:
        raise ValueError("receipt output parent is missing") from error
    if stat.S_ISLNK(parent_stat.st_mode) or not stat.S_ISDIR(parent_stat.st_mode):
        raise ValueError("receipt output parent is unsafe")
    parent = path.parent.resolve(strict=True)
    if output_path_is_inside_root(root, parent):
        raise ValueError("receipt output must be outside the source repository")
    output = parent / path.name
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(output, flags, 0o600)
    try:
        offset = 0
        while offset < len(rendered):
            written = os.write(descriptor, rendered[offset:])
            if written <= 0:
                raise OSError("receipt output write failed")
            offset += written
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    if stat.S_IMODE(output.stat().st_mode) != 0o600:
        raise ValueError("receipt output mode is unsafe")


def fixture_macho(uuid_value: str) -> bytes:
    header = struct.pack(
        "<IiiIIIII",
        MACHO_64_LE_MAGIC,
        CPU_TYPE_X86_64,
        3,
        MH_EXECUTE,
        1,
        24,
        0,
        0,
    )
    command = struct.pack("<II", LC_UUID, 24) + uuid.UUID(uuid_value).bytes
    return header + command


def fixture_candidate_document(source: dict[str, object]) -> dict[str, object]:
    return {
        "schema": CANDIDATE_RECEIPT_SCHEMA_V2,
        "receipt_kind": "local-unpublished-iwn-lab-candidate",
        "candidate": {
            "source_commit": source["commit"],
            "source_identity_sha256": source["identity_sha256"],
            "source_identity_paths_count": source["paths_count"],
            "profile": LAB_PROFILE,
            "staged_kext_repo_path": "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext",
            "trace_client_sha256": "a" * 64,
            "archive_sha256": "b" * 64,
            "info_plist_sha256": "c" * 64,
            "binary_sha256": "d" * 64,
            "bundle_tree_sha256": "e" * 64,
            "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
            "bundle_id": "com.zxystd.AirportItlwm",
            "bundle_version": "fixture-build",
            "short_version": "fixture-version",
        },
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


def self_test() -> int:
    expected_uuid = "89ABCDEF-0123-4567-89AB-CDEF01234567"
    with tempfile.TemporaryDirectory(prefix="aiam-public-recovery-receipt-") as temp:
        temporary = Path(temp)
        root = temporary / "source"
        helper_source = root / HELPER_SOURCE_PATHS[0]
        build_recipe = root / HELPER_SOURCE_PATHS[1]
        helper_source.parent.mkdir(parents=True)
        build_recipe.parent.mkdir(parents=True, exist_ok=True)
        helper_source.write_text("/* fixture public recovery helper */\n", encoding="utf-8")
        build_recipe.write_text("#!/usr/bin/env bash\nexit 0\n", encoding="utf-8")
        build_recipe.chmod(0o755)
        (root / "scripts" / "build_tahoe.sh").write_text(
            "#!/usr/bin/env bash\n", encoding="utf-8"
        )
        source_trace = root / "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c"
        source_trace.parent.mkdir(parents=True)
        source_trace.write_text("/* fixture trace source */\n", encoding="utf-8")
        (root / ".gitignore").write_text("Build/\n", encoding="utf-8")
        subprocess.run(["git", "init", "-q"], cwd=str(root), check=True)
        subprocess.run(
            ["git", "add", ".gitignore", *HELPER_SOURCE_PATHS, "scripts/build_tahoe.sh",
             "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c"],
            cwd=str(root),
            check=True,
        )
        subprocess.run(
            ["git", "-c", "user.name=AIAM", "-c", "user.email=aiam@example.invalid",
             "commit", "-q", "-m", "fixture"],
            cwd=str(root),
            check=True,
        )
        head = require_clean_head(root)
        source = current_source_identity(root, head)
        helper = root / HELPER_REPO_PATH
        helper.parent.mkdir(parents=True)
        helper.write_bytes(fixture_macho(expected_uuid))
        helper.chmod(0o700)
        candidate_path = temporary / "candidate.json"
        candidate_path.write_text(
            json.dumps(fixture_candidate_document(source)), encoding="utf-8"
        )

        document = make_receipt(root, candidate_path, helper, "Ab9_-.z")
        loaded = validate_public_recovery_receipt_document(document)
        if loaded["helper_macho_uuid"] != expected_uuid:
            raise SystemExit("self-test: helper Mach-O UUID did not round-trip")
        if loaded["helper_sha256"] != sha256_bytes(helper.read_bytes()):
            raise SystemExit("self-test: helper digest did not round-trip")
        if loaded["gate_build_dir_token"] != "Ab9_-.z":
            raise SystemExit("self-test: gate token did not round-trip")
        rendered = json.dumps(document, sort_keys=True)
        if str(candidate_path) in rendered or str(helper) in rendered:
            raise SystemExit("self-test: receipt serialized a local input path")
        malformed_helper_receipt = json.loads(json.dumps(document))
        malformed_helper = malformed_helper_receipt["helper"]
        if not isinstance(malformed_helper, dict):
            raise SystemExit("self-test: malformed-helper fixture is malformed")
        malformed_helper["macho_uuid"] = "not-a-uuid"
        try:
            validate_public_recovery_receipt_document(malformed_helper_receipt)
        except ValueError as error:
            if "Mach-O UUID" not in str(error):
                raise
        else:
            raise SystemExit("self-test: malformed helper Mach-O UUID was accepted")
        malformed_profile_receipt = json.loads(json.dumps(document))
        malformed_candidate_binding = malformed_profile_receipt["candidate_binding"]
        if not isinstance(malformed_candidate_binding, dict):
            raise SystemExit("self-test: malformed-profile fixture is malformed")
        malformed_candidate_binding["profile"] = "unexpected-profile"
        try:
            validate_public_recovery_receipt_document(malformed_profile_receipt)
        except ValueError as error:
            if "candidate profile" not in str(error):
                raise
        else:
            raise SystemExit("self-test: malformed candidate profile was accepted")
        try:
            macho_uuid(b"\0" * 32)
        except ValueError as error:
            if "thin 64-bit" not in str(error):
                raise
        else:
            raise SystemExit("self-test: non-Mach-O helper was accepted")
        output = temporary / "public-recovery-receipt.json"
        write_new_json(root, document, str(output))
        if load_public_recovery_receipt(output)["helper_sha256"] != loaded["helper_sha256"]:
            raise SystemExit("self-test: public recovery receipt loader did not round-trip")
        try:
            write_new_json(root, document, str(output))
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: existing receipt output was overwritten")
        try:
            write_new_json(root, document, str(root / "inside-source.json"))
        except ValueError as error:
            if "outside the source" not in str(error):
                raise
        else:
            raise SystemExit("self-test: source-tree receipt output was accepted")

        mismatched_candidate = fixture_candidate_document(source)
        mismatched_candidate_section = mismatched_candidate["candidate"]
        if not isinstance(mismatched_candidate_section, dict):
            raise SystemExit("self-test: mismatch fixture is malformed")
        mismatched_candidate_section["source_commit"] = "f" * 40
        candidate_path.write_text(json.dumps(mismatched_candidate), encoding="utf-8")
        try:
            make_receipt(root, candidate_path, helper, "Ab9_-.z")
        except ValueError as error:
            if "does not match the current clean source identity" not in str(error):
                raise
        else:
            raise SystemExit("self-test: mismatched candidate source was accepted")

        v1_candidate = fixture_candidate_document(source)
        v1_candidate["schema"] = "itlwm-tahoe-iwn-lab-candidate-receipt/v1"
        candidate_path.write_text(json.dumps(v1_candidate), encoding="utf-8")
        try:
            make_receipt(root, candidate_path, helper, "Ab9_-.z")
        except ValueError as error:
            if "typed v2" not in str(error):
                raise
        else:
            raise SystemExit("self-test: v1 candidate receipt was accepted")

        candidate_path.write_text(
            '{"schema": "x", "schema": "x"}', encoding="utf-8"
        )
        try:
            read_typed_candidate_receipt(candidate_path)
        except ValueError as error:
            if "safe JSON" not in str(error):
                raise
        else:
            raise SystemExit("self-test: duplicate candidate JSON key was accepted")

        candidate_path.write_text(
            json.dumps(fixture_candidate_document(source)), encoding="utf-8"
        )
        symlink = temporary / "helper-symlink"
        symlink.symlink_to(helper)
        try:
            helper_identity(root, symlink)
        except ValueError as error:
            if "must not be a symlink" not in str(error):
                raise
        else:
            raise SystemExit("self-test: symlinked helper was accepted")
        try:
            make_receipt(root, candidate_path, helper, "../unsafe")
        except ValueError as error:
            if "gate token" not in str(error):
                raise
        else:
            raise SystemExit("self-test: unsafe gate token was accepted")

        helper_source.write_text("/* dirty helper source */\n", encoding="utf-8")
        try:
            make_receipt(root, candidate_path, helper, "Ab9_-.z")
        except ValueError as error:
            if "worktree is not clean" not in str(error):
                raise
        else:
            raise SystemExit("self-test: dirty public recovery source was accepted")
    print("PASS: Tahoe IWN public recovery receipt self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-receipt", type=Path)
    parser.add_argument("--helper", type=Path)
    parser.add_argument("--gate-token")
    parser.add_argument("--output", default="-")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.candidate_receipt is None or args.helper is None or args.gate_token is None:
        parser.error(
            "--candidate-receipt, --helper, and --gate-token are required unless "
            "--self-test is used"
        )
    try:
        root = repository_root()
        document = make_receipt(root, args.candidate_receipt, args.helper, args.gate_token)
        write_new_json(root, document, args.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        # Do not render exception details: inputs can be locally named and this
        # helper is an identity boundary rather than a diagnostic exporter.
        del error
        print("FAIL: Tahoe IWN public recovery receipt", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
