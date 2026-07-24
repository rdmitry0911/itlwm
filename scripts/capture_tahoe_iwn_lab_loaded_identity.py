#!/usr/bin/env python3
"""Bind a local untagged IWN lab candidate receipt to a loaded Tahoe kext.

This is a read-only precondition capture for the unpublished IWN software-PMF
lab lane.  It consumes only the typed v2 direct-runtime candidate receipt,
connects to one pinned QEMU guest with a pinned host key, and observes the
installed and loaded AirportItlwm identity.  It never installs, activates,
loads, unloads, associates, changes networking, or reboots either machine.

The result is deliberately distinct from the tagged-release identity format.
An unpublished lab artifact must not acquire a release identity merely because
its installed and loaded executable hashes match a local receipt.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import json
import re
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable, Optional


# Keep the receipt helper optional at import time.  This makes --self-test and
# static consumers safe while a checkout is temporarily between receipt schema
# migrations; a real capture fails closed before opening SSH if the v2 direct
# receipt helper is unavailable.
try:
    from capture_tahoe_iwn_lab_candidate_receipt import (
        load_direct_runtime_candidate_receipt,
    )
except ImportError as import_error:
    load_direct_runtime_candidate_receipt = None
    _DIRECT_RECEIPT_IMPORT_ERROR: Optional[ImportError] = import_error
else:
    _DIRECT_RECEIPT_IMPORT_ERROR = None


SCHEMA_VERSION = "itlwm-tahoe-iwn-lab-loaded-identity/v1"
CAPTURE_KIND = "local-unpublished-iwn-lab-loaded-candidate"
RECEIPT_SCHEMA_VERSION = "itlwm-tahoe-iwn-lab-candidate-receipt/v2"
RECEIPT_KIND = "local-unpublished-iwn-lab-candidate"

PINNED_QEMU_GUEST = "devops@127.0.0.1"
PINNED_QEMU_PORT = 3322
PINNED_QEMU_BUILD = "25C56"
PINNED_QEMU_INTERFACE = "en1"
PINNED_KEXT_PATH = "/Library/Extensions/AirportItlwm.kext"
PINNED_QEMU_HOST_KEY = (
    "[127.0.0.1]:3322 ssh-ed25519 "
    "AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
)
PINNED_QEMU_HOST_KEY_SHA256 = "SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"
DOUBLE_READ_DELAY_SECONDS = 1

BUNDLE_ID = "com.zxystd.AirportItlwm"
LAB_PROFILE = "iwn-software-pmf-lab"
LAB_STAGED_KEXT_REPO_PATH = "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext"

SOURCE_COMMIT_RE = re.compile(r"[0-9a-f]{40}")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
UUID_RE = re.compile(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}")
UUID_FIND_RE = re.compile(
    r"\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
    r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\b"
)

_CANDIDATE_STRING_FIELDS = (
    "source_commit",
    "source_identity_sha256",
    "profile",
    "staged_kext_repo_path",
    "archive_sha256",
    "info_plist_sha256",
    "bundle_tree_sha256",
    "binary_sha256",
    "macho_uuid",
    "bundle_id",
    "trace_client_sha256",
)
_GUEST_VALUE_KEYS = {
    "guest_build",
    "installed_bundle_present",
    "installed_bundle_id",
    "installed_bundle_version",
    "installed_short_version",
    "installed_info_plist_sha256",
    "installed_binary_sha256",
}
_STABLE_SANITIZED_IDENTITY_FACT_KEYS = (
    "os_build",
    "wifi_interface",
    "wifi_interface_present",
    "installed_bundle_present",
    "installed_bundle_id",
    "installed_bundle_version",
    "installed_short_version",
    "installed_info_plist_sha256",
    "installed_binary_sha256",
    "installed_macho_uuid",
    "installed_macho_uuid_unambiguous",
    "kext_reported_loaded",
    "loaded_uuids_observed",
    "guest_observation_parsed",
)


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def host_key_fingerprint() -> str:
    fields = PINNED_QEMU_HOST_KEY.split()
    if len(fields) != 3:
        raise ValueError("pinned QEMU known-hosts entry is malformed")
    encoded_key = fields[2]
    key_blob = base64.b64decode(encoded_key + "=" * (-len(encoded_key) % 4))
    digest = base64.b64encode(hashlib.sha256(key_blob).digest()).decode("ascii")
    return "SHA256:" + digest.rstrip("=")


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


def canonical_direct_candidate(candidate: object) -> dict[str, Any]:
    """Retain only the direct v2 receipt fields needed by this binding lane."""
    if not isinstance(candidate, dict):
        raise ValueError("typed IWN lab candidate receipt candidate section")
    if any(not isinstance(candidate.get(key), str) for key in _CANDIDATE_STRING_FIELDS):
        raise ValueError("typed IWN lab candidate receipt string field")
    path_count = candidate.get("source_identity_paths_count")
    if type(path_count) is not int or path_count < 1:
        raise ValueError("typed IWN lab candidate receipt source path count")
    if SOURCE_COMMIT_RE.fullmatch(candidate["source_commit"]) is None:
        raise ValueError("typed IWN lab candidate receipt source commit")
    for key in (
        "source_identity_sha256",
        "archive_sha256",
        "info_plist_sha256",
        "bundle_tree_sha256",
        "binary_sha256",
        "trace_client_sha256",
    ):
        if SHA256_RE.fullmatch(candidate[key]) is None:
            raise ValueError(f"typed IWN lab candidate receipt {key}")
    if UUID_RE.fullmatch(candidate["macho_uuid"]) is None:
        raise ValueError("typed IWN lab candidate receipt Mach-O UUID")
    if candidate["profile"] != LAB_PROFILE:
        raise ValueError("typed IWN lab candidate receipt profile")
    if candidate["staged_kext_repo_path"] != LAB_STAGED_KEXT_REPO_PATH:
        raise ValueError("typed IWN lab candidate receipt staged kext path")
    if candidate["bundle_id"] != BUNDLE_ID:
        raise ValueError("typed IWN lab candidate receipt bundle identifier")
    return {
        **{key: candidate[key] for key in _CANDIDATE_STRING_FIELDS},
        "source_identity_paths_count": path_count,
    }


def load_typed_direct_candidate_receipt(path: Path) -> dict[str, Any]:
    """Load only the v2 receipt accepted for direct IWN runtime evidence."""
    receipt_path = require_regular_file(path, "candidate receipt")
    if load_direct_runtime_candidate_receipt is None:
        detail = "missing direct receipt helper"
        if _DIRECT_RECEIPT_IMPORT_ERROR is not None:
            detail = type(_DIRECT_RECEIPT_IMPORT_ERROR).__name__
        raise ValueError(f"typed IWN lab v2 receipt helper is unavailable ({detail})")
    try:
        candidate = load_direct_runtime_candidate_receipt(receipt_path)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise ValueError("typed IWN lab v2 candidate receipt was rejected") from error
    return canonical_direct_candidate(candidate)


def guest_probe_script() -> str:
    """Return the fixed read-only guest query; it accepts no caller data."""
    script = r'''
set -u
p="__PINNED_KEXT_PATH__"
info="$p/Contents/Info.plist"
bin="$p/Contents/MacOS/AirportItlwm"
printf 'guest_build=%s\n' "$(sw_vers -buildVersion 2>/dev/null || true)"
printf '__NETWORKSETUP_BEGIN__\n'
networksetup -listallhardwareports 2>/dev/null || true
printf '__NETWORKSETUP_END__\n'
if [ -d "$p" ] && [ -f "$info" ] && [ -f "$bin" ]; then
  printf 'installed_bundle_present=true\n'
else
  printf 'installed_bundle_present=false\n'
fi
printf 'installed_bundle_id=%s\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$info" 2>/dev/null || true)"
printf 'installed_bundle_version=%s\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$info" 2>/dev/null || true)"
printf 'installed_short_version=%s\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$info" 2>/dev/null || true)"
printf 'installed_info_plist_sha256=%s\n' "$(shasum -a 256 "$info" 2>/dev/null | awk '{print $1}')"
printf 'installed_binary_sha256=%s\n' "$(shasum -a 256 "$bin" 2>/dev/null | awk '{print $1}')"
printf '__INSTALLED_UUID_BEGIN__\n'
dwarfdump --uuid "$bin" 2>/dev/null || true
printf '__INSTALLED_UUID_END__\n'
printf '__LOADED_BEGIN__\n'
kextstat 2>/dev/null | grep -E 'com\.zxystd\.AirportItlwm|AirportItlwm' || true
kmutil showloaded 2>/dev/null | grep -E 'com\.zxystd\.AirportItlwm|AirportItlwm' || true
printf '__LOADED_END__\n'
'''
    return script.replace("__PINNED_KEXT_PATH__", PINNED_KEXT_PATH)


def run_pinned_qemu_guest(timeout_seconds: int) -> dict[str, object]:
    known_hosts = tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", prefix="aiam-iwn-pinned-known-hosts-", delete=False
    )
    known_hosts.write(PINNED_QEMU_HOST_KEY + "\n")
    known_hosts.close()
    try:
        command = [
            "ssh",
            "-o",
            "BatchMode=yes",
            "-o",
            "StrictHostKeyChecking=yes",
            "-o",
            f"UserKnownHostsFile={known_hosts.name}",
            "-o",
            "GlobalKnownHostsFile=/dev/null",
            "-p",
            str(PINNED_QEMU_PORT),
            PINNED_QEMU_GUEST,
            # Transactional activation intentionally makes the canonical
            # bundle root-owned and non-writable.  The fixed probe itself is
            # read-only, but it must run with the already-required
            # noninteractive authority to observe that hardened bundle.
            "sudo",
            "-n",
            "/bin/bash",
            "-s",
        ]
        started = time.monotonic()
        result = subprocess.run(
            command,
            input=guest_probe_script(),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_seconds,
        )
        return {
            "returncode": result.returncode,
            "stdout": result.stdout,
            "duration_seconds": round(time.monotonic() - started, 3),
            "stderr_line_count": len(result.stderr.splitlines()),
        }
    finally:
        Path(known_hosts.name).unlink(missing_ok=True)


def section(output: str, begin: str, end: str) -> Optional[str]:
    if output.count(begin) != 1 or output.count(end) != 1:
        return None
    begin_index = output.find(begin)
    end_index = output.find(end)
    if end_index <= begin_index:
        return None
    return output[begin_index + len(begin):end_index]


def guest_key_values(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if key not in _GUEST_VALUE_KEYS:
            continue
        if key in values:
            raise ValueError("duplicate guest identity value")
        values[key] = value.strip()
    return values


def unique_uuids(text: str) -> list[str]:
    values: list[str] = []
    for value in UUID_FIND_RE.findall(text):
        normalized = value.upper()
        if normalized not in values:
            values.append(normalized)
    return values


def interface_present(networksetup_output: Optional[str]) -> bool:
    if networksetup_output is None:
        return False
    return bool(
        re.search(
            r"(?im)^\s*Device:\s*" + re.escape(PINNED_QEMU_INTERFACE) + r"\s*$",
            networksetup_output,
        )
    )


def parse_guest_observation(output: str) -> dict[str, object]:
    """Reduce untrusted guest stdout to categorical, non-secret evidence."""
    default = {
        "os_build": "unknown",
        "wifi_interface": PINNED_QEMU_INTERFACE,
        "wifi_interface_present": False,
        "installed_bundle_present": False,
        "installed_bundle_id": "",
        "installed_bundle_version": "",
        "installed_short_version": "",
        "installed_info_plist_sha256": "",
        "installed_binary_sha256": "",
        "installed_macho_uuid": "",
        "installed_macho_uuid_unambiguous": False,
        "kext_reported_loaded": False,
        "loaded_uuids_observed": [],
        "loaded_driver_line_count": 0,
        "guest_observation_parsed": False,
    }
    try:
        values = guest_key_values(output)
        network = section(output, "__NETWORKSETUP_BEGIN__", "__NETWORKSETUP_END__")
        installed_uuid_text = section(
            output, "__INSTALLED_UUID_BEGIN__", "__INSTALLED_UUID_END__"
        )
        loaded = section(output, "__LOADED_BEGIN__", "__LOADED_END__")
        if network is None or installed_uuid_text is None or loaded is None:
            return default
        installed_uuids = unique_uuids(installed_uuid_text)
        loaded_driver_lines = [
            line for line in loaded.splitlines()
            if re.search(r"(?i)\bcom\.zxystd\.AirportItlwm\b|\bAirportItlwm\b", line)
        ]
        installed_uuid = installed_uuids[0] if len(installed_uuids) == 1 else ""
        return {
            "os_build": values.get("guest_build", "unknown"),
            "wifi_interface": PINNED_QEMU_INTERFACE,
            "wifi_interface_present": interface_present(network),
            "installed_bundle_present": values.get("installed_bundle_present") == "true",
            "installed_bundle_id": values.get("installed_bundle_id", ""),
            "installed_bundle_version": values.get("installed_bundle_version", ""),
            "installed_short_version": values.get("installed_short_version", ""),
            "installed_info_plist_sha256": values.get("installed_info_plist_sha256", ""),
            "installed_binary_sha256": values.get("installed_binary_sha256", ""),
            "installed_macho_uuid": installed_uuid,
            "installed_macho_uuid_unambiguous": len(installed_uuids) == 1,
            "kext_reported_loaded": bool(loaded_driver_lines),
            "loaded_uuids_observed": unique_uuids("\n".join(loaded_driver_lines)),
            "loaded_driver_line_count": len(loaded_driver_lines),
            "guest_observation_parsed": True,
        }
    except ValueError:
        return default


def reduce_guest_probe(query: dict[str, object]) -> tuple[dict[str, object], dict[str, object]]:
    """Drop raw probe output while retaining only safe command and identity facts."""
    stdout = query.get("stdout")
    if not isinstance(stdout, str):
        stdout = ""
    returncode = query.get("returncode")
    if type(returncode) is not int:
        returncode = -1
    duration = query.get("duration_seconds")
    if not isinstance(duration, (int, float)):
        duration = 0.0
    stderr_line_count = query.get("stderr_line_count")
    if type(stderr_line_count) is not int or stderr_line_count < 0:
        stderr_line_count = 0
    return (
        parse_guest_observation(stdout),
        {
            "ssh_returncode": returncode,
            "duration_seconds": round(float(duration), 3),
            "stderr_line_count": stderr_line_count,
        },
    )


def sanitized_identity_facts(guest: dict[str, object]) -> dict[str, object]:
    """Canonicalize the identity subset whose equality fences a double read."""
    facts: dict[str, object] = {}
    for key in _STABLE_SANITIZED_IDENTITY_FACT_KEYS:
        value = guest.get(key)
        if key == "loaded_uuids_observed":
            if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
                value = []
            else:
                value = sorted(set(value))
        facts[key] = value
    return facts


def double_read_facts(
    first_guest: dict[str, object],
    first_command: dict[str, object],
    second_guest: dict[str, object],
    second_command: dict[str, object],
) -> dict[str, object]:
    """Summarize two reduced probes without retaining either raw stdout."""
    first_succeeded = first_command.get("ssh_returncode") == 0
    second_succeeded = second_command.get("ssh_returncode") == 0
    first_parsed = first_guest.get("guest_observation_parsed") is True
    second_parsed = second_guest.get("guest_observation_parsed") is True
    stable = (
        first_parsed
        and second_parsed
        and sanitized_identity_facts(first_guest) == sanitized_identity_facts(second_guest)
    )
    first_build_matches = first_guest.get("os_build") == PINNED_QEMU_BUILD
    second_build_matches = second_guest.get("os_build") == PINNED_QEMU_BUILD
    return {
        "probe_count": 2,
        "inter_probe_delay_seconds": DOUBLE_READ_DELAY_SECONDS,
        "first_pinned_qemu_guest_query_succeeded": first_succeeded,
        "second_pinned_qemu_guest_query_succeeded": second_succeeded,
        "both_pinned_qemu_guest_queries_succeeded": first_succeeded and second_succeeded,
        "first_sanitized_guest_observation_parsed": first_parsed,
        "second_sanitized_guest_observation_parsed": second_parsed,
        "both_sanitized_guest_observations_parsed": first_parsed and second_parsed,
        "first_pinned_qemu_build_matches": first_build_matches,
        "second_pinned_qemu_build_matches": second_build_matches,
        "both_pinned_qemu_builds_match": first_build_matches and second_build_matches,
        "sanitized_installed_loaded_identity_stable": stable,
    }


def binding_result(
    expected: dict[str, Any],
    guest: dict[str, object],
    double_read: dict[str, object],
) -> dict[str, object]:
    loaded_uuids = guest.get("loaded_uuids_observed")
    if not isinstance(loaded_uuids, list) or not all(
        isinstance(value, str) for value in loaded_uuids
    ):
        loaded_uuids = []
    installed_uuid = guest.get("installed_macho_uuid")
    if not isinstance(installed_uuid, str):
        installed_uuid = ""
    checks = {
        "pinned_qemu_guest_double_probe_succeeded":
            double_read.get("both_pinned_qemu_guest_queries_succeeded") is True,
        "both_sanitized_guest_observations_parsed":
            double_read.get("both_sanitized_guest_observations_parsed") is True,
        "pinned_qemu_build_matches":
            double_read.get("both_pinned_qemu_builds_match") is True,
        "sanitized_installed_loaded_identity_stable":
            double_read.get("sanitized_installed_loaded_identity_stable") is True,
        "wifi_interface_present": guest.get("wifi_interface_present") is True,
        "installed_bundle_present": guest.get("installed_bundle_present") is True,
        "installed_bundle_id_matches_candidate":
            guest.get("installed_bundle_id") == expected["bundle_id"],
        "installed_info_plist_sha256_matches_candidate":
            guest.get("installed_info_plist_sha256") == expected["info_plist_sha256"],
        "installed_binary_sha256_matches_candidate":
            guest.get("installed_binary_sha256") == expected["binary_sha256"],
        "installed_macho_uuid_unambiguous":
            guest.get("installed_macho_uuid_unambiguous") is True,
        "installed_macho_uuid_matches_candidate": installed_uuid == expected["macho_uuid"],
        "kext_reported_loaded": guest.get("kext_reported_loaded") is True,
        "loaded_uuid_matches_installed": bool(installed_uuid) and installed_uuid in loaded_uuids,
        "loaded_uuid_matches_candidate": expected["macho_uuid"] in loaded_uuids,
    }
    failure_reasons = [name for name, passed in checks.items() if not passed]
    return {
        "checks": checks,
        "candidate_kext_bound": not failure_reasons,
        "failure_reasons": failure_reasons,
    }


def capture(
    candidate_receipt: Path,
    timeout_seconds: int,
    candidate_loader: Callable[[Path], dict[str, Any]] = load_typed_direct_candidate_receipt,
    guest_runner: Callable[[int], dict[str, object]] = run_pinned_qemu_guest,
    sleeper: Callable[[float], None] = time.sleep,
) -> dict[str, object]:
    expected = canonical_direct_candidate(candidate_loader(candidate_receipt))
    first_guest, first_command = reduce_guest_probe(guest_runner(timeout_seconds))
    sleeper(DOUBLE_READ_DELAY_SECONDS)
    second_guest, second_command = reduce_guest_probe(guest_runner(timeout_seconds))
    double_read = double_read_facts(
        first_guest, first_command, second_guest, second_command
    )
    binding = binding_result(expected, first_guest, double_read)
    return {
        "schema_version": SCHEMA_VERSION,
        "capture_kind": CAPTURE_KIND,
        "captured_at_utc": utc_now(),
        "capture_mode": "read-only-pinned-qemu-guest",
        "candidate_receipt_schema": RECEIPT_SCHEMA_VERSION,
        "candidate_receipt_kind": RECEIPT_KIND,
        "expected_local_lab_candidate": expected,
        "guest_observation": first_guest,
        "guest_double_read": double_read,
        "candidate_binding": binding,
        "command_result": {
            "read_only_probe_count": 2,
            "inter_probe_delay_seconds": DOUBLE_READ_DELAY_SECONDS,
            "first_probe": first_command,
            "second_probe": second_command,
            "guest_host_key_fingerprint": PINNED_QEMU_HOST_KEY_SHA256,
            "guest_command": "two read-only installed-and-loaded-kext identity queries",
            "raw_guest_stdout_retained": False,
            "raw_guest_stderr_retained": False,
        },
        "non_claims": {
            "release_tag_claimed": False,
            "candidate_kext_installed_by_capture": False,
            "candidate_kext_loaded_by_capture": False,
            "kext_unloaded_by_capture": False,
            "host_or_guest_rebooted": False,
            "network_configuration_changed": False,
            "association_tested": False,
            "authentication_tested": False,
            "dhcp_tested": False,
            "data_transfer_tested": False,
        },
        "verdict": {
            "ready_for_exact_local_lab_candidate_runtime_experiment":
                binding["candidate_kext_bound"],
            "candidate_runtime_test_performed": False,
        },
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
        raise ValueError("loaded identity output must be outside the source repository")
    if path.exists() or path.is_symlink():
        raise ValueError("loaded identity output must be a new non-symlink path")
    if not path.parent.is_dir():
        raise ValueError("loaded identity output parent is missing")
    path.write_text(rendered, encoding="utf-8")


def fixture_candidate() -> dict[str, Any]:
    return {
        "source_commit": "a" * 40,
        "source_identity_sha256": "b" * 64,
        "source_identity_paths_count": 7,
        "profile": LAB_PROFILE,
        "staged_kext_repo_path": LAB_STAGED_KEXT_REPO_PATH,
        "archive_sha256": "c" * 64,
        "info_plist_sha256": "d" * 64,
        "bundle_tree_sha256": "e" * 64,
        "binary_sha256": "f" * 64,
        "macho_uuid": "01234567-89AB-CDEF-0123-456789ABCDEF",
        "bundle_id": BUNDLE_ID,
        "bundle_version": "fixture-build",
        "short_version": "fixture-version",
        "trace_client_sha256": "1" * 64,
    }


def fixture_guest(candidate: dict[str, Any]) -> dict[str, object]:
    return {
        "os_build": PINNED_QEMU_BUILD,
        "wifi_interface": PINNED_QEMU_INTERFACE,
        "wifi_interface_present": True,
        "installed_bundle_present": True,
        "installed_bundle_id": candidate["bundle_id"],
        "installed_bundle_version": "fixture-build",
        "installed_short_version": "fixture-version",
        "installed_info_plist_sha256": candidate["info_plist_sha256"],
        "installed_binary_sha256": candidate["binary_sha256"],
        "installed_macho_uuid": candidate["macho_uuid"],
        "installed_macho_uuid_unambiguous": True,
        "kext_reported_loaded": True,
        "loaded_uuids_observed": [candidate["macho_uuid"]],
        "loaded_driver_line_count": 1,
        "guest_observation_parsed": True,
    }


def fixture_guest_stdout(candidate: dict[str, Any], guest_build: str = PINNED_QEMU_BUILD,
                         loaded_uuid: Optional[str] = None) -> str:
    """Build a credential-free fixture response for local double-read tests."""
    uuid_value = candidate["macho_uuid"] if loaded_uuid is None else loaded_uuid
    return f"""guest_build={guest_build}
