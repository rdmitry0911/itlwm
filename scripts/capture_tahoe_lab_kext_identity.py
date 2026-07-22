#!/usr/bin/env python3
"""Read-only binding of a Tahoe lab guest to an exact release kext artifact.

This is deliberately a precondition capture.  It never installs, loads,
unloads, associates, changes networking, or reboots the guest.  A false
binding is a useful result: it prevents an old loaded driver from being
credited with a candidate runtime result.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import json
import plistlib
import re
import struct
import subprocess
import sys
import tempfile
import time
import uuid
import zipfile
from pathlib import Path

from create_tahoe_candidate_provenance import (
    bind_candidate_provenance,
    load_candidate_provenance,
)


SCHEMA_VERSION = "itlwm-tahoe-lab-kext-identity-binding/v2"
PINNED_GUEST = "devops@127.0.0.1"
PINNED_PORT = 3322
PINNED_INTERFACE = "en1"
PINNED_KEXT_PATH = "/Library/Extensions/AirportItlwm.kext"
PINNED_HOST_KEY = (
    "[127.0.0.1]:3322 ssh-ed25519 "
    "AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
)
PINNED_HOST_KEY_SHA256 = "SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"
BUNDLE_ID = "com.zxystd.AirportItlwm"
ZIP_INFO = "AirportItlwm.kext/Contents/Info.plist"
ZIP_BINARY = "AirportItlwm.kext/Contents/MacOS/AirportItlwm"
LC_UUID = 0x1B
MACHO_64_LE_MAGIC = 0xFEEDFACF


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def host_key_fingerprint() -> str:
    fields = PINNED_HOST_KEY.split()
    if len(fields) != 3:
        raise ValueError("pinned known-hosts entry is malformed")
    encoded_key = fields[2]
    key_blob = base64.b64decode(encoded_key + "=" * (-len(encoded_key) % 4))
    digest = base64.b64encode(hashlib.sha256(key_blob).digest()).decode("ascii")
    return "SHA256:" + digest.rstrip("=")


def macho_uuid(binary: bytes) -> str:
    """Return the LC_UUID from a thin little-endian 64-bit Mach-O binary."""
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


def release_identity(release_zip: Path, release_tag: str) -> dict[str, str]:
    archive = release_zip.read_bytes()
    with zipfile.ZipFile(release_zip) as bundle:
        names = set(bundle.namelist())
        missing = [name for name in (ZIP_INFO, ZIP_BINARY) if name not in names]
        if missing:
            raise ValueError("release archive lacks required AirportItlwm.kext files")
        info = plistlib.loads(bundle.read(ZIP_INFO))
        binary = bundle.read(ZIP_BINARY)
    bundle_id = plist_string(info.get("CFBundleIdentifier"))
    if bundle_id != BUNDLE_ID:
        raise ValueError(f"unexpected kext bundle identifier: {bundle_id or 'missing'}")
    return {
        "source": "local_release_zip",
        "release_tag": release_tag,
        "archive_sha256": sha256(archive),
        "bundle_id": bundle_id,
        "bundle_version": plist_string(info.get("CFBundleVersion")),
        "short_version": plist_string(info.get("CFBundleShortVersionString")),
        "binary_sha256": sha256(binary),
        "macho_uuid": macho_uuid(binary),
    }


def release_identity_from_candidate_provenance(
    release_zip: Path, provenance_path: Path
) -> dict[str, str]:
    provenance = load_candidate_provenance(provenance_path)
    archive = release_identity(release_zip, str(provenance["release_tag"]))
    return bind_candidate_provenance(provenance, archive)


def guest_probe_script() -> str:
    script = r'''
set -u
p="__PINNED_KEXT_PATH__"
bin="$p/Contents/MacOS/AirportItlwm"
printf 'guest_build=%s\n' "$(sw_vers -buildVersion 2>/dev/null || true)"
printf '__NETWORKSETUP_BEGIN__\n'
networksetup -listallhardwareports 2>/dev/null || true
printf '__NETWORKSETUP_END__\n'
if [ -d "$p" ] && [ -f "$bin" ]; then
  printf 'installed_bundle_present=true\n'
else
  printf 'installed_bundle_present=false\n'
fi
printf 'installed_bundle_id=%s\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$p/Contents/Info.plist" 2>/dev/null || true)"
printf 'installed_bundle_version=%s\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$p/Contents/Info.plist" 2>/dev/null || true)"
printf 'installed_short_version=%s\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$p/Contents/Info.plist" 2>/dev/null || true)"
printf 'installed_binary_sha256=%s\n' "$(shasum -a 256 "$bin" 2>/dev/null | awk '{print $1}')"
printf '__INSTALLED_UUID_BEGIN__\n'
dwarfdump --uuid "$bin" 2>/dev/null || true
printf '__INSTALLED_UUID_END__\n'
printf '__LOADED_BEGIN__\n'
kextstat 2>/dev/null | grep -i 'AirportItlwm\|itlwm' || true
kmutil showloaded 2>/dev/null | grep -i 'AirportItlwm\|itlwm' || true
printf '__LOADED_END__\n'
'''
    return script.replace("__PINNED_KEXT_PATH__", PINNED_KEXT_PATH)


def run_pinned_guest(timeout_seconds: int) -> dict[str, object]:
    known_hosts = tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", prefix="aiam-pinned-known-hosts-", delete=False
    )
    known_hosts.write(PINNED_HOST_KEY + "\n")
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
            str(PINNED_PORT),
            PINNED_GUEST,
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


def section(output: str, begin: str, end: str) -> str:
    if begin not in output or end not in output:
        return ""
    return output.split(begin, 1)[1].split(end, 1)[0]


def key_values(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def first_uuid(text: str) -> str:
    match = re.search(
        r"\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
        r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\b",
        text,
    )
    return match.group(0).upper() if match else ""


def unique_uuids(text: str) -> list[str]:
    values: list[str] = []
    for value in re.findall(
        r"\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
        r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\b",
        text,
    ):
        normalized = value.upper()
        if normalized not in values:
            values.append(normalized)
    return values


def interface_present(networksetup_output: str) -> bool:
    return bool(
        re.search(r"(?im)^\s*Device:\s*" + re.escape(PINNED_INTERFACE) + r"\s*$",
                  networksetup_output)
    )


def parse_guest_observation(output: str) -> dict[str, object]:
    values = key_values(output)
    loaded = section(output, "__LOADED_BEGIN__", "__LOADED_END__")
    loaded_driver_lines = [
        line for line in loaded.splitlines()
        if re.search(r"(?i)\bAirportItlwm\b|\bitlwm\b", line)
    ]
    return {
        "os_build": values.get("guest_build", "unknown"),
        "wifi_interface": PINNED_INTERFACE,
        "wifi_interface_present": interface_present(
            section(output, "__NETWORKSETUP_BEGIN__", "__NETWORKSETUP_END__")
        ),
        "installed_bundle_present": values.get("installed_bundle_present") == "true",
        "installed_bundle_id": values.get("installed_bundle_id", ""),
        "installed_bundle_version": values.get("installed_bundle_version", ""),
        "installed_short_version": values.get("installed_short_version", ""),
        "installed_binary_sha256": values.get("installed_binary_sha256", ""),
        "installed_macho_uuid": first_uuid(
            section(output, "__INSTALLED_UUID_BEGIN__", "__INSTALLED_UUID_END__")
        ),
        "kext_reported_loaded": bool(loaded_driver_lines),
        "loaded_uuids_observed": unique_uuids("\n".join(loaded_driver_lines)),
        "loaded_driver_line_count": len(loaded_driver_lines),
    }


def binding_result(
    expected: dict[str, str], guest: dict[str, object], ssh_succeeded: bool
) -> dict[str, object]:
    loaded_uuids = guest["loaded_uuids_observed"]
    installed_uuid = guest["installed_macho_uuid"]
    checks = {
        "pinned_guest_query_succeeded": ssh_succeeded,
        "wifi_interface_present": guest["wifi_interface_present"] is True,
        "installed_bundle_present": guest["installed_bundle_present"] is True,
        "installed_bundle_id_matches_release":
            guest["installed_bundle_id"] == expected["bundle_id"],
        "installed_binary_sha256_matches_release":
            guest["installed_binary_sha256"] == expected["binary_sha256"],
        "installed_macho_uuid_matches_release":
            installed_uuid == expected["macho_uuid"],
        "kext_reported_loaded": guest["kext_reported_loaded"] is True,
        "loaded_uuid_matches_installed":
            bool(installed_uuid) and installed_uuid in loaded_uuids,
        "loaded_uuid_matches_release":
            expected["macho_uuid"] in loaded_uuids,
    }
    failure_reasons = [name for name, passed in checks.items() if not passed]
    return {
        "checks": checks,
        "candidate_kext_bound": not failure_reasons,
        "failure_reasons": failure_reasons,
    }


def capture(
    release_zip: Path, candidate_provenance: Path, timeout_seconds: int
) -> dict[str, object]:
    expected = release_identity_from_candidate_provenance(
        release_zip, candidate_provenance
    )
    query = run_pinned_guest(timeout_seconds)
    guest = parse_guest_observation(str(query["stdout"]))
    binding = binding_result(expected, guest, query["returncode"] == 0)
    return {
        "schema_version": SCHEMA_VERSION,
        "captured_at_utc": utc_now(),
        "capture_mode": "read-only-pinned-guest",
        "expected_release": expected,
        "guest_observation": guest,
        "candidate_binding": binding,
        "command_result": {
            "ssh_returncode": query["returncode"],
            "duration_seconds": query["duration_seconds"],
            "stderr_line_count": query["stderr_line_count"],
            "guest_host_key_fingerprint": PINNED_HOST_KEY_SHA256,
            "guest_command": "read-only installed-and-loaded-kext identity query",
            "raw_guest_stdout_retained": False,
            "raw_guest_stderr_retained": False,
        },
        "non_claims": {
            "candidate_kext_installed_by_capture": False,
            "candidate_kext_loaded_by_capture": False,
            "kext_unloaded_by_capture": False,
            "host_or_guest_rebooted": False,
            "association_tested": False,
            "authentication_tested": False,
            "dhcp_tested": False,
            "data_transfer_tested": False,
        },
        "verdict": {
            "ready_for_exact_candidate_runtime_experiment":
                binding["candidate_kext_bound"],
            "candidate_runtime_test_performed": False,
        },
    }


def write_json(data: dict[str, object], destination: str) -> None:
    rendered = json.dumps(data, indent=2, sort_keys=True) + "\n"
    if destination == "-":
        sys.stdout.write(rendered)
        return
    path = Path(destination)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(rendered, encoding="utf-8")


def self_test() -> int:
    expected_uuid = "01234567-89AB-CDEF-0123-456789ABCDEF"
    if host_key_fingerprint() != PINNED_HOST_KEY_SHA256:
        raise SystemExit("self-test: pinned host-key fingerprint does not match key")
    header = struct.pack("<IiiIIIII", MACHO_64_LE_MAGIC, 0, 0, 0, 1, 24, 0, 0)
    command = struct.pack("<II", LC_UUID, 24) + uuid.UUID(expected_uuid).bytes
    fixture_binary = header + command
    if macho_uuid(fixture_binary) != expected_uuid:
        raise SystemExit("self-test: LC_UUID parser did not preserve UUID")
    fixture_info = plistlib.dumps(
        {
            "CFBundleIdentifier": BUNDLE_ID,
            "CFBundleVersion": "fixture-build",
            "CFBundleShortVersionString": "fixture-version",
        }
    )
    with tempfile.TemporaryDirectory(prefix="aiam-kext-identity-fixture-") as temp:
        archive_path = Path(temp) / "fixture.zip"
        with zipfile.ZipFile(archive_path, "w") as archive:
            archive.writestr(ZIP_INFO, fixture_info)
            archive.writestr(ZIP_BINARY, fixture_binary)
        provenance_path = Path(temp) / "fixture-provenance.json"
        provenance_path.write_text(json.dumps({
            "schema": "itlwm-tahoe-candidate-provenance/v1",
            "candidate": {
                "source_commit": "a" * 40,
                "source_identity_sha256": "b" * 64,
                "source_identity_paths_count": 1,
                "release_tag": "v2.4.0-alpha",
                "archive_sha256": sha256(archive_path.read_bytes()),
                "binary_sha256": sha256(fixture_binary),
                "macho_uuid": expected_uuid,
                "bundle_id": BUNDLE_ID,
            },
        }), encoding="utf-8")
        parsed_fixture = release_identity_from_candidate_provenance(
            archive_path, provenance_path
        )
        iwx_provenance_path = Path(temp) / "fixture-iwx-provenance.json"
        iwx_provenance_path.write_text(json.dumps({
            "schema": "itlwm-tahoe-candidate-provenance/v2",
            "candidate": {
                "source_commit": "a" * 40,
                "source_identity_sha256": "b" * 64,
                "source_identity_paths_count": 1,
                "release_tag": "v2.4.0-alpha",
                "archive_sha256": sha256(archive_path.read_bytes()),
                "binary_sha256": sha256(fixture_binary),
                "macho_uuid": expected_uuid,
                "bundle_id": BUNDLE_ID,
                "trace_client_sha256": "c" * 64,
            },
        }), encoding="utf-8")
        parsed_iwx_fixture = release_identity_from_candidate_provenance(
            archive_path, iwx_provenance_path
        )
    if parsed_fixture["macho_uuid"] != expected_uuid:
        raise SystemExit("self-test: release archive UUID did not round-trip")
    if parsed_fixture["binary_sha256"] != sha256(fixture_binary):
        raise SystemExit("self-test: release archive hash did not round-trip")
    if parsed_fixture["source_commit"] != "a" * 40:
        raise SystemExit("self-test: provenance source commit did not bind")
    if parsed_iwx_fixture["source_identity_sha256"] != "b" * 64:
        raise SystemExit("self-test: IWX v2 provenance was not accepted")
    expected = {
        "bundle_id": BUNDLE_ID,
        "binary_sha256": "a" * 64,
        "macho_uuid": expected_uuid,
    }
    guest = {
        "wifi_interface_present": True,
        "installed_bundle_present": True,
        "installed_bundle_id": BUNDLE_ID,
        "installed_binary_sha256": "a" * 64,
        "installed_macho_uuid": expected_uuid,
        "kext_reported_loaded": True,
        "loaded_uuids_observed": [expected_uuid],
    }
    if not binding_result(expected, guest, True)["candidate_kext_bound"]:
        raise SystemExit("self-test: matching identity was not bound")
    guest["loaded_uuids_observed"] = []
    result = binding_result(expected, guest, True)
    if result["candidate_kext_bound"]:
        raise SystemExit("self-test: missing loaded UUID was accepted")
    if "loaded_uuid_matches_release" not in result["failure_reasons"]:
        raise SystemExit("self-test: missing loaded UUID reason was not retained")
    print("PASS: Tahoe lab kext identity capture self-test")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-release-zip", type=Path)
    parser.add_argument("--candidate-provenance", type=Path)
    parser.add_argument("--output", default="-")
    parser.add_argument("--timeout-seconds", type=int, default=45)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.expected_release_zip is None or args.candidate_provenance is None:
        parser.error("--expected-release-zip and --candidate-provenance are required unless --self-test is used")
    try:
        data = capture(args.expected_release_zip, args.candidate_provenance,
                       args.timeout_seconds)
    except (OSError, ValueError, subprocess.TimeoutExpired, zipfile.BadZipFile) as error:
        print(f"FAIL: read-only identity capture: {error}", file=sys.stderr)
        return 2
    write_json(data, args.output)
    return 0 if data["candidate_binding"]["candidate_kext_bound"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
