#!/usr/bin/env python3
"""Create a local identity receipt for an unpublished Tahoe IWN lab build.

This tool intentionally does *not* use the release-candidate provenance
format: a locally staged lab build is not a tagged release.  It binds the
known IWN software-PMF lab output path, a local ZIP archive, one local regular
executable trace-client artifact, and a clean committed source HEAD.  It never
installs or loads a kext, contacts a guest, changes networking, or performs an
authentication attempt.

The resulting schema is deliberately narrow and typed so a later read-only
installed/loaded identity verifier can consume it without inferring release
status from a laboratory artifact.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import plistlib
import re
import stat
import struct
import subprocess
import sys
import tempfile
import uuid
import zipfile
from pathlib import Path
from pathlib import PurePosixPath
from typing import Any

from tahoe_source_identity import source_identity


RECEIPT_SCHEMA_V1 = "itlwm-tahoe-iwn-lab-candidate-receipt/v1"
RECEIPT_SCHEMA_V2 = "itlwm-tahoe-iwn-lab-candidate-receipt/v2"
# New receipts always use v2.  Keep the v1 spelling below because an
# installed/loaded identity verifier may need to read a historic local
# candidate receipt even though that receipt is not sufficient for the direct
# SAE runtime lane.
SCHEMA_VERSION = RECEIPT_SCHEMA_V2
SUPPORTED_RECEIPT_SCHEMAS = frozenset((RECEIPT_SCHEMA_V1, RECEIPT_SCHEMA_V2))
RECEIPT_KIND = "local-unpublished-iwn-lab-candidate"
BUNDLE_ID = "com.zxystd.AirportItlwm"
BUNDLE_ROOT = "AirportItlwm.kext"
ZIP_INFO = "AirportItlwm.kext/Contents/Info.plist"
ZIP_BINARY = "AirportItlwm.kext/Contents/MacOS/AirportItlwm"
LC_UUID = 0x1B
MACHO_64_LE_MAGIC = 0xFEEDFACF
SOURCE_COMMIT_RE = re.compile(r"[0-9a-f]{40}")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
UUID_RE = re.compile(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}")

BASE_VALIDATION_KEYS = (
    "source_tree_clean_before_capture",
    "source_tree_clean_after_capture",
    "source_head_stable_during_capture",
    "staged_kext_path_matches_profile",
    "archive_validated",
    "staged_bundle_validated",
    "archive_and_staged_bundle_match",
    "artifact_stable_during_capture",
)
DIRECT_RUNTIME_VALIDATION_KEYS = (
    "trace_client_regular_executable",
    "trace_client_stable_during_capture",
)

# This is an assertion about the build output selected by build_tahoe.sh, not
# a claim that this receipt observed the compiler invocation or preprocessor
# flags.  The latter is deliberately outside this local artifact reader.
PROFILE_STAGED_KEXT_PATHS = {
    "iwn-software-pmf-lab": "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext",
}


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def macho_uuid(binary: bytes) -> str:
    """Return LC_UUID from a thin little-endian 64-bit Mach-O binary."""
    if len(binary) < 32:
        raise ValueError("Mach-O header is truncated")
    header = struct.unpack_from("<IiiIIIII", binary, 0)
    if header[0] != MACHO_64_LE_MAGIC:
        raise ValueError("expected a thin little-endian 64-bit Mach-O")
    command_count = header[4]
    command_bytes = header[5]
    cursor = 32
    command_end = min(len(binary), cursor + command_bytes)
    for _ in range(command_count):
        if cursor + 8 > command_end:
            raise ValueError("Mach-O load command is truncated")
        command, command_size = struct.unpack_from("<II", binary, cursor)
        if command_size < 8 or cursor + command_size > command_end:
            raise ValueError("Mach-O load command has an invalid size")
        if command == LC_UUID:
            if command_size < 24:
                raise ValueError("LC_UUID command is truncated")
            return str(uuid.UUID(bytes=binary[cursor + 8 : cursor + 24])).upper()
        cursor += command_size
    raise ValueError("Mach-O has no LC_UUID command")


def plist_string(value: object) -> str:
    return value if isinstance(value, str) else ""


def parse_info_plist(data: bytes, label: str) -> dict[str, str]:
    try:
        info = plistlib.loads(data)
    except Exception as error:
        raise ValueError(f"{label} Info.plist is invalid: {error}") from error
    if not isinstance(info, dict):
        raise ValueError(f"{label} Info.plist is not a dictionary")
    bundle_id = plist_string(info.get("CFBundleIdentifier"))
    if bundle_id != BUNDLE_ID:
        raise ValueError(f"unexpected kext bundle identifier: {bundle_id or 'missing'}")
    return {
        "bundle_id": bundle_id,
        "bundle_version": plist_string(info.get("CFBundleVersion")),
        "short_version": plist_string(info.get("CFBundleShortVersionString")),
    }


def require_regular_file(path: Path, label: str) -> Path:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise ValueError(f"{label} is missing: {error}") from error
    if stat.S_ISLNK(metadata.st_mode):
        raise ValueError(f"{label} must not be a symlink")
    if not stat.S_ISREG(metadata.st_mode):
        raise ValueError(f"{label} must be a regular file")
    return path.resolve(strict=True)


def require_directory(path: Path, label: str) -> Path:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise ValueError(f"{label} is missing: {error}") from error
    if stat.S_ISLNK(metadata.st_mode):
        raise ValueError(f"{label} must not be a symlink")
    if not stat.S_ISDIR(metadata.st_mode):
        raise ValueError(f"{label} must be a directory")
    return path.resolve(strict=True)


def require_relative_path(root: Path, path: Path, expected: str, label: str) -> None:
    try:
        relative = path.relative_to(root)
    except ValueError as error:
        raise ValueError(f"{label} must resolve below the source repository") from error
    if relative.as_posix() != expected:
        raise ValueError(
            f"{label} does not match the selected profile path: "
            f"expected {expected}"
        )


def zip_member_is_symlink(info: zipfile.ZipInfo) -> bool:
    return stat.S_IFMT(info.external_attr >> 16) == stat.S_IFLNK


def bundle_tree_sha256(files: dict[str, bytes]) -> str:
    """Hash the logical bundle file tree, independent of ZIP timestamps."""
    digest = hashlib.sha256()
    digest.update(b"tahoe-iwn-lab-kext-logical-tree/v1\0")
    for relative_path in sorted(files):
        digest.update(relative_path.encode("utf-8", "surrogateescape"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_bytes(files[relative_path])))
        digest.update(b"\0")
    return digest.hexdigest()


def archive_bundle_files(archive: zipfile.ZipFile) -> dict[str, bytes]:
    """Read every regular file below the expected kext root exactly once."""
    files: dict[str, bytes] = {}
    root_prefix = BUNDLE_ROOT + "/"
    for info in archive.infolist():
        if not info.filename.startswith(root_prefix):
            continue
        relative_path = info.filename[len(root_prefix):]
        if not relative_path:
            if info.is_dir():
                continue
            raise ValueError("archive kext root must be a directory")
        components = PurePosixPath(relative_path).parts
        if any(component in {"", ".", ".."} for component in components):
            raise ValueError("archive kext member path is unsafe")
        if info.is_dir():
            continue
        if zip_member_is_symlink(info):
            raise ValueError("archive kext member must not be a symlink")
        if relative_path in files:
            raise ValueError(f"archive contains duplicate kext member: {relative_path}")
        files[relative_path] = archive.read(info)
    return files


def staged_bundle_files(staged_kext: Path) -> dict[str, bytes]:
    """Read the logical staged bundle tree without accepting symlink members."""
    files: dict[str, bytes] = {}
    for path in sorted(staged_kext.rglob("*")):
        metadata = path.lstat()
        relative_path = path.relative_to(staged_kext).as_posix()
        if stat.S_ISLNK(metadata.st_mode):
            raise ValueError(f"staged kext member must not be a symlink: {relative_path}")
        if stat.S_ISDIR(metadata.st_mode):
            continue
        if not stat.S_ISREG(metadata.st_mode):
            raise ValueError(f"staged kext member must be a regular file: {relative_path}")
        files[relative_path] = path.read_bytes()
    return files


def artifact_identity(
    info_plist: bytes, binary: bytes, bundle_files: dict[str, bytes]
) -> dict[str, str]:
    parsed = parse_info_plist(info_plist, "candidate")
    return {
        "bundle_id": parsed["bundle_id"],
        "bundle_version": parsed["bundle_version"],
        "short_version": parsed["short_version"],
        "info_plist_sha256": sha256_bytes(info_plist),
        "binary_sha256": sha256_bytes(binary),
        "macho_uuid": macho_uuid(binary),
        "bundle_tree_sha256": bundle_tree_sha256(bundle_files),
    }


def archive_identity(archive_path: Path) -> dict[str, str]:
    archive_path = require_regular_file(archive_path, "candidate archive")
    try:
        with zipfile.ZipFile(archive_path) as archive:
            files = archive_bundle_files(archive)
    except zipfile.BadZipFile as error:
        raise ValueError(f"candidate archive is not a ZIP: {error}") from error
    try:
        info_plist = files["Contents/Info.plist"]
        binary = files["Contents/MacOS/AirportItlwm"]
    except KeyError as error:
        raise ValueError(f"archive lacks required kext member: {error.args[0]}") from error
    return {
        "archive_sha256": sha256_file(archive_path),
        **artifact_identity(info_plist, binary, files),
    }


def staged_bundle_identity(root: Path, profile: str, staged_kext: Path) -> dict[str, str]:
    expected_path = PROFILE_STAGED_KEXT_PATHS[profile]
    staged_kext = require_directory(staged_kext, "staged kext bundle")
    require_relative_path(root, staged_kext, expected_path, "staged kext bundle")

    contents = require_directory(staged_kext / "Contents", "staged kext Contents")
    macos = require_directory(contents / "MacOS", "staged kext MacOS")
    info_path = require_regular_file(contents / "Info.plist", "staged kext Info.plist")
    binary_path = require_regular_file(macos / "AirportItlwm", "staged kext Mach-O")
    info_plist = info_path.read_bytes()
    binary = binary_path.read_bytes()
    return artifact_identity(info_plist, binary, staged_bundle_files(staged_kext))


def trace_client_identity(trace_client: Path) -> dict[str, str]:
    """Bind one local executable that will interpret direct-SAE traces.

    The receipt records only its bytes, never a guest path.  The runtime
    runner owns its separate restricted guest-path policy and must compare the
    copied executable to this digest before every invocation.  The source
    client and its build script remain covered by the committed source
    identity; hashing the executable here prevents confusing that source
    digest with the parser bytes that actually run on Tahoe.
    """
    trace_client = require_regular_file(trace_client, "direct-SAE trace client")
    if not os.access(trace_client, os.X_OK):
        raise ValueError("direct-SAE trace client must be executable")
    return {
        "trace_client_sha256": sha256_file(trace_client),
    }


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


def require_clean_head(root: Path) -> str:
    status = git_text(root, "status", "--porcelain=v1", "--untracked-files=all")
    if status:
        raise ValueError("candidate source worktree is not clean")
    head = git_text(root, "rev-parse", "HEAD").lower()
    if SOURCE_COMMIT_RE.fullmatch(head) is None:
        raise ValueError("candidate source HEAD is not a full commit")
    return head


def identities_match(archive: dict[str, str], staged: dict[str, str]) -> bool:
    return all(
        archive[key] == staged[key]
        for key in (
            "bundle_id",
            "bundle_version",
            "short_version",
            "info_plist_sha256",
            "binary_sha256",
            "macho_uuid",
            "bundle_tree_sha256",
        )
    )


def make_receipt(
    root: Path, profile: str, staged_kext: Path, archive_path: Path,
    trace_client: Path,
) -> dict[str, object]:
    if profile not in PROFILE_STAGED_KEXT_PATHS:
        raise ValueError("unsupported Tahoe lab profile")
    root = root.resolve(strict=True)
    head_before = require_clean_head(root)
    source = source_identity(root, "HEAD")
    trace_client_before = trace_client_identity(trace_client)
    archive_before = archive_identity(archive_path)
    staged_before = staged_bundle_identity(root, profile, staged_kext)
    if not identities_match(archive_before, staged_before):
        raise ValueError("archive and staged kext bundle do not have the same identity")

    # Re-read every artifact and the source boundary before writing a receipt,
    # so a concurrently replaced archive, staged bundle, trace client, or
    # source tree cannot be represented as one coherent candidate.
    archive_after = archive_identity(archive_path)
    staged_after = staged_bundle_identity(root, profile, staged_kext)
    trace_client_after = trace_client_identity(trace_client)
    head_after = require_clean_head(root)
    if head_before != head_after:
        raise ValueError("candidate source HEAD changed during receipt capture")
    if archive_before != archive_after or staged_before != staged_after:
        raise ValueError("candidate artifact changed during receipt capture")
    if trace_client_before != trace_client_after:
        raise ValueError("direct-SAE trace client changed during receipt capture")

    candidate = {
        "source_commit": head_after,
        "source_identity_sha256": str(source["identity"]),
        "source_identity_paths_count": int(source["included_paths_count"]),
        "profile": profile,
        "staged_kext_repo_path": PROFILE_STAGED_KEXT_PATHS[profile],
        **trace_client_after,
        **archive_after,
    }
    return {
        "schema": SCHEMA_VERSION,
        "receipt_kind": RECEIPT_KIND,
        "created_at_utc": utc_now(),
        "candidate": candidate,
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


def receipt_schema(document: object) -> str:
    """Return one supported receipt schema without silently upgrading it."""
    if not isinstance(document, dict):
        raise ValueError("IWN lab candidate receipt document")
    schema = document.get("schema")
    if schema not in SUPPORTED_RECEIPT_SCHEMAS:
        raise ValueError("IWN lab candidate receipt schema")
    return schema


def candidate_from_receipt(document: object) -> dict[str, Any]:
    """Validate a v1/v2 receipt and return its typed candidate section.

    This generic reader intentionally remains able to interpret historic v1
    receipts for non-direct identity work.  Call
    ``direct_runtime_candidate_from_receipt`` when the result could authorize
    a direct-SAE runtime observation; that lane requires v2's trace-client
    binding and rejects v1 explicitly.
    """
    schema = receipt_schema(document)
    if not isinstance(document, dict):
        raise ValueError("IWN lab candidate receipt document")
    if document.get("receipt_kind") != RECEIPT_KIND:
        raise ValueError("IWN lab candidate receipt kind")
    candidate = document.get("candidate")
    if not isinstance(candidate, dict):
        raise ValueError("IWN lab candidate receipt candidate section")
    source_commit = candidate.get("source_commit")
    source_identity_sha256 = candidate.get("source_identity_sha256")
    path_count = candidate.get("source_identity_paths_count")
    profile = candidate.get("profile")
    staged_path = candidate.get("staged_kext_repo_path")
    if not isinstance(source_commit, str) or SOURCE_COMMIT_RE.fullmatch(source_commit) is None:
        raise ValueError("IWN lab candidate receipt source commit")
    if (not isinstance(source_identity_sha256, str) or
            SHA256_RE.fullmatch(source_identity_sha256) is None):
        raise ValueError("IWN lab candidate receipt source identity")
    if not isinstance(path_count, int) or path_count < 1:
        raise ValueError("IWN lab candidate receipt source path count")
    if profile not in PROFILE_STAGED_KEXT_PATHS:
        raise ValueError("IWN lab candidate receipt profile")
    if staged_path != PROFILE_STAGED_KEXT_PATHS[profile]:
        raise ValueError("IWN lab candidate receipt staged path")
    if schema == RECEIPT_SCHEMA_V2:
        trace_client_sha256 = candidate.get("trace_client_sha256")
        if (not isinstance(trace_client_sha256, str) or
                SHA256_RE.fullmatch(trace_client_sha256) is None):
            raise ValueError("IWN lab candidate receipt trace-client digest")
    for key in (
        "archive_sha256",
        "info_plist_sha256",
        "binary_sha256",
        "bundle_tree_sha256",
    ):
        value = candidate.get(key)
        if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
            raise ValueError(f"IWN lab candidate receipt {key}")
    macho = candidate.get("macho_uuid")
    if not isinstance(macho, str) or UUID_RE.fullmatch(macho) is None:
        raise ValueError("IWN lab candidate receipt Mach-O UUID")
    if candidate.get("bundle_id") != BUNDLE_ID:
        raise ValueError("IWN lab candidate receipt bundle identifier")
    for key in ("bundle_version", "short_version"):
        if not isinstance(candidate.get(key), str):
            raise ValueError(f"IWN lab candidate receipt {key}")
    validation = document.get("validation")
    validation_keys = BASE_VALIDATION_KEYS
    if schema == RECEIPT_SCHEMA_V2:
        validation_keys += DIRECT_RUNTIME_VALIDATION_KEYS
    if not isinstance(validation, dict) or not all(
        validation.get(key) is True for key in validation_keys
    ):
        raise ValueError("IWN lab candidate receipt validation")
    non_claims = document.get("non_claims")
    if not isinstance(non_claims, dict) or any(
        non_claims.get(key) is not False
        for key in (
            "release_tag_claimed",
            "candidate_kext_installed",
            "candidate_kext_loaded",
            "auxkc_admission",
            "guest_rebooted",
            "association_tested",
            "authentication_tested",
            "dhcp_tested",
            "data_transfer_tested",
        )
    ):
        raise ValueError("IWN lab candidate receipt non-claims")
    return candidate.copy()


def direct_runtime_candidate_from_receipt(document: object) -> dict[str, Any]:
    """Validate the narrow v2 receipt required by the direct-SAE runtime lane.

    v1 parsing remains available through ``candidate_from_receipt`` to avoid
    invalidating historic local evidence.  It cannot be used here because it
    carries no immutable identity for the trace client that interprets the
    candidate's direct-SAE result.
    """
    if receipt_schema(document) != RECEIPT_SCHEMA_V2:
        raise ValueError("IWN lab direct-runtime receipt requires schema v2")
    candidate = candidate_from_receipt(document)
    if not isinstance(document, dict):
        raise ValueError("IWN lab candidate receipt document")
    verdict = document.get("verdict")
    if not isinstance(verdict, dict) or any(
        verdict.get(key) is not expected
        for key, expected in (
            ("local_candidate_identity_captured", True),
            ("suitable_for_later_identity_verification", True),
            ("runtime_experiment_performed", False),
        )
    ):
        raise ValueError("IWN lab direct-runtime receipt verdict")
    return candidate


def receipt_document_from_path(path: Path) -> object:
    receipt = require_regular_file(path, "IWN lab candidate receipt")
    try:
        return json.loads(receipt.read_text(encoding="utf-8"))
    except Exception as error:
        raise ValueError(f"IWN lab candidate receipt read: {error}") from error


def load_candidate_receipt(path: Path) -> dict[str, Any]:
    return candidate_from_receipt(receipt_document_from_path(path))


def load_direct_runtime_candidate_receipt(path: Path) -> dict[str, Any]:
    """Load only a regular-file v2 receipt safe for direct-SAE runtime use."""
    return direct_runtime_candidate_from_receipt(receipt_document_from_path(path))


def output_path_is_inside_root(root: Path, output: Path) -> bool:
    try:
        output.parent.resolve(strict=True).relative_to(root)
    except ValueError:
        return False
    return True


def write_new_json(root: Path, document: dict[str, object], destination: str) -> None:
    rendered = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if destination == "-":
        sys.stdout.write(rendered)
        return
    path = Path(destination)
    if output_path_is_inside_root(root, path):
        raise ValueError("receipt output must be outside the source repository")
    if path.exists() or path.is_symlink():
        raise ValueError("receipt output must be a new non-symlink path")
    if not path.parent.is_dir():
        raise ValueError("receipt output parent is missing")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o600)
    try:
        payload = rendered.encode("utf-8")
        offset = 0
        while offset < len(payload):
            written = os.write(descriptor, payload[offset:])
            if written <= 0:
                raise OSError("short receipt write")
            offset += written
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    metadata = path.lstat()
    if (stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode) or
            metadata.st_nlink != 1 or stat.S_IMODE(metadata.st_mode) != 0o600):
        raise OSError("receipt output post-write verification failed")


def fixture_macho(uuid_value: str) -> bytes:
    header = struct.pack("<IiiIIIII", MACHO_64_LE_MAGIC, 0, 0, 0, 1, 24, 0, 0)
    command = struct.pack("<II", LC_UUID, 24) + uuid.UUID(uuid_value).bytes
    return header + command


def self_test() -> int:
    expected_uuid = "01234567-89AB-CDEF-0123-456789ABCDEF"
    profile = "iwn-software-pmf-lab"
    expected_path = PROFILE_STAGED_KEXT_PATHS[profile]
    binary = fixture_macho(expected_uuid)
    info = plistlib.dumps({
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleVersion": "fixture-build",
        "CFBundleShortVersionString": "fixture-version",
    })
    with tempfile.TemporaryDirectory(prefix="aiam-iwn-lab-candidate-") as temp:
        temporary = Path(temp)
        root = temporary / "source"
        (root / "scripts").mkdir(parents=True)
        (root / "scripts" / "build_tahoe.sh").write_text(
            "#!/usr/bin/env bash\n", encoding="utf-8"
        )
        source_trace_client = (
            root / "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c"
        )
        source_trace_client.parent.mkdir(parents=True)
        source_trace_client.write_text(
            "/* fixture direct-SAE trace client */\n", encoding="utf-8"
        )
        (root / ".gitignore").write_text("Build/\n", encoding="utf-8")
        subprocess.run(["git", "init", "-q"], cwd=str(root), check=True)
        subprocess.run(
            [
                "git", "add", ".gitignore", "scripts/build_tahoe.sh",
                "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c",
            ],
            cwd=str(root),
            check=True,
        )
        subprocess.run(
            ["git", "-c", "user.name=AIAM", "-c", "user.email=aiam@example.invalid",
             "commit", "-q", "-m", "fixture"],
            cwd=str(root),
            check=True,
        )
        staged = root / expected_path
        (staged / "Contents" / "MacOS").mkdir(parents=True)
        (staged / "Contents" / "Info.plist").write_bytes(info)
        (staged / "Contents" / "MacOS" / "AirportItlwm").write_bytes(binary)
        archive = temporary / "candidate.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr(BUNDLE_ROOT + "/", b"")
            bundle.writestr(ZIP_INFO, info)
            bundle.writestr(ZIP_BINARY, binary)

        trace_client = temporary / "airport_itlwm_post_plti_trace"
        trace_client.write_bytes(b"fixture direct-SAE trace client executable\n")
        trace_client.chmod(0o700)
        document = make_receipt(root, profile, staged, archive, trace_client)
        candidate = candidate_from_receipt(document)
        if candidate["macho_uuid"] != expected_uuid:
            raise SystemExit("self-test: Mach-O UUID did not round-trip")
        if candidate["archive_sha256"] != sha256_file(archive):
            raise SystemExit("self-test: archive digest did not round-trip")
        if candidate["staged_kext_repo_path"] != expected_path:
            raise SystemExit("self-test: staged profile path did not round-trip")
        if candidate["trace_client_sha256"] != sha256_file(trace_client):
            raise SystemExit("self-test: trace-client digest did not round-trip")
        receipt = temporary / "receipt.json"
        write_new_json(root, document, str(receipt))
        if stat.S_IMODE(receipt.lstat().st_mode) != 0o600:
            raise SystemExit("self-test: candidate receipt is not private")
        if load_candidate_receipt(receipt)["binary_sha256"] != sha256_bytes(binary):
            raise SystemExit("self-test: typed receipt loader did not round-trip")
        if (load_direct_runtime_candidate_receipt(receipt)["trace_client_sha256"]
                != sha256_file(trace_client)):
            raise SystemExit("self-test: direct-runtime receipt loader did not round-trip")
        receipt_symlink = temporary / "receipt-symlink.json"
        receipt_symlink.symlink_to(receipt)
        try:
            load_direct_runtime_candidate_receipt(receipt_symlink)
        except ValueError as error:
            if "must not be a symlink" not in str(error):
                raise
        else:
            raise SystemExit("self-test: symlinked direct-runtime receipt accepted")
        trace_client_symlink = temporary / "trace-client-symlink"
        trace_client_symlink.symlink_to(trace_client)
        try:
            trace_client_identity(trace_client_symlink)
        except ValueError as error:
            if "must not be a symlink" not in str(error):
                raise
        else:
            raise SystemExit("self-test: symlinked trace client accepted")
        non_executable_trace_client = temporary / "non-executable-trace-client"
        non_executable_trace_client.write_bytes(b"not executable\n")
        non_executable_trace_client.chmod(0o600)
        try:
            trace_client_identity(non_executable_trace_client)
        except ValueError as error:
            if "must be executable" not in str(error):
                raise
        else:
            raise SystemExit("self-test: non-executable trace client accepted")

        # Historic v1 receipts retain their generic identity-reader path, but
        # must never silently authorize the direct-SAE runtime lane because
        # they predate the immutable trace-client binding.
        v1_document = json.loads(json.dumps(document))
        v1_document["schema"] = RECEIPT_SCHEMA_V1
        v1_candidate = v1_document["candidate"]
        if not isinstance(v1_candidate, dict):
            raise SystemExit("self-test: v1 candidate fixture is malformed")
        del v1_candidate["trace_client_sha256"]
        v1_validation = v1_document["validation"]
        if not isinstance(v1_validation, dict):
            raise SystemExit("self-test: v1 validation fixture is malformed")
        for key in DIRECT_RUNTIME_VALIDATION_KEYS:
            del v1_validation[key]
        if candidate_from_receipt(v1_document)["binary_sha256"] != sha256_bytes(binary):
            raise SystemExit("self-test: v1 generic receipt compatibility failed")
        try:
            direct_runtime_candidate_from_receipt(v1_document)
        except ValueError as error:
            if "requires schema v2" not in str(error):
                raise
        else:
            raise SystemExit("self-test: v1 receipt authorized direct runtime")

        malformed_v2 = json.loads(json.dumps(document))
        malformed_candidate = malformed_v2["candidate"]
        if not isinstance(malformed_candidate, dict):
            raise SystemExit("self-test: v2 candidate fixture is malformed")
        malformed_candidate["trace_client_sha256"] = "not-a-digest"
        try:
            direct_runtime_candidate_from_receipt(malformed_v2)
        except ValueError as error:
            if "trace-client digest" not in str(error):
                raise
        else:
            raise SystemExit("self-test: malformed trace-client digest accepted")
        try:
            write_new_json(root, document, str(receipt))
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: existing receipt output was overwritten")

        (root / "dirty-source-file").write_text("dirty\n", encoding="utf-8")
        try:
            make_receipt(root, profile, staged, archive, trace_client)
        except ValueError as error:
            if "worktree is not clean" not in str(error):
                raise
        else:
            raise SystemExit("self-test: dirty source tree was accepted")
    print("PASS: Tahoe IWN lab candidate receipt self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=sorted(PROFILE_STAGED_KEXT_PATHS))
    parser.add_argument("--staged-kext", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument(
        "--trace-client", type=Path,
        help="local regular executable Tahoe trace client built for this candidate",
    )
    parser.add_argument("--output", default="-")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if (args.profile is None or args.staged_kext is None or args.archive is None
            or args.trace_client is None):
        parser.error(
            "--profile, --staged-kext, --archive, and --trace-client are required "
            "unless --self-test is used"
        )
    try:
        root = repository_root()
        document = make_receipt(
            root, args.profile, args.staged_kext, args.archive, args.trace_client
        )
        write_new_json(root, document, args.output)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print(f"FAIL: Tahoe IWN lab candidate receipt: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