__NETWORKSETUP_BEGIN__
Hardware Port: Wi-Fi
Device: {PINNED_QEMU_INTERFACE}
__NETWORKSETUP_END__
installed_bundle_present=true
installed_bundle_id={candidate["bundle_id"]}
installed_bundle_version=fixture
installed_short_version=fixture
installed_info_plist_sha256={candidate["info_plist_sha256"]}
installed_binary_sha256={candidate["binary_sha256"]}
__INSTALLED_UUID_BEGIN__
UUID: {candidate["macho_uuid"]} (x86_64) AirportItlwm
__INSTALLED_UUID_END__
__LOADED_BEGIN__
123 0x0 0x0 {candidate["bundle_id"]} ({uuid_value})
__LOADED_END__
"""


def self_test() -> int:
    if host_key_fingerprint() != PINNED_QEMU_HOST_KEY_SHA256:
        raise SystemExit("self-test: pinned QEMU host-key fingerprint does not match key")
    expected = canonical_direct_candidate(fixture_candidate())
    guest = fixture_guest(expected)
    successful_command = {
        "ssh_returncode": 0,
        "duration_seconds": 0.1,
        "stderr_line_count": 0,
    }
    stable = double_read_facts(guest, successful_command, guest, successful_command)
    if not binding_result(expected, guest, stable)["candidate_kext_bound"]:
        raise SystemExit("self-test: matching local candidate was not bound")
    missing_loaded_uuid = fixture_guest(expected)
    missing_loaded_uuid["loaded_uuids_observed"] = []
    failed = binding_result(
        expected,
        missing_loaded_uuid,
        double_read_facts(
            missing_loaded_uuid, successful_command, missing_loaded_uuid, successful_command
        ),
    )
    if failed["candidate_kext_bound"]:
        raise SystemExit("self-test: missing loaded UUID was accepted")
    if "loaded_uuid_matches_candidate" not in failed["failure_reasons"]:
        raise SystemExit("self-test: missing loaded UUID reason was not retained")
    wrong_build = fixture_guest(expected)
    wrong_build["os_build"] = "wrong-build"
    wrong_build_result = binding_result(
        expected,
        wrong_build,
        double_read_facts(wrong_build, successful_command, wrong_build, successful_command),
    )
    if wrong_build_result["candidate_kext_bound"]:
        raise SystemExit("self-test: unpinned QEMU build was accepted")
    if "pinned_qemu_build_matches" not in wrong_build_result["failure_reasons"]:
        raise SystemExit("self-test: wrong QEMU build reason was not retained")
    changed_second = fixture_guest(expected)
    changed_second["loaded_uuids_observed"] = []
    unstable = double_read_facts(guest, successful_command, changed_second, successful_command)
    if unstable["sanitized_installed_loaded_identity_stable"]:
        raise SystemExit("self-test: changed loaded identity was accepted as stable")
    malformed = parse_guest_observation("installed_bundle_present=true\n")
    if malformed["guest_observation_parsed"] is not False:
        raise SystemExit("self-test: malformed guest observation was accepted")

    with tempfile.TemporaryDirectory(prefix="aiam-iwn-loaded-identity-") as temp:
        temporary = Path(temp)
        receipt = temporary / "candidate-receipt.json"
        receipt.write_text("{}\n", encoding="utf-8")

        def fixture_loader(path: Path) -> dict[str, Any]:
            if path != receipt.resolve(strict=True):
                raise SystemExit("self-test: receipt path changed before loader")
            return fixture_candidate()

        events: list[str] = []
        outputs = [fixture_guest_stdout(expected), fixture_guest_stdout(expected)]

        def fixture_runner(timeout_seconds: int) -> dict[str, object]:
            if timeout_seconds != 11:
                raise SystemExit("self-test: timeout changed before guest runner")
            if not outputs:
                raise SystemExit("self-test: double probe requested too many guest queries")
            events.append("probe")
            return {
                "returncode": 0,
                "stdout": outputs.pop(0),
                "duration_seconds": 0.1,
                "stderr_line_count": 0,
            }

        def fixture_sleep(delay_seconds: float) -> None:
            if delay_seconds != DOUBLE_READ_DELAY_SECONDS:
                raise SystemExit("self-test: double-read delay changed")
            events.append("delay")

        document = capture(
            receipt, 11, fixture_loader, fixture_runner, fixture_sleep
        )
        if not document["candidate_binding"]["candidate_kext_bound"]:
            raise SystemExit("self-test: matching double-read candidate was not bound")
        if events != ["probe", "delay", "probe"]:
            raise SystemExit("self-test: double-read order changed")
        if document["guest_double_read"]["sanitized_installed_loaded_identity_stable"] is not True:
            raise SystemExit("self-test: stable double-read fact was not retained")
        if document["command_result"]["raw_guest_stdout_retained"] is not False:
            raise SystemExit("self-test: guest stdout retention claim changed")
    print("PASS: Tahoe IWN lab loaded identity self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-receipt", type=Path)
    parser.add_argument("--output", default="-")
    parser.add_argument("--timeout-seconds", type=int, default=45)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.candidate_receipt is None:
        parser.error("--candidate-receipt is required unless --self-test is used")
    if not 1 <= args.timeout_seconds <= 120:
        parser.error("--timeout-seconds must be in 1..120")
    try:
        root = repository_root()
        document = capture(args.candidate_receipt, args.timeout_seconds)
        write_new_json(root, document, args.output)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: read-only IWN lab loaded identity capture: {error}", file=sys.stderr)
        return 2
    return 0 if document["candidate_binding"]["candidate_kext_bound"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
