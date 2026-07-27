#!/usr/bin/env python3
"""Run one bounded public-CoreWLAN same-ESS recovery observation.

The supervisor never accepts a wireless name, BSSID, password, profile, or
other credential as an argument.  Its standard input must already be a FIFO;
the descriptor is handed directly to the bounded native broker and is never
read by Python.  The only target values that cross this process boundary are
fixed-width SHA-256 values returned by the host's hash-only LabAP status.

This is intentionally a one-shot laboratory controller.  It binds the exact
candidate, public-helper, loaded-kext, and sidecar-stage receipts before it
touches the host or guest.  It runs one helper process, accepts only its
positive aggregate sequence, performs one host withdrawal only after the
helper has armed it, and restores then retires the temporary host state on
every terminal route that has a verified rollback.
"""

from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import hashlib
import json
import math
import os
import re
import selectors
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional


ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    # The entrypoint uses Python isolated mode.  Append only the repository's
    # fixed scripts directory, after the standard-library search paths, rather
    # than allowing this runner to shadow arbitrary import locations.
    sys.path.append(str(SCRIPTS))

from capture_tahoe_iwn_lab_candidate_receipt import (  # noqa: E402
    load_direct_runtime_candidate_receipt,
)
from capture_tahoe_iwn_lab_loaded_identity import (  # noqa: E402
    CAPTURE_KIND as LOADED_IDENTITY_CAPTURE_KIND,
    DOUBLE_READ_DELAY_SECONDS as LOADED_IDENTITY_DOUBLE_READ_DELAY_SECONDS,
    PINNED_QEMU_BUILD,
    PINNED_QEMU_GUEST,
    PINNED_QEMU_HOST_KEY,
    PINNED_QEMU_HOST_KEY_SHA256,
    PINNED_QEMU_PORT,
    RECEIPT_KIND as CANDIDATE_RECEIPT_KIND,
    RECEIPT_SCHEMA_VERSION as CANDIDATE_RECEIPT_SCHEMA,
    SCHEMA_VERSION as LOADED_IDENTITY_SCHEMA,
    canonical_direct_candidate,
    capture as capture_loaded_identity,
    guest_probe_script,
)
from capture_tahoe_iwn_lab_public_recovery_receipt import (  # noqa: E402
    load_public_recovery_receipt,
)
from tahoe_source_identity import source_identity_domain, source_paths  # noqa: E402


RUNTIME_SCHEMA = "itlwm-tahoe-iwn-public-recovery-runtime/v1"
STAGE_SCHEMA = "itlwm-tahoe-iwn-public-recovery-stage-attestation/v2"
BROKER_PROTOCOL = "tahoe-lab-credential-broker/v1"
LABAP_SWITCHER = ROOT / "scripts" / "tahoe_labap_bss_switcher.sh"
STATE_PREFIX = "/tmp/aiam-labap-bss-switch."
GUEST_STAGE_PREFIX = "/private/tmp/aiam-iwn-public-recovery-"
HELPER_NAME = "airport_itlwm_lab_public_recovery"
PUBLIC_RECEIPT_NAME = "iwn-public-recovery-receipt-v1.json"
BROKER_SOURCE_RELATIVE = "AirportItlwmLabPublicRecovery/airport_itlwm_lab_credential_broker.c"
BROKER_BINARY_NAME = "airport_itlwm_lab_credential_broker"
BROKER_SOURCE_COPY_NAME = "airport_itlwm_lab_credential_broker.c"
BROKER_BUILD_PREFIX = "aiam-public-recovery-broker-"
BROKER_COMPILER = "/usr/bin/cc"
BROKER_BUILD_FLAGS = (
    "-std=c11", "-O2", "-D_FORTIFY_SOURCE=2", "-fstack-protector-strong",
    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
)

LEASE_SECONDS = 300
# This pre-secret wait covers only read-only admission/topology scans.  It is
# deliberately longer than the later secret-retention windows so RF variance
# cannot cause an artificial activation failure before the broker exists.
HOST_CREDENTIAL_READY_TIMEOUT_SECONDS = 90
HOST_CREDENTIAL_READY = b"LABAP_BSS_CREDENTIAL_READY=1\n"
# The nonsecret SETUP_STARTED acknowledgement is emitted only after the
# switcher consumed the host pipe and armed its exact 180-second v4 setup
# deadline with durable rollback ownership.  Rebase the active/start path
# there; HOST_FED remains a larger native retention ceiling covering the
# preceding 45-second read plus 15-second setup preparation.
HOST_SETUP_STARTED_TIMEOUT_SECONDS = 60
HOST_SETUP_STARTED = b"LABAP_BSS_SETUP_STARTED=1\n"
HOST_ACTIVATION_TIMEOUT_SECONDS = 185
SETUP_STARTED_TO_START_BUDGET_SECONDS = 220
HOST_FED_TO_START_BUDGET_SECONDS = 280
BROKER_HOST_FED_CAP_SECONDS = 290
# The native broker starts this outer cap when it sends HOST_FED, while this
# controller observes that packet slightly later.  Keep an explicit ten-second
# native-to-controller handoff fence rather than treating the two origins as
# identical.
BROKER_HOST_FED_HANDOFF_MARGIN_SECONDS = 10
# FIFO credential read (15), host-pipe feed (15), and HOST_FED send (15) each
# have native bounds.  Keep another full fifteen seconds for scheduler/pipe
# handoff variance rather than making the public timeout their exact sum.
HOST_FED_TIMEOUT_SECONDS = 60
# Each control timeout is one absolute end-to-end budget: local SOCK_SEQPACKET
# send/sendmsg plus the matching native acknowledgement.  Neither half may
# consume a second full phase window.
BROKER_START_CONTROL_TIMEOUT_SECONDS = 20
BROKER_CONTROL_TIMEOUT_SECONDS = 20
BROKER_START_SAFETY_SECONDS = 5
BROKER_STARTED_TO_ARM_CAP_SECONDS = 145
BROKER_ARMED_TO_RELEASE_CAP_SECONDS = 110
# A successful START begins the native broker's independent 270-second cap.
BROKER_POST_START_CAP_SECONDS = 270
START_TO_ARM_TIMEOUT_SECONDS = 115
ARM_TO_WITHDRAW_TIMEOUT_SECONDS = 15
HOST_RENEW_FOR_WITHDRAW_TIMEOUT_SECONDS = 10
HOST_STATUS_TIMEOUT_SECONDS = 10
HOST_WITHDRAW_TIMEOUT_SECONDS = 25
# The public helper and broker both allow this bounded post-arm control window.
HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS = 90
# The helper/runner begin their 90-second control window after receiving the
# ARMED acknowledgement.  The native broker began first, so reserve a
# separate bounded acknowledgement-handoff margin in its 110-second cap.
ARMED_NATIVE_ACK_HANDOFF_MARGIN_SECONDS = 20
ARMED_PHASE_SAFETY_SECONDS = 5
POST_ARM_RELEASE_PATH_SECONDS = (
    HOST_RENEW_FOR_WITHDRAW_TIMEOUT_SECONDS + HOST_STATUS_TIMEOUT_SECONDS +
    HOST_WITHDRAW_TIMEOUT_SECONDS + BROKER_CONTROL_TIMEOUT_SECONDS
)
HELPER_RECOVERY_TIMEOUT_SECONDS = 125
PROCESS_CLEANUP_TIMEOUT_SECONDS = 20
WATCHDOG_HANDOFF_RESERVE_SECONDS = 15
ROLLBACK_RESTORE_TIMEOUT_SECONDS = 180
# If a foreground rollback loses the switch lock before the renewed lease
# expires, the watchdog may still wait the remaining full lease and then run
# one legal LAR-backed restore.  The proof poll covers that complete bounded
# handoff rather than assuming the race has already reached restoration.
WATCHDOG_RETIRE_PROOF_TIMEOUT_SECONDS = (
    LEASE_SECONDS + ROLLBACK_RESTORE_TIMEOUT_SECONDS + PROCESS_CLEANUP_TIMEOUT_SECONDS
)
WATCHDOG_RETIRE_POLL_SECONDS = 2
# A watchdog that just completed restore clears its marker before it clears
# its pid receipt.  Retire is the only proof for that narrow terminal race;
# retry it briefly before classifying an otherwise ambiguous owner receipt.
COMPLETED_WATCHDOG_RETIRE_RACE_TIMEOUT_SECONDS = 25
# The active lease starts only at promotion.  Both status observations demand
# a visibly fresh receipt, not merely a remainder large enough for the next
# protocol phase.
MINIMUM_INITIAL_LEASE_REMAINING_SECONDS = 285
MINIMUM_WITHDRAW_LEASE_REMAINING_SECONDS = (
    HOST_WITHDRAW_TIMEOUT_SECONDS + BROKER_CONTROL_TIMEOUT_SECONDS +
    HELPER_RECOVERY_TIMEOUT_SECONDS + WATCHDOG_HANDOFF_RESERVE_SECONDS
)
MINIMUM_RENEWED_LEASE_REMAINING_SECONDS = 285
HELPER_GRACEFUL_CLEANUP_TIMEOUT_SECONDS = 130
REMOTE_COMMAND_TIMEOUT_SECONDS = 20
MAX_HELPER_LINE_BYTES = 2048
MAX_HOST_ACTIVATION_LINE_BYTES = 256
FRESH_LOADED_IDENTITY_TIMEOUT_SECONDS = 20

SHA256_RE = re.compile(r"[0-9a-f]{64}")
COMMIT_RE = re.compile(r"[0-9a-f]{40}")
UUID_RE = re.compile(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}")
TOKEN_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
CANONICAL_DECIMAL_RE = re.compile(r"(?:0|[1-9][0-9]*)")
PREFLIGHT_RE = re.compile(
    rb"^LABAP_BSS_PREFLIGHT=PASS external_bss_count=([1-9][0-9]*) external_band_count=2\n$"
)
ACTIVE_RE = re.compile(
    rb"^LABAP_BSS_SWITCH=ACTIVE external_bss_count=([1-9][0-9]*) external_band_count=2\n$"
)
STATUS_RE = re.compile(
    rb"^LABAP_BSS_STATUS schema=tahoe-labap-bss-status/v1 active=1 "
    rb"target_ssid_sha256=([0-9a-f]{64}) target_bssid_sha256=([0-9a-f]{64}) "
    rb"lease_seconds=([1-9][0-9]*) lease_remaining_seconds=([1-9][0-9]*)\n$"
)
HELPER_POSITIVE_STATES = frozenset({"initial-ready", "withdraw-armed", "recovered"})
HELPER_FAILURE_STATES = frozenset({
    "airport-itlwm-bsd-unresolved",
    "airport-itlwm-service-unavailable",
    "corewlan-input-unavailable",
    "credential-input-invalid",
    "initial-identity-timeout",
    "initial-or-alternate-target-unavailable",
    "interface-unavailable",
    "not-started",
    "post-association-alternate-unavailable",
    "post-scan-initial-identity-lost",
    "pre-withdrawal-identity-lost",
    "public-association-failed",
    "recovery-timeout",
    "target-input-invalid",
    "usage",
    "withdrawal-arm-rejected",
    "withdrawal-control-rejected",
})
HELPER_RESULT_PATTERN = "|".join(sorted(HELPER_POSITIVE_STATES | HELPER_FAILURE_STATES))
HELPER_RE = re.compile(
    rf"^public_corewlan_recovery=({HELPER_RESULT_PATTERN}) "
    r"endpoint_binding=(airport-itlwm-bsd|unresolved) "
    r"discovery_attempts=([0-9]+) "
    r"matching_records=([0-9]+) "
    r"alternate_bss_count=([0-9]+) "
    r"alternate_band_count=([0-9]+) "
    r"alternate_ready=([01]) "
    r"scan_error_present=([01]) "
    r"association_error_present=([01]) "
    r"initial_identity_exact=([01]) "
    r"withdrawal_arm_accepted=([01]) "
    r"pre_withdrawal_identity_exact=([01]) "
    r"withdrawal_control_accepted=([01]) "
    r"recovery_same_ssid=([01]) "
    r"recovery_different_bss=([01]) "
    r"cleanup_disassociate_attempted=([01])\n$"
)


class RunnerError(Exception):
    """A categorical, non-sensitive failure intended for the aggregate receipt."""

    def __init__(self, phase: str):
        super().__init__(phase)
        self.phase = phase


def require(condition: bool, phase: str) -> None:
    if not condition:
        raise RunnerError(phase)


def clean_environment() -> dict[str, str]:
    return {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL": "C"}


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_absolute_regular(path: Path, phase: str, executable: bool = False) -> Path:
    if not path.is_absolute():
        raise RunnerError(phase)
    try:
        metadata = path.lstat()
    except OSError as error:
        raise RunnerError(phase) from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise RunnerError(phase)
    if executable and metadata.st_mode & 0o111 == 0:
        raise RunnerError(phase)
    try:
        return path.resolve(strict=True)
    except OSError as error:
        raise RunnerError(phase) from error


def write_private_bytes(path: Path, payload: bytes, phase: str) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags, 0o600)
    except OSError as error:
        raise RunnerError(phase) from error
    try:
        offset = 0
        while offset < len(payload):
            written = os.write(descriptor, payload[offset:])
            if written <= 0:
                raise OSError("short write")
            offset += written
        os.fsync(descriptor)
    except OSError as error:
        raise RunnerError(phase) from error
    finally:
        os.close(descriptor)


def require_fifo_stdin() -> int:
    """Duplicate stdin without consuming one byte of its credential record."""
    try:
        descriptor = sys.stdin.fileno()
        metadata = os.fstat(descriptor)
    except OSError as error:
        raise RunnerError("stdin-descriptor") from error
    if not stat.S_ISFIFO(metadata.st_mode) or os.isatty(descriptor):
        raise RunnerError("stdin-not-fifo")
    try:
        return os.dup(descriptor)
    except OSError as error:
        raise RunnerError("stdin-duplicate") from error


def no_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def load_json(path: Path, phase: str) -> dict[str, object]:
    safe = require_absolute_regular(path, phase)
    try:
        data = safe.read_bytes()
        document = json.loads(data.decode("utf-8"), object_pairs_hook=no_duplicate_keys)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        raise RunnerError(phase) from error
    if not isinstance(document, dict):
        raise RunnerError(phase)
    return document


def require_exact_keys(document: dict[str, object], expected: set[str], phase: str) -> None:
    if set(document) != expected:
        raise RunnerError(phase)


def require_digest(value: object, phase: str) -> str:
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise RunnerError(phase)
    return value


def require_commit(value: object, phase: str) -> str:
    if not isinstance(value, str) or COMMIT_RE.fullmatch(value) is None:
        raise RunnerError(phase)
    return value


def require_uuid(value: object, phase: str) -> str:
    if not isinstance(value, str) or UUID_RE.fullmatch(value) is None:
        raise RunnerError(phase)
    return value


def require_positive_int(value: object, phase: str) -> int:
    if type(value) is not int or value < 1:
        raise RunnerError(phase)
    return value


def require_token(value: object, phase: str) -> str:
    if not isinstance(value, str) or TOKEN_RE.fullmatch(value) is None:
        raise RunnerError(phase)
    return value


def require_all_true(section: object, expected: set[str], phase: str) -> None:
    if not isinstance(section, dict) or set(section) != expected:
        raise RunnerError(phase)
    if any(value is not True for value in section.values()):
        raise RunnerError(phase)


def require_all_false(section: object, expected: set[str], phase: str) -> None:
    if not isinstance(section, dict) or set(section) != expected:
        raise RunnerError(phase)
    if any(value is not False for value in section.values()):
        raise RunnerError(phase)


@dataclass(frozen=True)
class BoundArtifacts:
    helper_sha256: str
    helper_macho_uuid: str
    public_receipt_sha256: str
    guest_stage_token: str

    @property
    def guest_stage_path(self) -> str:
        return f"{GUEST_STAGE_PREFIX}{self.guest_stage_token}"


@dataclass(frozen=True)
class HostTarget:
    ssid_sha256: str
    bssid_sha256: str
    remaining_seconds: int


@dataclass
class RuntimeState:
    candidate_public_bound: bool = False
    candidate_loaded_bound: bool = False
    fresh_loaded_identity_bound: bool = False
    public_stage_bound: bool = False
    guest_helper_reverified: bool = False
    current_source_candidate_bound: bool = False
    broker_materialized_from_head: bool = False
    stdin_fifo_verified: bool = False
    host_preflight_passed: bool = False
    host_credential_ready: bool = False
    host_setup_started: bool = False
    host_activated: bool = False
    host_status_initial_verified: bool = False
    host_status_before_withdraw_verified: bool = False
    initial_lease_sufficient: bool = False
    withdraw_lease_sufficient: bool = False
    host_lease_renewed_after_helper_arm: bool = False
    host_renewed_lease_fresh: bool = False
    host_withdraw_called_after_helper_armed: bool = False
    host_withdrawn_verified: bool = False
    host_rollback_attempted: bool = False
    host_rollback_verified: bool = False
    host_watchdog_rollback_verified: bool = False
    host_retire_attempted: bool = False
    host_retired_verified: bool = False
    broker_host_fed: bool = False
    broker_started: bool = False
    broker_armed: bool = False
    broker_released: bool = False
    broker_aborted: bool = False
    broker_exited_zero: bool = False
    helper_initial_ready: bool = False
    helper_withdraw_armed: bool = False
    helper_recovered: bool = False
    helper_positive_sequence_verified: bool = False
    helper_exited_zero: bool = False
    helper_cleanup_closed: bool = False
    activation_attempted: bool = False
    failure_phase: str = "preflight"
    result: str = "INCONCLUSIVE"


@dataclass
class Config:
    candidate_receipt: Path
    public_receipt: Path
    loaded_identity: Path
    stage_report: Path
    output: Path


def canonical_candidate(path: Path) -> tuple[dict[str, object], str]:
    safe = require_absolute_regular(path, "candidate-receipt")
    try:
        candidate = canonical_direct_candidate(load_direct_runtime_candidate_receipt(safe))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise RunnerError("candidate-receipt") from error
    if not isinstance(candidate, dict):
        raise RunnerError("candidate-receipt")
    required = {
        "source_commit", "source_identity_sha256", "source_identity_paths_count",
        "profile", "staged_kext_repo_path", "archive_sha256", "info_plist_sha256",
        "bundle_tree_sha256", "binary_sha256", "macho_uuid", "bundle_id",
        "trace_client_sha256",
    }
    if set(candidate) != required:
        raise RunnerError("candidate-receipt")
    return candidate, sha256_file(safe)


LOADED_IDENTITY_GUEST_KEYS = {
    "os_build", "installed_bundle_present", "installed_bundle_id",
    "installed_bundle_version", "installed_short_version",
    "installed_info_plist_sha256", "installed_binary_sha256",
    "installed_macho_uuid", "installed_macho_uuid_unambiguous",
    "kext_reported_loaded", "loaded_uuids_observed", "loaded_driver_line_count",
    "guest_observation_parsed",
}
LOADED_IDENTITY_DOUBLE_READ_KEYS = {
    "probe_count", "inter_probe_delay_seconds",
    "first_pinned_qemu_guest_query_succeeded",
    "second_pinned_qemu_guest_query_succeeded",
    "both_pinned_qemu_guest_queries_succeeded",
    "first_sanitized_guest_observation_parsed",
    "second_sanitized_guest_observation_parsed",
    "both_sanitized_guest_observations_parsed",
    "first_pinned_qemu_build_matches", "second_pinned_qemu_build_matches",
    "both_pinned_qemu_builds_match", "sanitized_installed_loaded_identity_stable",
}
LOADED_IDENTITY_CHECK_KEYS = {
    "pinned_qemu_guest_double_probe_succeeded",
    "both_sanitized_guest_observations_parsed", "pinned_qemu_build_matches",
    "sanitized_installed_loaded_identity_stable", "installed_bundle_present",
    "installed_bundle_id_matches_candidate",
    "installed_info_plist_sha256_matches_candidate",
    "installed_binary_sha256_matches_candidate", "installed_macho_uuid_unambiguous",
    "installed_macho_uuid_matches_candidate", "kext_reported_loaded",
    "loaded_uuid_matches_installed", "loaded_uuid_matches_candidate",
}
LOADED_IDENTITY_NON_CLAIM_KEYS = {
    "release_tag_claimed", "candidate_kext_installed_by_capture",
    "candidate_kext_loaded_by_capture", "kext_unloaded_by_capture",
    "host_or_guest_rebooted", "network_configuration_changed", "association_tested",
    "authentication_tested", "dhcp_tested", "data_transfer_tested",
}


def require_nonnegative_number(value: object, phase: str) -> None:
    if type(value) not in {int, float}:
        raise RunnerError(phase)
    try:
        numeric = float(value)
    except OverflowError as error:
        raise RunnerError(phase) from error
    if not math.isfinite(numeric) or numeric < 0:
        raise RunnerError(phase)


def require_probe_result(value: object, phase: str) -> None:
    if not isinstance(value, dict):
        raise RunnerError(phase)
    require_exact_keys(value, {"ssh_returncode", "duration_seconds", "stderr_line_count"}, phase)
    if type(value.get("ssh_returncode")) is not int or value["ssh_returncode"] != 0:
        raise RunnerError(phase)
    require_nonnegative_number(value.get("duration_seconds"), phase)
    if type(value.get("stderr_line_count")) is not int or value["stderr_line_count"] < 0:
        raise RunnerError(phase)


def bind_loaded_identity(document: dict[str, object], candidate: dict[str, object]) -> None:
    """Accept only a complete, positive, non-retaining identity capture."""
    require_exact_keys(document, {
        "schema_version", "capture_kind", "captured_at_utc", "capture_mode",
        "candidate_receipt_schema", "candidate_receipt_kind", "expected_local_lab_candidate",
        "guest_observation", "guest_double_read", "candidate_binding", "command_result",
        "non_claims", "verdict",
    }, "loaded-identity")
    if (document.get("schema_version") != LOADED_IDENTITY_SCHEMA or
            document.get("capture_kind") != LOADED_IDENTITY_CAPTURE_KIND or
            document.get("capture_mode") != "read-only-pinned-qemu-guest" or
            not isinstance(document.get("captured_at_utc"), str) or
            document.get("candidate_receipt_schema") != CANDIDATE_RECEIPT_SCHEMA or
            document.get("candidate_receipt_kind") != CANDIDATE_RECEIPT_KIND):
        raise RunnerError("loaded-identity")
    expected = document.get("expected_local_lab_candidate")
    if not isinstance(expected, dict):
        raise RunnerError("loaded-identity")
    require_exact_keys(expected, set(candidate), "loaded-identity")
    for key, value in candidate.items():
        if expected.get(key) != value:
            raise RunnerError("loaded-identity")

    guest = document.get("guest_observation")
    if not isinstance(guest, dict):
        raise RunnerError("loaded-identity")
    require_exact_keys(guest, LOADED_IDENTITY_GUEST_KEYS, "loaded-identity")
    if (guest.get("os_build") != PINNED_QEMU_BUILD or
            guest.get("installed_bundle_present") is not True or
            guest.get("installed_bundle_id") != candidate["bundle_id"] or
            guest.get("installed_info_plist_sha256") != candidate["info_plist_sha256"] or
            guest.get("installed_binary_sha256") != candidate["binary_sha256"] or
            guest.get("installed_macho_uuid") != candidate["macho_uuid"] or
            guest.get("installed_macho_uuid_unambiguous") is not True or
            guest.get("kext_reported_loaded") is not True or
            guest.get("guest_observation_parsed") is not True):
        raise RunnerError("loaded-identity")
    if (not isinstance(guest.get("installed_bundle_version"), str) or
            not isinstance(guest.get("installed_short_version"), str) or
            type(guest.get("loaded_driver_line_count")) is not int or
            guest["loaded_driver_line_count"] < 1):
        raise RunnerError("loaded-identity")
    require_digest(guest.get("installed_info_plist_sha256"), "loaded-identity")
    require_digest(guest.get("installed_binary_sha256"), "loaded-identity")
    require_uuid(guest.get("installed_macho_uuid"), "loaded-identity")
    loaded_uuids = guest.get("loaded_uuids_observed")
    if (not isinstance(loaded_uuids, list) or not loaded_uuids or
            any(not isinstance(value, str) or UUID_RE.fullmatch(value) is None
                for value in loaded_uuids) or candidate["macho_uuid"] not in loaded_uuids):
        raise RunnerError("loaded-identity")

    double_read = document.get("guest_double_read")
    if not isinstance(double_read, dict):
        raise RunnerError("loaded-identity")
    require_exact_keys(double_read, LOADED_IDENTITY_DOUBLE_READ_KEYS, "loaded-identity")
    if (type(double_read.get("probe_count")) is not int or double_read["probe_count"] != 2 or
            type(double_read.get("inter_probe_delay_seconds")) not in {int, float} or
            double_read["inter_probe_delay_seconds"] != LOADED_IDENTITY_DOUBLE_READ_DELAY_SECONDS or
            any(double_read.get(key) is not True for key in (
                "first_pinned_qemu_guest_query_succeeded",
                "second_pinned_qemu_guest_query_succeeded",
                "both_pinned_qemu_guest_queries_succeeded",
                "first_sanitized_guest_observation_parsed",
                "second_sanitized_guest_observation_parsed",
                "both_sanitized_guest_observations_parsed",
                "first_pinned_qemu_build_matches", "second_pinned_qemu_build_matches",
                "both_pinned_qemu_builds_match",
                "sanitized_installed_loaded_identity_stable",
            ))):
        raise RunnerError("loaded-identity")

    binding = document.get("candidate_binding")
    if not isinstance(binding, dict):
        raise RunnerError("loaded-identity")
    require_exact_keys(binding, {"checks", "candidate_kext_bound", "failure_reasons"}, "loaded-identity")
    checks = binding.get("checks")
    if (not isinstance(checks, dict) or binding.get("candidate_kext_bound") is not True or
            binding.get("failure_reasons") != []):
        raise RunnerError("loaded-identity")
    require_all_true(checks, LOADED_IDENTITY_CHECK_KEYS, "loaded-identity")

    command = document.get("command_result")
    if not isinstance(command, dict):
        raise RunnerError("loaded-identity")
    require_exact_keys(command, {
        "read_only_probe_count", "inter_probe_delay_seconds", "first_probe", "second_probe",
        "guest_host_key_fingerprint", "guest_command", "raw_guest_stdout_retained",
        "raw_guest_stderr_retained",
    }, "loaded-identity")
    if (type(command.get("read_only_probe_count")) is not int or
            command["read_only_probe_count"] != 2 or
            type(command.get("inter_probe_delay_seconds")) not in {int, float} or
            command["inter_probe_delay_seconds"] != LOADED_IDENTITY_DOUBLE_READ_DELAY_SECONDS or
            command.get("guest_host_key_fingerprint") != PINNED_QEMU_HOST_KEY_SHA256 or
            command.get("guest_command") != "two read-only installed-and-loaded-kext identity queries" or
            command.get("raw_guest_stdout_retained") is not False or
            command.get("raw_guest_stderr_retained") is not False):
        raise RunnerError("loaded-identity")
    require_probe_result(command.get("first_probe"), "loaded-identity")
    require_probe_result(command.get("second_probe"), "loaded-identity")

    require_all_false(document.get("non_claims"), LOADED_IDENTITY_NON_CLAIM_KEYS, "loaded-identity")
    verdict = document.get("verdict")
    if not isinstance(verdict, dict):
        raise RunnerError("loaded-identity")
    require_exact_keys(verdict, {
        "ready_for_exact_local_lab_candidate_runtime_experiment",
        "candidate_runtime_test_performed",
    }, "loaded-identity")
    if (verdict.get("ready_for_exact_local_lab_candidate_runtime_experiment") is not True or
            verdict.get("candidate_runtime_test_performed") is not False):
        raise RunnerError("loaded-identity")


def bind_stage_report(
    document: dict[str, object], candidate: dict[str, object], candidate_sha256: str,
    public: dict[str, object], public_sha256: str,
) -> BoundArtifacts:
    require_exact_keys(document, {
        "schema", "candidate_receipt_sha256", "public_recovery_receipt_sha256",
        "gate_build_dir_token", "guest_dir_token", "source", "helper", "validation",
        "non_claims",
    }, "stage-report")
    if document.get("schema") != STAGE_SCHEMA:
        raise RunnerError("stage-report")
    if (require_digest(document.get("candidate_receipt_sha256"), "stage-report") !=
            candidate_sha256 or
            require_digest(document.get("public_recovery_receipt_sha256"), "stage-report") !=
            public_sha256):
        raise RunnerError("stage-report")
    gate_token = require_token(document.get("gate_build_dir_token"), "stage-report")
    guest_token = require_token(document.get("guest_dir_token"), "stage-report")
    if gate_token != public["gate_build_dir_token"]:
        raise RunnerError("stage-report")
    source = document.get("source")
    if not isinstance(source, dict):
        raise RunnerError("stage-report")
    require_exact_keys(source, {"commit", "identity_sha256", "identity_paths_count"}, "stage-report")
    if (require_commit(source.get("commit"), "stage-report") != candidate["source_commit"] or
            require_digest(source.get("identity_sha256"), "stage-report") !=
            candidate["source_identity_sha256"] or
            require_positive_int(source.get("identity_paths_count"), "stage-report") !=
            candidate["source_identity_paths_count"]):
        raise RunnerError("stage-report")
    if (source["commit"] != public["source_commit"] or
            source["identity_sha256"] != public["source_identity_sha256"] or
            source["identity_paths_count"] != public["source_identity_paths_count"]):
        raise RunnerError("stage-report")
    helper = document.get("helper")
    if not isinstance(helper, dict):
        raise RunnerError("stage-report")
    require_exact_keys(helper, {"sha256", "macho_uuid"}, "stage-report")
    helper_sha256 = require_digest(helper.get("sha256"), "stage-report")
    helper_uuid = require_uuid(helper.get("macho_uuid"), "stage-report")
    if helper_sha256 != public["helper_sha256"] or helper_uuid != public["helper_macho_uuid"]:
        raise RunnerError("stage-report")
    require_all_true(document.get("validation"), {
        "pinned_guest_host_key", "pinned_guest_build", "fresh_restricted_guest_directory",
        "helper_hash_matches_local_sidecar_receipt",
        "helper_macho_uuid_matches_local_sidecar_receipt",
        "guest_rehash_matches_local_bytes", "guest_macho_uuid_matches_local_bytes",
        "root_owned_nonwritable_guest_stage",
    }, "stage-report")
    require_all_false(document.get("non_claims"), {
        "helper_invoked", "target_identity_collected", "credential_collected",
        "remote_output_collected", "association_tested", "rebooted",
        "runtime_experiment_performed",
    }, "stage-report")
    return BoundArtifacts(helper_sha256, helper_uuid, public_sha256, guest_token)


def bind_artifacts(config: Config, state: RuntimeState) -> tuple[dict[str, object], BoundArtifacts]:
    candidate, candidate_sha256 = canonical_candidate(config.candidate_receipt)
    public_path = require_absolute_regular(config.public_receipt, "public-receipt")
    try:
        public = load_public_recovery_receipt(public_path)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise RunnerError("public-receipt") from error
    public_sha256 = sha256_file(public_path)
    if (public["candidate_receipt_sha256"] != candidate_sha256 or
            public["candidate_profile"] != candidate["profile"] or
            public["source_commit"] != candidate["source_commit"] or
            public["source_identity_sha256"] != candidate["source_identity_sha256"] or
            public["source_identity_paths_count"] != candidate["source_identity_paths_count"]):
        raise RunnerError("candidate-public-binding")
    state.candidate_public_bound = True
    loaded = load_json(config.loaded_identity, "loaded-identity")
    bind_loaded_identity(loaded, candidate)
    state.candidate_loaded_bound = True
    stage = load_json(config.stage_report, "stage-report")
    artifacts = bind_stage_report(stage, candidate, candidate_sha256, public, public_sha256)
    state.public_stage_bound = True
    return candidate, artifacts


REMOTE_HELPER_CHECK = r'''
import hashlib
import os
import re
import stat
import struct
import sys
import uuid

if len(sys.argv) != 5:
    raise SystemExit(1)
stage, expected_sha256, expected_uuid, expected_receipt_sha256 = sys.argv[1:]
prefix = "/private/tmp/aiam-iwn-public-recovery-"
if not stage.startswith(prefix):
    raise SystemExit(1)
token = stage[len(prefix):]
if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", token) is None:
    raise SystemExit(1)
if (re.fullmatch(r"[0-9a-f]{64}", expected_sha256) is None or
        re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", expected_uuid) is None or
        re.fullmatch(r"[0-9a-f]{64}", expected_receipt_sha256) is None):
    raise SystemExit(1)
for parent in ("/private", "/private/tmp"):
    metadata = os.lstat(parent)
    if (stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode) or
            metadata.st_uid != 0):
        raise SystemExit(1)
stage_metadata = os.lstat(stage)
if (stat.S_ISLNK(stage_metadata.st_mode) or not stat.S_ISDIR(stage_metadata.st_mode) or
        stage_metadata.st_uid != 0 or stat.S_IMODE(stage_metadata.st_mode) != 0o555):
    raise SystemExit(1)
stage_fd = os.open(
    stage, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)
)
opened_stage = os.fstat(stage_fd)
if (not stat.S_ISDIR(opened_stage.st_mode) or opened_stage.st_uid != 0 or
        stat.S_IMODE(opened_stage.st_mode) != 0o555 or
        (opened_stage.st_dev, opened_stage.st_ino) != (stage_metadata.st_dev, stage_metadata.st_ino)):
    raise SystemExit(1)
helper_name = "airport_itlwm_lab_public_recovery"
receipt_name = "iwn-public-recovery-receipt-v1.json"
if set(os.listdir(stage_fd)) != {helper_name, receipt_name}:
    raise SystemExit(1)
helper = os.path.join(stage, helper_name)

def open_checked(name, required_mode):
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(name, flags, dir_fd=stage_fd)
    metadata = os.fstat(descriptor)
    if (not stat.S_ISREG(metadata.st_mode) or metadata.st_uid != 0 or
            metadata.st_nlink != 1 or
            stat.S_IMODE(metadata.st_mode) != required_mode):
        os.close(descriptor)
        raise SystemExit(1)
    chunks = []
    total = 0
    while True:
        chunk = os.read(descriptor, 1024 * 1024)
        if not chunk:
            break
        chunks.append(chunk)
        total += len(chunk)
        if total > 32 * 1024 * 1024:
            os.close(descriptor)
            raise SystemExit(1)
    return descriptor, b"".join(chunks)

helper_fd, payload = open_checked(helper_name, 0o555)
receipt_fd, receipt_payload = open_checked(receipt_name, 0o444)
if (hashlib.sha256(payload).hexdigest() != expected_sha256 or
        hashlib.sha256(receipt_payload).hexdigest() != expected_receipt_sha256 or
        len(payload) < 32):
    raise SystemExit(1)
magic, cpu_type, _subtype, file_type, count, command_bytes, _flags, _reserved = struct.unpack_from(
    "<IiiIIIII", payload, 0
)
if magic != 0xFEEDFACF or cpu_type not in (0x01000007, 0x0100000C) or file_type != 2:
    raise SystemExit(1)
cursor = 32
end = cursor + command_bytes
if end > len(payload):
    raise SystemExit(1)
found = None
for _ in range(count):
    if cursor + 8 > end:
        raise SystemExit(1)
    command, size = struct.unpack_from("<II", payload, cursor)
    if size < 8 or cursor + size > end:
        raise SystemExit(1)
    if command == 0x1B:
        if size < 24 or found is not None:
            raise SystemExit(1)
        found = str(uuid.UUID(bytes=payload[cursor + 8:cursor + 24])).upper()
    cursor += size
if found != expected_uuid:
    raise SystemExit(1)
'''

REMOTE_VERIFY_HELPER = REMOTE_HELPER_CHECK + r'''
os.close(helper_fd)
os.close(receipt_fd)
os.close(stage_fd)
print("OK")
'''

REMOTE_EXEC_HELPER = REMOTE_HELPER_CHECK + r'''
os.close(helper_fd)
os.close(receipt_fd)
os.close(stage_fd)
os.execve(helper, [helper], os.environ)
raise SystemExit(1)
'''


def quote_remote_shell_word(value: str) -> str:
    """Quote a fixed SSH remote-command word without delegating to a local shell."""
    return "'" + value.replace("'", "'\"'\"'") + "'"


class PinnedGuest:
    """One strict-key SSH transport; commands never inherit controller stdin."""

    def __init__(self) -> None:
        self._known_hosts: Optional[Path] = None
        self._base: list[str] = []

    def open(self) -> None:
        handle: Optional[tempfile.NamedTemporaryFile] = None
        try:
            handle = tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", prefix="aiam-public-recovery-known-hosts-",
                delete=False,
            )
            handle.write(PINNED_QEMU_HOST_KEY + "\n")
            handle.close()
            os.chmod(handle.name, 0o600)
            verification = subprocess.run(
                ["/usr/bin/ssh-keygen", "-lf", handle.name, "-E", "sha256"],
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                check=False, timeout=REMOTE_COMMAND_TIMEOUT_SECONDS, env=clean_environment(),
            )
            fields = verification.stdout.split()
            if (verification.returncode != 0 or len(fields) < 2 or
                    fields[1].decode("ascii", "ignore") != PINNED_QEMU_HOST_KEY_SHA256):
                raise RunnerError("guest-hostkey-pin")
            self._known_hosts = Path(handle.name)
            self._base = [
                "/usr/bin/ssh", "-F", "/dev/null", "-T", "-p", str(PINNED_QEMU_PORT),
                "-o", "BatchMode=yes", "-o", "ConnectTimeout=8",
                "-o", "StrictHostKeyChecking=yes",
                "-o", f"UserKnownHostsFile={self._known_hosts}",
                "-o", "GlobalKnownHostsFile=/dev/null", "-o", "UpdateHostKeys=no",
                "-o", "LogLevel=ERROR", PINNED_QEMU_GUEST,
            ]
        except (OSError, subprocess.SubprocessError, RunnerError) as error:
            if handle is not None:
                Path(handle.name).unlink(missing_ok=True)
            self._known_hosts = None
            self._base = []
            if isinstance(error, RunnerError):
                raise
            raise RunnerError("guest-hostkey-pin") from error

    def close(self) -> None:
        if self._known_hosts is not None:
            self._known_hosts.unlink(missing_ok=True)
        self._known_hosts = None
        self._base = []

    def run(self, command: list[str], *, input_data: Optional[bytes] = None,
            timeout: int = REMOTE_COMMAND_TIMEOUT_SECONDS,
            capture_stderr: bool = False) -> subprocess.CompletedProcess[bytes]:
        require(bool(self._base), "guest-transport")
        try:
            return subprocess.run(
                [*self._base, *command], input=input_data, stdin=(
                    subprocess.DEVNULL if input_data is None else None
                ), stdout=subprocess.PIPE,
                stderr=subprocess.PIPE if capture_stderr else subprocess.DEVNULL, check=False,
                timeout=timeout, env=clean_environment(),
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RunnerError("guest-command") from error

    def verify_build(self) -> None:
        result = self.run(["/usr/bin/sw_vers", "-buildVersion"])
        if result.returncode != 0 or result.stdout != (PINNED_QEMU_BUILD + "\n").encode("ascii"):
            raise RunnerError("guest-build")

    def verify_staged_helper(self, artifacts: BoundArtifacts) -> None:
        result = self.run(
            ["/usr/bin/python3", "-I", "-", artifacts.guest_stage_path,
             artifacts.helper_sha256, artifacts.helper_macho_uuid,
             artifacts.public_receipt_sha256],
            input_data=REMOTE_VERIFY_HELPER.encode("utf-8"),
        )
        if result.returncode != 0 or result.stdout != b"OK\n":
            raise RunnerError("guest-helper-binding")

    def start_helper(self, artifacts: BoundArtifacts, target: HostTarget,
                     guest_stdin_read: int) -> subprocess.Popen[bytes]:
        require(SHA256_RE.fullmatch(target.ssid_sha256) is not None, "target-status")
        require(SHA256_RE.fullmatch(target.bssid_sha256) is not None, "target-status")
        remote_words = [
            "/usr/bin/env", "-i", "PATH=/usr/bin:/bin",
            f"AIRPORT_ITLWM_LAB_TARGET_SSID_SHA256={target.ssid_sha256}",
            f"AIRPORT_ITLWM_LAB_TARGET_BSSID_SHA256={target.bssid_sha256}",
            "/usr/bin/python3", "-I", "-c", REMOTE_EXEC_HELPER,
            artifacts.guest_stage_path, artifacts.helper_sha256,
            artifacts.helper_macho_uuid, artifacts.public_receipt_sha256,
        ]
        remote_command = "exec " + " ".join(quote_remote_shell_word(word) for word in remote_words)
        try:
            return subprocess.Popen(
                [*self._base, remote_command], stdin=guest_stdin_read, stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL, close_fds=True, env=clean_environment(),
            )
        except OSError as error:
            raise RunnerError("guest-helper-start") from error


class BrokerSession:
    """Native relay control.  Python only passes file descriptors and tokens."""

    def __init__(self, broker: Path, credential_fd: int, host_pipe_write: int) -> None:
        self._parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self._parent.settimeout(HOST_FED_TIMEOUT_SECONDS)
        self._process: Optional[subprocess.Popen[bytes]] = None
        try:
            self._process = subprocess.Popen(
                [str(broker), str(credential_fd), str(host_pipe_write), str(child.fileno())],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                close_fds=True, pass_fds=(credential_fd, host_pipe_write, child.fileno()),
                env=clean_environment(),
            )
        except OSError as error:
            child.close()
            self._parent.close()
            raise RunnerError("broker-start") from error
        finally:
            child.close()
            try:
                os.close(credential_fd)
            except OSError:
                pass
            try:
                os.close(host_pipe_write)
            except OSError:
                pass

    def _set_timeout_until(self, deadline: float) -> None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RunnerError("broker-control")
        try:
            self._parent.settimeout(remaining)
        except OSError as error:
            raise RunnerError("broker-control") from error

    def _receive_until(self, expected: bytes, deadline: float) -> None:
        self._set_timeout_until(deadline)
        try:
            payload, ancillary, flags, _address = self._parent.recvmsg(64, 0)
        except (OSError, socket.timeout) as error:
            raise RunnerError("broker-control") from error
        if payload != expected or ancillary or flags != 0:
            raise RunnerError("broker-control")

    def wait_host_fed(self) -> None:
        self._receive_until(b"HOST_FED", time.monotonic() + HOST_FED_TIMEOUT_SECONDS)

    def start(self, target: HostTarget, guest_write: int) -> None:
        payload = b"START " + target.ssid_sha256.encode("ascii") + b" " + target.bssid_sha256.encode("ascii")
        deadline = time.monotonic() + BROKER_START_CONTROL_TIMEOUT_SECONDS
        try:
            self._set_timeout_until(deadline)
            sent = self._parent.sendmsg(
                [payload], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, struct.pack("i", guest_write))]
            )
        except (OSError, socket.timeout) as error:
            raise RunnerError("broker-start-control") from error
        if sent != len(payload):
            raise RunnerError("broker-start-control")
        self._receive_until(b"STARTED", deadline)

    def arm(self) -> None:
        self._send_fixed(b"ARM", b"ARMED")

    def release(self) -> None:
        self._send_fixed(b"RELEASE", b"RELEASED")

    def abort(self) -> bool:
        try:
            self._send_fixed(b"ABORT", b"ABORTED")
            return True
        except RunnerError:
            return False

    def _send_fixed(self, message: bytes, expected: bytes) -> None:
        deadline = time.monotonic() + BROKER_CONTROL_TIMEOUT_SECONDS
        try:
            self._set_timeout_until(deadline)
            sent = self._parent.send(message)
        except (OSError, socket.timeout) as error:
            raise RunnerError("broker-control") from error
        if sent != len(message):
            raise RunnerError("broker-control")
        self._receive_until(expected, deadline)

    def close(self) -> bool:
        exited_zero = False
        try:
            if self._process is not None:
                try:
                    exited_zero = self._process.wait(timeout=PROCESS_CLEANUP_TIMEOUT_SECONDS) == 0
                except subprocess.TimeoutExpired:
                    self._process.terminate()
                    try:
                        self._process.wait(timeout=PROCESS_CLEANUP_TIMEOUT_SECONDS)
                    except subprocess.TimeoutExpired:
                        self._process.kill()
                        self._process.wait(timeout=PROCESS_CLEANUP_TIMEOUT_SECONDS)
        finally:
            self._parent.close()
            self._process = None
        return exited_zero


class ActivationOutput:
    """Incrementally consume the two fixed switcher lines without retaining output."""

    def __init__(self, process: subprocess.Popen[bytes]) -> None:
        if process.stdout is None:
            raise RunnerError("host-activation-output")
        self._process = process
        self._stream = process.stdout
        self._selector = selectors.DefaultSelector()
        self._selector.register(self._stream, selectors.EVENT_READ)
        self._buffer = bytearray()

    def close(self) -> None:
        self._selector.close()
        self._stream.close()
        self._buffer.clear()

    def next_line(self, timeout_seconds: float, phase: str) -> bytes:
        deadline = time.monotonic() + timeout_seconds
        while True:
            marker = self._buffer.find(b"\n")
            if marker >= 0:
                line = bytes(self._buffer[:marker + 1])
                del self._buffer[:marker + 1]
                return line
            if len(self._buffer) > MAX_HOST_ACTIVATION_LINE_BYTES:
                raise RunnerError(phase)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RunnerError(phase)
            events = self._selector.select(remaining)
            if not events:
                raise RunnerError(phase)
            try:
                chunk = os.read(self._stream.fileno(), 256)
            except OSError as error:
                raise RunnerError(phase) from error
            if not chunk:
                raise RunnerError(phase)
            self._buffer.extend(chunk)

    def require_clean_eof(self, timeout_seconds: float, phase: str) -> None:
        deadline = time.monotonic() + timeout_seconds
        while True:
            if self._buffer:
                raise RunnerError(phase)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RunnerError(phase)
            events = self._selector.select(remaining)
            if not events:
                raise RunnerError(phase)
            try:
                chunk = os.read(self._stream.fileno(), 256)
            except OSError as error:
                raise RunnerError(phase) from error
            if not chunk:
                return
            self._buffer.extend(chunk)


class HelperOutput:
    """A bounded, line-oriented parser that never persists helper output."""

    def __init__(self, process: subprocess.Popen[bytes]) -> None:
        if process.stdout is None:
            raise RunnerError("helper-output")
        self._process = process
        self._stream = process.stdout
        self._selector = selectors.DefaultSelector()
        self._selector.register(self._stream, selectors.EVENT_READ)
        self._buffer = bytearray()

    def close(self) -> None:
        self._selector.close()
        self._stream.close()
        self._buffer.clear()

    def next_expected(self, expected_state: str, timeout_seconds: int) -> None:
        deadline = time.monotonic() + timeout_seconds
        while True:
            marker = self._buffer.find(b"\n")
            if marker >= 0:
                line = bytes(self._buffer[:marker + 1])
                del self._buffer[:marker + 1]
                self._validate_line(line, expected_state)
                return
            if len(self._buffer) > MAX_HELPER_LINE_BYTES:
                raise RunnerError("helper-output")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RunnerError("helper-timeout")
            events = self._selector.select(remaining)
            if not events:
                raise RunnerError("helper-timeout")
            try:
                chunk = os.read(self._stream.fileno(), 512)
            except OSError as error:
                raise RunnerError("helper-output") from error
            if not chunk:
                raise RunnerError("helper-output")
            self._buffer.extend(chunk)

    def require_clean_eof(self, timeout_seconds: int) -> None:
        deadline = time.monotonic() + timeout_seconds
        while True:
            if self._buffer:
                raise RunnerError("helper-extra-output")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RunnerError("helper-output")
            events = self._selector.select(remaining)
            if not events:
                raise RunnerError("helper-output")
            try:
                chunk = os.read(self._stream.fileno(), 512)
            except OSError as error:
                raise RunnerError("helper-output") from error
            if not chunk:
                return
            self._buffer.extend(chunk)

    def drain_until_eof(self, timeout_seconds: int) -> bool:
        """Drain and discard helper output so its final cleanup cannot SIGPIPE."""
        deadline = time.monotonic() + timeout_seconds
        self._buffer.clear()
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            events = self._selector.select(min(remaining, 1.0))
            if events:
                try:
                    chunk = os.read(self._stream.fileno(), 4096)
                except OSError as error:
                    raise RunnerError("helper-output") from error
                if not chunk:
                    return True
                # Deliberately discard rather than retain an error/result line.
                continue
            if self._process.poll() is not None:
                # A dead child must eventually close stdout; keep the remaining
                # short bounded wait to distinguish that from a malformed pipe.
                continue

    @staticmethod
    def _validate_line(line: bytes, expected_state: str) -> None:
        try:
            text = line.decode("ascii")
        except UnicodeDecodeError as error:
            raise RunnerError("helper-grammar") from error
        matched = HELPER_RE.fullmatch(text)
        if matched is None:
            raise RunnerError("helper-grammar")
        fields = matched.groups()
        state = fields[0]
        try:
            values = [int(value) for value in fields[2:]]
        except ValueError as error:
            raise RunnerError("helper-grammar") from error
        (discovery, matching_records, alternate_count, alternate_bands, alternate_ready,
         scan_error, association_error, initial_exact, withdrawal_arm,
         pre_withdrawal_exact, withdrawal_control, recovery_same,
         recovery_different, cleanup_disassociate) = values
        if (state not in HELPER_POSITIVE_STATES | HELPER_FAILURE_STATES or
                fields[1] not in {"airport-itlwm-bsd", "unresolved"} or
                not 0 <= discovery <= 80 or not 0 <= matching_records <= 80 or
                not 0 <= alternate_count <= 80 or not 0 <= alternate_bands <= 2):
            raise RunnerError("helper-grammar")
        if state in HELPER_FAILURE_STATES:
            if state == "initial-or-alternate-target-unavailable":
                if scan_error != 0:
                    raise RunnerError("helper-result-initial-scan-error")
                if matching_records == 0:
                    if alternate_count > 0:
                        raise RunnerError(
                            "helper-result-initial-bss-unavailable-same-ess-visible"
                        )
                    raise RunnerError("helper-result-initial-ess-unavailable")
                if matching_records > 1:
                    raise RunnerError("helper-result-initial-target-ambiguous")
                if alternate_count < 2:
                    raise RunnerError("helper-result-alternate-bss-insufficient")
                if alternate_bands < 2:
                    raise RunnerError("helper-result-alternate-band-insufficient")
                if alternate_ready != 1:
                    raise RunnerError("helper-result-alternate-readiness-inconsistent")
            raise RunnerError("helper-result-" + state)
        if state != expected_state or fields[1] != "airport-itlwm-bsd" or \
                not 1 <= discovery <= 80 or matching_records != 1 or \
                alternate_count < 2 or alternate_bands != 2 or alternate_ready != 1 or \
                scan_error != 0 or association_error != 0 or initial_exact != 1:
            raise RunnerError("helper-grammar")
        expected_flags = {
            "initial-ready": (0, 0, 0, 0, 0, 0),
            "withdraw-armed": (1, 1, 0, 0, 0, 0),
            "recovered": (1, 1, 1, 1, 1, 1),
        }
        if (withdrawal_arm, pre_withdrawal_exact, withdrawal_control, recovery_same,
                recovery_different, cleanup_disassociate) != expected_flags[state]:
            raise RunnerError("helper-grammar")


def parse_multiband_result(output: bytes, pattern: re.Pattern[bytes], phase: str) -> None:
    matched = pattern.fullmatch(output)
    if matched is None:
        raise RunnerError(phase)
    try:
        external_count = int(matched.group(1))
    except ValueError as error:
        raise RunnerError(phase) from error
    if external_count < 2:
        raise RunnerError(phase)


def parse_hash_only_status(output: bytes, phase: str) -> HostTarget:
    matched = STATUS_RE.fullmatch(output)
    if matched is None:
        raise RunnerError(phase)
    try:
        ssid_digest = matched.group(1).decode("ascii")
        bssid_digest = matched.group(2).decode("ascii")
        lease_seconds = int(matched.group(3))
        remaining = int(matched.group(4))
    except (UnicodeDecodeError, ValueError) as error:
        raise RunnerError(phase) from error
    if (SHA256_RE.fullmatch(ssid_digest) is None or SHA256_RE.fullmatch(bssid_digest) is None or
            lease_seconds != LEASE_SECONDS or not 1 <= remaining <= LEASE_SECONDS):
        raise RunnerError(phase)
    return HostTarget(ssid_digest, bssid_digest, remaining)


def run_switcher(arguments: list[str], *, timeout: int,
                 stdin: object = subprocess.DEVNULL) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            [str(LABAP_SWITCHER), *arguments], stdin=stdin, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, check=False, timeout=timeout, env=clean_environment(),
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RunnerError("host-switcher") from error


def wait_host_credential_ready(output: ActivationOutput) -> None:
    """Accept the one fixed pre-credential line before starting the broker."""
    if output.next_line(HOST_CREDENTIAL_READY_TIMEOUT_SECONDS, "host-credential-ready") != \
            HOST_CREDENTIAL_READY:
        raise RunnerError("host-credential-ready")


def wait_host_setup_started(output: ActivationOutput, timeout: float) -> None:
    """Accept the fixed post-credential setup origin before ACTIVE timing starts."""
    if output.next_line(timeout, "host-setup-started") != HOST_SETUP_STARTED:
        raise RunnerError("host-setup-started")


def wait_host_activation(process: subprocess.Popen[bytes], output: ActivationOutput,
                         timeout: float) -> bytes:
    """Consume the final switcher result on the same bounded stream parser."""
    deadline = time.monotonic() + timeout
    line = output.next_line(timeout, "host-activation")
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise RunnerError("host-activation")
    output.require_clean_eof(remaining, "host-activation")
    try:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RunnerError("host-activation")
        returncode = process.wait(timeout=remaining)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RunnerError("host-activation") from error
    if returncode != 0:
        raise RunnerError("host-activation")
    return line


@contextlib.contextmanager
def mask_cleanup_signals() -> object:
    """Do not let a second terminal signal interrupt verified host restoration."""
    previous: dict[int, Any] = {}
    try:
        for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
            previous[signum] = signal.signal(signum, signal.SIG_IGN)
        yield
    finally:
        for signum, handler in previous.items():
            signal.signal(signum, handler)


def fixed_git_bytes(arguments: list[str], phase: str) -> bytes:
    """Read one committed-tree fact without inheriting user Git configuration input."""
    try:
        result = subprocess.run(
            ["/usr/bin/git", "-C", str(ROOT), *arguments], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False,
            timeout=REMOTE_COMMAND_TIMEOUT_SECONDS, env=clean_environment(),
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RunnerError(phase) from error
    if result.returncode != 0:
        raise RunnerError(phase)
    return result.stdout


def require_clean_tracked_source(phase: str) -> None:
    """Reject staged or tracked edits while permitting unrelated user untracked files."""
    for arguments in (
        ["diff", "--quiet", "--exit-code"],
        ["diff", "--cached", "--quiet", "--exit-code"],
    ):
        try:
            result = subprocess.run(
                ["/usr/bin/git", "-C", str(ROOT), *arguments], stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
                timeout=REMOTE_COMMAND_TIMEOUT_SECONDS, env=clean_environment(),
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise RunnerError(phase) from error
        if result.returncode != 0:
            raise RunnerError(phase)


def bind_current_candidate_source(candidate: dict[str, object]) -> Path:
    """Bind the committed Tahoe source tree and broker C bytes to the candidate."""
    root_text = fixed_git_bytes(["rev-parse", "--show-toplevel"], "candidate-source")
    try:
        root = Path(root_text.decode("utf-8").strip()).resolve(strict=True)
        expected_root = ROOT.resolve(strict=True)
    except (OSError, UnicodeDecodeError) as error:
        raise RunnerError("candidate-source") from error
    if root != expected_root:
        raise RunnerError("candidate-source")
    require_clean_tracked_source("candidate-source")
    head_bytes = fixed_git_bytes(["rev-parse", "--verify", "HEAD"], "candidate-source")
    try:
        head = head_bytes.decode("ascii").strip().lower()
    except UnicodeDecodeError as error:
        raise RunnerError("candidate-source") from error
    if COMMIT_RE.fullmatch(head) is None or head != candidate["source_commit"]:
        raise RunnerError("candidate-source")
    raw_records = fixed_git_bytes(
        ["ls-tree", "-r", "-z", "HEAD", "--", *source_paths("v2")], "candidate-source"
    )
    digest = hashlib.sha256()
    digest.update(source_identity_domain("v2"))
    records_count = 0
    try:
        for entry in raw_records.split(b"\0"):
            if not entry:
                continue
            metadata, path = entry.split(b"\t", 1)
            mode, object_type, object_id = metadata.split(b" ", 2)
            if object_type != b"blob":
                continue
            digest.update(
                f"{mode.decode('ascii')} {object_id.decode('ascii')} "
                f"{path.decode('utf-8', 'surrogateescape')}\0".encode(
                    "utf-8", "surrogateescape"
                )
            )
            records_count += 1
    except (UnicodeDecodeError, ValueError) as error:
        raise RunnerError("candidate-source") from error
    if (records_count < 1 or digest.hexdigest() != candidate["source_identity_sha256"] or
            records_count != candidate["source_identity_paths_count"]):
        raise RunnerError("candidate-source")
    source = require_absolute_regular(ROOT / BROKER_SOURCE_RELATIVE, "broker-source")
    committed = fixed_git_bytes(["show", f"HEAD:{BROKER_SOURCE_RELATIVE}"], "broker-source")
    try:
        if source.read_bytes() != committed:
            raise RunnerError("broker-source")
    except OSError as error:
        raise RunnerError("broker-source") from error
    return source


class Supervisor:
    def __init__(self, config: Config, state: RuntimeState) -> None:
        self.config = config
        self.state = state
        self.candidate: Optional[dict[str, object]] = None
        self.artifacts: Optional[BoundArtifacts] = None
        self.transport: Optional[PinnedGuest] = None
        self.broker: Optional[BrokerSession] = None
        self.broker_binary: Optional[Path] = None
        self._broker_build_dir: Optional[Path] = None
        self.helper_process: Optional[subprocess.Popen[bytes]] = None
        self.helper_output: Optional[HelperOutput] = None
        self.activation: Optional[subprocess.Popen[bytes]] = None
        self.activation_output: Optional[ActivationOutput] = None
        self.state_dir: Optional[Path] = None
        self.host_target: Optional[HostTarget] = None
        self._host_fed_deadline: Optional[float] = None
        self._setup_started_deadline: Optional[float] = None
        self._withdraw_control_deadline: Optional[float] = None

    def execute(self) -> None:
        self.candidate, self.artifacts = bind_artifacts(self.config, self.state)
        self._require_local_tools()
        self._run_host_preflight()
        self.transport = PinnedGuest()
        self.transport.open()
        self.transport.verify_build()
        self.transport.verify_staged_helper(self.artifacts)
        self.state.guest_helper_reverified = True
        self._capture_fresh_loaded_identity()
        self._activate_and_start_broker()
        self._run_helper_sequence()
        with mask_cleanup_signals():
            self._verified_rollback_and_retire()
        self.state.result = "PASS"
        self.state.failure_phase = "none"

    def cleanup_after_failure(self) -> None:
        with mask_cleanup_signals():
            if self.state.result == "PASS":
                self._close_runtime_resources()
                return
            if self.broker is not None and not self.state.broker_released:
                self.state.broker_aborted = self.broker.abort()
                if not self.state.broker_aborted:
                    self.state.broker_exited_zero = self.broker.close()
                    self.broker = None
            self._stop_activation()
            if self.state.activation_attempted:
                try:
                    self._verified_rollback_and_retire()
                except RunnerError:
                    if self.state.failure_phase == "none":
                        self.state.failure_phase = "cleanup-rollback"
            helper_cleanup_error: Optional[Exception] = None
            try:
                self._close_helper_after_abort()
            except Exception as error:
                helper_cleanup_error = error
            self._close_runtime_resources()
            if helper_cleanup_error is not None:
                raise RunnerError("helper-cleanup") from helper_cleanup_error

    def _require_local_tools(self) -> None:
        require_absolute_regular(LABAP_SWITCHER.resolve(), "host-switcher", executable=True)
        if self.candidate is None:
            raise RunnerError("candidate-source")
        source = bind_current_candidate_source(self.candidate)
        self.state.current_source_candidate_bound = True
        build_dir: Optional[Path] = None
        try:
            build_dir = Path(tempfile.mkdtemp(prefix=BROKER_BUILD_PREFIX, dir="/tmp"))
            os.chmod(build_dir, 0o700)
            metadata = build_dir.lstat()
            if (not str(build_dir).startswith("/tmp/" + BROKER_BUILD_PREFIX) or
                    stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode) or
                    stat.S_IMODE(metadata.st_mode) != 0o700):
                raise RunnerError("broker-build")
            self._broker_build_dir = build_dir
            committed_source = fixed_git_bytes(
                ["show", f"HEAD:{BROKER_SOURCE_RELATIVE}"], "broker-source"
            )
            if source.read_bytes() != committed_source:
                raise RunnerError("broker-source")
            private_source = build_dir / BROKER_SOURCE_COPY_NAME
            write_private_bytes(private_source, committed_source, "broker-build")
            require_absolute_regular(private_source, "broker-build")
            broker = build_dir / BROKER_BINARY_NAME
            result = subprocess.run(
                [BROKER_COMPILER, *BROKER_BUILD_FLAGS, str(private_source), "-o", str(broker)],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                check=False, timeout=45, env=clean_environment(),
            )
            if result.returncode != 0:
                raise RunnerError("broker-build")
            self.broker_binary = require_absolute_regular(broker, "broker-build", executable=True)
            # Re-read every source fence after compilation.  A clean current
            # worktree and byte-for-byte C source are prerequisites for using
            # the materialized binary, not merely for starting its build.
            if bind_current_candidate_source(self.candidate) != source:
                raise RunnerError("candidate-source")
            self.state.broker_materialized_from_head = True
        except (OSError, subprocess.TimeoutExpired, RunnerError) as error:
            try:
                self._cleanup_broker_build()
            except RunnerError:
                pass
            if isinstance(error, RunnerError):
                raise
            raise RunnerError("broker-build") from error

    def _run_host_preflight(self) -> None:
        result = run_switcher(["--preflight"], timeout=HOST_ACTIVATION_TIMEOUT_SECONDS)
        if result.returncode != 0:
            raise RunnerError("host-preflight")
        parse_multiband_result(result.stdout, PREFLIGHT_RE, "host-preflight")
        self.state.host_preflight_passed = True

    def _capture_fresh_loaded_identity(self) -> None:
        """Repeat the pinned, read-only identity observation immediately before activation."""
        if self.transport is None or self.candidate is None:
            raise RunnerError("fresh-loaded-identity")
        probe = guest_probe_script().encode("utf-8")

        def strict_guest_runner(timeout_seconds: int) -> dict[str, object]:
            started = time.monotonic()
            result = self.transport.run(
                ["/usr/bin/sudo", "-n", "/bin/bash", "-s"], input_data=probe,
                timeout=timeout_seconds, capture_stderr=True,
            )
            # The capture reducer immediately turns this transient response into
            # typed identity facts.  The runner never prints, writes, or reports
            # the raw response.
            return {
                "returncode": result.returncode,
                "stdout": result.stdout.decode("utf-8", "replace"),
                "duration_seconds": round(time.monotonic() - started, 3),
                "stderr_line_count": len((result.stderr or b"").splitlines()),
            }

        try:
            fresh = capture_loaded_identity(
                self.config.candidate_receipt,
                FRESH_LOADED_IDENTITY_TIMEOUT_SECONDS,
                candidate_loader=lambda _path: dict(self.candidate or {}),
                guest_runner=strict_guest_runner,
                sleeper=time.sleep,
            )
            bind_loaded_identity(fresh, self.candidate)
        except (OSError, UnicodeError, ValueError, RunnerError) as error:
            if isinstance(error, RunnerError):
                raise RunnerError("fresh-loaded-identity") from error
            raise RunnerError("fresh-loaded-identity") from error
        self.state.fresh_loaded_identity_bound = True

    def _make_state_directory(self) -> Path:
        try:
            path = Path(tempfile.mkdtemp(prefix="aiam-labap-bss-switch.", dir="/tmp"))
            os.chmod(path, 0o700)
            metadata = path.lstat()
        except OSError as error:
            raise RunnerError("host-state-directory") from error
        if (not str(path).startswith(STATE_PREFIX) or stat.S_ISLNK(metadata.st_mode) or
                not stat.S_ISDIR(metadata.st_mode) or stat.S_IMODE(metadata.st_mode) != 0o700):
            raise RunnerError("host-state-directory")
        return path

    def _activate_and_start_broker(self) -> None:
        self.state_dir = self._make_state_directory()
        try:
            host_read, host_write = os.pipe()
        except OSError as error:
            try:
                self._discard_exact_empty_state_dir("host-activation-cleanup")
            except RunnerError as cleanup_error:
                raise cleanup_error from error
            raise RunnerError("host-activation") from error
        try:
            self.activation = subprocess.Popen(
                [str(LABAP_SWITCHER), "--activate", "--state-dir", str(self.state_dir),
                 "--lease-seconds", str(LEASE_SECONDS), "--credential-stdin"],
                stdin=host_read, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                close_fds=True, env=clean_environment(),
            )
        except OSError as error:
            try:
                os.close(host_read)
            except OSError:
                pass
            try:
                os.close(host_write)
            except OSError:
                pass
            try:
                self._discard_exact_empty_state_dir("host-activation-cleanup")
            except RunnerError as cleanup_error:
                raise cleanup_error from error
            raise RunnerError("host-activation") from error
        finally:
            try:
                os.close(host_read)
            except OSError:
                pass
        self.state.activation_attempted = True
        try:
            self.activation_output = ActivationOutput(self.activation)
            # The switcher has now completed every read-only host/topology
            # gate and is blocked in its bounded pre-marker credential read.
            # Do not create the credential-owning native process before this
            # exact nonsecret acknowledgement is consumed.
            wait_host_credential_ready(self.activation_output)
            self.state.host_credential_ready = True
            credential_fd = require_fifo_stdin()
            self.state.stdin_fifo_verified = True
            if self.broker_binary is None:
                try:
                    os.close(credential_fd)
                except OSError:
                    pass
                raise RunnerError("broker-build")
            self.broker = BrokerSession(self.broker_binary, credential_fd, host_write)
            host_write = -1
        finally:
            if host_write >= 0:
                try:
                    os.close(host_write)
                except OSError:
                    pass
        self.broker.wait_host_fed()
        self.state.broker_host_fed = True
        self._host_fed_deadline = time.monotonic() + HOST_FED_TO_START_BUDGET_SECONDS
        if self.activation is None or self.activation_output is None:
            raise RunnerError("host-activation")
        setup_started_budget = self._remaining_host_fed_budget() - \
            SETUP_STARTED_TO_START_BUDGET_SECONDS
        if setup_started_budget <= 0:
            raise RunnerError("broker-start-window")
        wait_host_setup_started(
            self.activation_output,
            min(float(HOST_SETUP_STARTED_TIMEOUT_SECONDS), setup_started_budget),
        )
        self.state.host_setup_started = True
        self._setup_started_deadline = (
            time.monotonic() + SETUP_STARTED_TO_START_BUDGET_SECONDS
        )
        activation_budget = self._remaining_start_budget() - (
            HOST_STATUS_TIMEOUT_SECONDS + BROKER_START_CONTROL_TIMEOUT_SECONDS +
            BROKER_START_SAFETY_SECONDS
        )
        if activation_budget <= 0:
            raise RunnerError("broker-start-window")
        active_output = wait_host_activation(
            self.activation,
            self.activation_output,
            min(float(HOST_ACTIVATION_TIMEOUT_SECONDS), activation_budget),
        )
        parse_multiband_result(active_output, ACTIVE_RE, "host-activation")
        self.activation = None
        self.activation_output.close()
        self.activation_output = None
        self.state.host_activated = True
        target = self._read_host_status("host-status-initial")
        if target.remaining_seconds < MINIMUM_INITIAL_LEASE_REMAINING_SECONDS:
            raise RunnerError("lease-initial-remainder")
        self.state.host_status_initial_verified = True
        self.state.initial_lease_sufficient = True
        self.host_target = target

    def _read_host_status(self, phase: str) -> HostTarget:
        if self.state_dir is None:
            raise RunnerError("host-status")
        timeout = HOST_STATUS_TIMEOUT_SECONDS
        if self._host_fed_deadline is not None or self._setup_started_deadline is not None:
            remaining = self._remaining_start_budget()
            if remaining <= BROKER_START_CONTROL_TIMEOUT_SECONDS + BROKER_START_SAFETY_SECONDS:
                raise RunnerError("broker-start-window")
            timeout = min(
                timeout,
                max(1, int(remaining - BROKER_START_CONTROL_TIMEOUT_SECONDS - BROKER_START_SAFETY_SECONDS)),
            )
        result = run_switcher(["--status", "--state-dir", str(self.state_dir)], timeout=timeout)
        if result.returncode != 0:
            raise RunnerError(phase)
        return parse_hash_only_status(result.stdout, phase)

    def _run_helper_sequence(self) -> None:
        if self.transport is None or self.artifacts is None or self.broker is None or self.host_target is None:
            raise RunnerError("runtime-setup")
        guest_read, guest_write = os.pipe()
        self._require_start_window()
        try:
            self.helper_process = self.transport.start_helper(self.artifacts, self.host_target, guest_read)
        finally:
            try:
                os.close(guest_read)
            except OSError:
                pass
        try:
            self._require_start_window()
            self.broker.start(self.host_target, guest_write)
        finally:
            try:
                os.close(guest_write)
            except OSError:
                pass
        self.state.broker_started = True
        self._host_fed_deadline = None
        self._setup_started_deadline = None
        self.helper_output = HelperOutput(self.helper_process)
        self.helper_output.next_expected("initial-ready", START_TO_ARM_TIMEOUT_SECONDS)
        self.state.helper_initial_ready = True
        self.broker.arm()
        self.state.broker_armed = True
        # The native broker's ARMED deadline begins immediately after its
        # ARMED acknowledgement, not after the helper later reports that it
        # consumed the arm token.  Start the matching local budget here.
        self._withdraw_control_deadline = (
            time.monotonic() + HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS
        )
        self.helper_output.next_expected("withdraw-armed", ARM_TO_WITHDRAW_TIMEOUT_SECONDS)
        self.state.helper_withdraw_armed = True
        self._require_withdraw_control_window(POST_ARM_RELEASE_PATH_SECONDS)
        self._renew_for_withdraw_once()
        self._require_withdraw_control_window(
            HOST_STATUS_TIMEOUT_SECONDS + HOST_WITHDRAW_TIMEOUT_SECONDS +
            BROKER_CONTROL_TIMEOUT_SECONDS
        )
        current = self._read_host_status("host-status-before-withdraw")
        if (current.ssid_sha256 != self.host_target.ssid_sha256 or
                current.bssid_sha256 != self.host_target.bssid_sha256):
            raise RunnerError("host-status-binding")
        if current.remaining_seconds < MINIMUM_RENEWED_LEASE_REMAINING_SECONDS:
            raise RunnerError("lease-renewed-remainder")
        self.state.host_renewed_lease_fresh = True
        if current.remaining_seconds < MINIMUM_WITHDRAW_LEASE_REMAINING_SECONDS:
            raise RunnerError("lease-withdraw-remainder")
        self.state.host_status_before_withdraw_verified = True
        self.state.withdraw_lease_sufficient = True
        self._require_withdraw_control_window(
            HOST_WITHDRAW_TIMEOUT_SECONDS + BROKER_CONTROL_TIMEOUT_SECONDS
        )
        self._withdraw_once()
        self._require_withdraw_control_window(BROKER_CONTROL_TIMEOUT_SECONDS)
        self.broker.release()
        self.state.broker_released = True
        self._withdraw_control_deadline = None
        self.helper_output.next_expected("recovered", HELPER_RECOVERY_TIMEOUT_SECONDS)
        self.state.helper_recovered = True
        # The helper emits `recovered` only after it has disassociated.  Claim
        # the switch lock now, before EOF/reap work can consume the renewed
        # lease and hand recovery to the watchdog.
        with mask_cleanup_signals():
            self._verified_rollback_and_retire()
        self.helper_output.require_clean_eof(PROCESS_CLEANUP_TIMEOUT_SECONDS)
        if self.helper_process.wait(timeout=PROCESS_CLEANUP_TIMEOUT_SECONDS) != 0:
            raise RunnerError("helper-exit")
        self.state.helper_exited_zero = True
        self.state.helper_positive_sequence_verified = True

    def _remaining_host_fed_budget(self) -> float:
        if self._host_fed_deadline is None:
            return float(HOST_ACTIVATION_TIMEOUT_SECONDS)
        remaining = self._host_fed_deadline - time.monotonic()
        if remaining <= 1:
            raise RunnerError("broker-start-window")
        return remaining

    def _remaining_start_budget(self) -> float:
        deadlines = tuple(
            deadline for deadline in (self._host_fed_deadline, self._setup_started_deadline)
            if deadline is not None
        )
        if not deadlines:
            return float(HOST_ACTIVATION_TIMEOUT_SECONDS)
        remaining = min(deadlines) - time.monotonic()
        if remaining <= 1:
            raise RunnerError("broker-start-window")
        return remaining

    def _require_start_window(self) -> None:
        if self._remaining_start_budget() <= (
                BROKER_START_CONTROL_TIMEOUT_SECONDS + BROKER_START_SAFETY_SECONDS):
            raise RunnerError("broker-start-window")

    def _require_withdraw_control_window(self, required_seconds: int) -> None:
        deadline = getattr(self, "_withdraw_control_deadline", None)
        if deadline is None:
            raise RunnerError("withdraw-control-window")
        remaining = deadline - time.monotonic()
        if remaining <= required_seconds + ARMED_PHASE_SAFETY_SECONDS:
            raise RunnerError("withdraw-control-window")

    def _renew_for_withdraw_once(self) -> None:
        if not self.state.helper_withdraw_armed or self.state_dir is None:
            raise RunnerError("renew-order")
        result = run_switcher(
            ["--renew-for-withdraw", "--state-dir", str(self.state_dir)],
            timeout=HOST_RENEW_FOR_WITHDRAW_TIMEOUT_SECONDS,
        )
        if result.returncode != 0 or result.stdout != b"LABAP_BSS_SWITCH=LEASE_RENEWED_FOR_WITHDRAW\n":
            raise RunnerError("host-lease-renew")
        self.state.host_lease_renewed_after_helper_arm = True

    def _withdraw_once(self) -> None:
        if (not self.state.helper_withdraw_armed or
                not self.state.host_lease_renewed_after_helper_arm or self.state_dir is None):
            raise RunnerError("withdraw-order")
        result = run_switcher(
            ["--withdraw", "--state-dir", str(self.state_dir)], timeout=HOST_WITHDRAW_TIMEOUT_SECONDS
        )
        self.state.host_withdraw_called_after_helper_armed = True
        if result.returncode != 0 or result.stdout != b"LABAP_BSS_SWITCH=WITHDRAWN\n":
            raise RunnerError("host-withdraw")
        self.state.host_withdrawn_verified = True

    def _verified_rollback_and_retire(self) -> None:
        if not self.state.activation_attempted or self.state.host_retired_verified:
            return
        if self.state_dir is None:
            raise RunnerError("cleanup-rollback")
        self.state.host_rollback_attempted = True
        try:
            restored = run_switcher(
                ["--rollback", "--state-dir", str(self.state_dir)],
                timeout=ROLLBACK_RESTORE_TIMEOUT_SECONDS,
            )
        except RunnerError:
            restored = None
        if restored is not None and restored.returncode == 0 and \
                restored.stdout == b"LABAP_BSS_SWITCH=ORIGINAL_RESTORED\n":
            self.state.host_rollback_verified = True
        else:
            # A watchdog may have acquired the lock, completed its verified
            # restore, and cleared its marker in the interval after the
            # foreground attempt lost.  One short retirement attempt is an
            # exact proof of that completed route; it does not retry rollback
            # or make any network mutation.
            self.state.host_retire_attempted = True
            if self._try_retire(PROCESS_CLEANUP_TIMEOUT_SECONDS):
                self.state.host_rollback_verified = True
                self.state.host_watchdog_rollback_verified = True
                self.state.host_retired_verified = True
                self.state_dir = None
                return
            # A failed pre-marker/pre-READY activation must not turn into a
            # speculative five-hundred-second wait.  Only an exact, read-only
            # receipt for this state directory and a live watchdog authorizes
            # the bounded handoff; no rollback is retried here.
            try:
                recovery_owner = self._recovery_owner()
            except RunnerError:
                # This covers the short original-restored/marker-cleared/
                # watchdog-pid interval.  It is retirement-only (never a
                # second rollback); a successful exact retirement is proof.
                if self._retire_after_completed_watchdog_window():
                    self.state.host_rollback_verified = True
                    self.state.host_watchdog_rollback_verified = True
                    self.state.host_retired_verified = True
                    self.state_dir = None
                    return
                raise
            if recovery_owner == b"LABAP_BSS_RECOVERY_OWNER=NONE\n":
                # Recheck exact retirement once after the read-only NONE
                # observation.  The empty receipt makes this fast in the
                # normal pre-marker case, while a concurrent completed
                # watchdog can still prove itself solely through retire.
                if self._try_retire(HOST_STATUS_TIMEOUT_SECONDS):
                    self.state.host_rollback_verified = True
                    self.state.host_watchdog_rollback_verified = True
                    self.state.host_retired_verified = True
                    self.state_dir = None
                    return
                self._discard_proven_unarmed_state_dir()
                return
            if recovery_owner != b"LABAP_BSS_RECOVERY_OWNER=WATCHDOG\n":
                raise RunnerError("cleanup-rollback")
            # A v4 watchdog can legally own the switch lock while restoring
            # through LAR.  Poll only its verified retirement proof after
            # that explicit ownership receipt, never after an arbitrary
            # failed foreground rollback.
            if not self._retire_after_watchdog_handoff():
                raise RunnerError("cleanup-rollback")
            self.state.host_rollback_verified = True
            self.state.host_watchdog_rollback_verified = True
            self.state.host_retired_verified = True
            self.state_dir = None
            return
        self.state.host_retire_attempted = True
        if not self._try_retire(PROCESS_CLEANUP_TIMEOUT_SECONDS):
            raise RunnerError("cleanup-retire")
        self.state.host_retired_verified = True
        self.state_dir = None

    def _try_retire(self, timeout_seconds: int) -> bool:
        if self.state_dir is None:
            return False
        try:
            retired = run_switcher(
                ["--retire", "--state-dir", str(self.state_dir)], timeout=timeout_seconds
            )
        except RunnerError:
            return False
        return retired.returncode == 0 and retired.stdout == b"LABAP_BSS_SWITCH=RETIRED\n"

    def _recovery_owner(self) -> bytes:
        """Read the exact v4 watchdog ownership receipt without mutation."""
        if self.state_dir is None:
            raise RunnerError("cleanup-recovery-owner")
        result = run_switcher(
            ["--recovery-owner", "--state-dir", str(self.state_dir)],
            timeout=HOST_STATUS_TIMEOUT_SECONDS,
        )
        if result.returncode != 0 or result.stdout not in (
                b"LABAP_BSS_RECOVERY_OWNER=WATCHDOG\n",
                b"LABAP_BSS_RECOVERY_OWNER=NONE\n",
        ):
            raise RunnerError("cleanup-recovery-owner")
        return result.stdout

    def _discard_proven_unarmed_state_dir(self) -> None:
        """Remove only the exact empty directory proven unarmed by the switcher."""
        self._discard_exact_empty_state_dir("cleanup-empty-state")

    def _discard_exact_empty_state_dir(self, phase: str) -> None:
        """Use rmdir only; never recursively remove a state directory."""
        state_dir = self.state_dir
        if state_dir is None:
            raise RunnerError(phase)
        try:
            metadata = state_dir.lstat()
        except OSError as error:
            raise RunnerError(phase) from error
        if (not str(state_dir).startswith(STATE_PREFIX) or stat.S_ISLNK(metadata.st_mode) or
                not stat.S_ISDIR(metadata.st_mode) or stat.S_IMODE(metadata.st_mode) != 0o700 or
                metadata.st_uid != os.getuid()):
            raise RunnerError(phase)
        try:
            state_dir.rmdir()
        except OSError as error:
            raise RunnerError(phase) from error
        self.state_dir = None

    def _retire_after_watchdog_handoff(self) -> bool:
        """Wait only for a bounded, self-verifying watchdog retirement receipt."""
        self.state.host_retire_attempted = True
        deadline = time.monotonic() + WATCHDOG_RETIRE_PROOF_TIMEOUT_SECONDS
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            timeout = min(PROCESS_CLEANUP_TIMEOUT_SECONDS, max(1, math.ceil(remaining)))
            if self._try_retire(timeout):
                return True
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            time.sleep(min(float(WATCHDOG_RETIRE_POLL_SECONDS), remaining))

    def _retire_after_completed_watchdog_window(self) -> bool:
        """Bridge only the watchdog's short post-restore receipt race."""
        deadline = time.monotonic() + COMPLETED_WATCHDOG_RETIRE_RACE_TIMEOUT_SECONDS
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            timeout = min(HOST_STATUS_TIMEOUT_SECONDS, max(1, math.ceil(remaining)))
            if self._try_retire(timeout):
                return True
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            time.sleep(min(float(WATCHDOG_RETIRE_POLL_SECONDS), remaining))

    def _close_helper_after_abort(self) -> None:
        self._finish_helper_gracefully()
        self.state.helper_cleanup_closed = True

    def _finish_helper_gracefully(self) -> None:
        process = self.helper_process
        if process is None:
            if self.helper_output is not None:
                self.helper_output.close()
                self.helper_output = None
            return
        output = self.helper_output
        deferred_error: Optional[RunnerError] = None
        if output is None:
            try:
                output = HelperOutput(process)
                self.helper_output = output
            except RunnerError as error:
                deferred_error = error
        if output is not None:
            try:
                output.drain_until_eof(HELPER_GRACEFUL_CLEANUP_TIMEOUT_SECONDS)
            except RunnerError as error:
                deferred_error = error
        if process.poll() is None:
            try:
                process.terminate()
            except OSError as error:
                deferred_error = RunnerError("helper-cleanup")
            if process.poll() is None:
                try:
                    process.wait(timeout=PROCESS_CLEANUP_TIMEOUT_SECONDS)
                except subprocess.TimeoutExpired:
                    try:
                        process.kill()
                    except OSError:
                        pass
                    if process.poll() is None:
                        process.wait(timeout=PROCESS_CLEANUP_TIMEOUT_SECONDS)
        if output is not None:
            try:
                output.drain_until_eof(PROCESS_CLEANUP_TIMEOUT_SECONDS)
            except RunnerError as error:
                deferred_error = error
            output.close()
            self.helper_output = None
        self.helper_process = None
        if deferred_error is not None:
            raise deferred_error

    def _cleanup_broker_build(self) -> None:
        build_dir = self._broker_build_dir
        broker = self.broker_binary
        self._broker_build_dir = None
        self.broker_binary = None
        if build_dir is None:
            return
        try:
            metadata = build_dir.lstat()
        except FileNotFoundError:
            return
        except OSError as error:
            raise RunnerError("broker-cleanup") from error
        if (not str(build_dir).startswith("/tmp/" + BROKER_BUILD_PREFIX) or
                stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode) or
                stat.S_IMODE(metadata.st_mode) != 0o700):
            raise RunnerError("broker-cleanup")
        private_source = build_dir / BROKER_SOURCE_COPY_NAME
        if private_source.exists() or private_source.is_symlink():
            try:
                source_metadata = private_source.lstat()
            except OSError as error:
                raise RunnerError("broker-cleanup") from error
            if (stat.S_ISLNK(source_metadata.st_mode) or not stat.S_ISREG(source_metadata.st_mode) or
                    stat.S_IMODE(source_metadata.st_mode) != 0o600):
                raise RunnerError("broker-cleanup")
            try:
                private_source.unlink()
            except OSError as error:
                raise RunnerError("broker-cleanup") from error
        output = build_dir / BROKER_BINARY_NAME
        if output.exists() or output.is_symlink():
            try:
                output_metadata = output.lstat()
            except OSError as error:
                raise RunnerError("broker-cleanup") from error
            if (stat.S_ISLNK(output_metadata.st_mode) or not stat.S_ISREG(output_metadata.st_mode) or
                    (broker is not None and output.resolve(strict=True) != broker)):
                raise RunnerError("broker-cleanup")
            try:
                output.unlink()
            except OSError as error:
                raise RunnerError("broker-cleanup") from error
        try:
            build_dir.rmdir()
        except OSError as error:
            raise RunnerError("broker-cleanup") from error

    def _close_runtime_resources(self) -> None:
        self._stop_activation()
        self._finish_helper_gracefully()
        if self.broker is not None:
            self.state.broker_exited_zero = self.broker.close()
            self.broker = None
        if self.transport is not None:
            self.transport.close()
            self.transport = None
        self._cleanup_broker_build()

    def _stop_activation(self) -> None:
        if self.activation is not None:
            try:
                if self.activation.poll() is None:
                    self.activation.terminate()
                self.activation.wait(timeout=PROCESS_CLEANUP_TIMEOUT_SECONDS)
            except (OSError, subprocess.TimeoutExpired):
                try:
                    self.activation.kill()
                    self.activation.wait(timeout=PROCESS_CLEANUP_TIMEOUT_SECONDS)
                except (OSError, subprocess.TimeoutExpired):
                    pass
            self.activation = None
        if self.activation_output is not None:
            self.activation_output.close()
            self.activation_output = None


def require_fresh_output(path: Path) -> Path:
    if not path.is_absolute() or path.name in {"", ".", ".."}:
        raise RunnerError("output-path")
    try:
        parent_metadata = path.parent.lstat()
    except OSError as error:
        raise RunnerError("output-parent") from error
    if stat.S_ISLNK(parent_metadata.st_mode) or not stat.S_ISDIR(parent_metadata.st_mode):
        raise RunnerError("output-parent")
    if path.exists() or path.is_symlink():
        raise RunnerError("output-not-fresh")
    try:
        parent = path.parent.resolve(strict=True)
        root = ROOT.resolve(strict=True)
        parent.relative_to(root)
    except ValueError:
        pass
    except OSError as error:
        raise RunnerError("output-parent") from error
    else:
        raise RunnerError("output-inside-source")
    return parent / path.name


def report_document(state: RuntimeState) -> dict[str, object]:
    return {
        "schema": RUNTIME_SCHEMA,
        "captured_at_utc": utc_now(),
        "result": state.result,
        "failure_phase": state.failure_phase,
        "receipt_bindings": {
            "candidate_public_exact": state.candidate_public_bound,
            "candidate_loaded_identity_exact": state.candidate_loaded_bound,
            "fresh_loaded_identity_exact": state.fresh_loaded_identity_bound,
            "public_stage_exact": state.public_stage_bound,
            "staged_helper_reverified": state.guest_helper_reverified,
            "current_source_candidate_exact": state.current_source_candidate_bound,
        },
        "host": {
            "multiband_preflight_passed": state.host_preflight_passed,
            "credential_ready_before_broker": state.host_credential_ready,
            "setup_started_after_credential_consumption": state.host_setup_started,
            "temporary_bss_activated": state.host_activated,
            "hash_only_status_initial_verified": state.host_status_initial_verified,
            "initial_lease_exact_300_and_sufficient": state.initial_lease_sufficient,
            "hash_only_status_before_withdraw_verified": state.host_status_before_withdraw_verified,
            "withdraw_lease_sufficient": state.withdraw_lease_sufficient,
            "lease_renewed_once_after_helper_arm": state.host_lease_renewed_after_helper_arm,
            "renewed_lease_fresh_and_sufficient": state.host_renewed_lease_fresh,
            "withdraw_called_only_after_helper_armed": state.host_withdraw_called_after_helper_armed,
            "withdrawn_verified": state.host_withdrawn_verified,
            "rollback_attempted": state.host_rollback_attempted,
            "rollback_verified": state.host_rollback_verified,
            "watchdog_rollback_verified": state.host_watchdog_rollback_verified,
            "retire_attempted_after_verified_rollback": state.host_retire_attempted,
            "retired_verified": state.host_retired_verified,
        },
        "broker": {
            "materialized_from_current_head": state.broker_materialized_from_head,
            "stdin_fifo_verified": state.stdin_fifo_verified,
            "host_fed": state.broker_host_fed,
            "started": state.broker_started,
            "armed": state.broker_armed,
            "released": state.broker_released,
            "aborted": state.broker_aborted,
            "exited_zero": state.broker_exited_zero,
        },
        "helper": {
            "initial_ready": state.helper_initial_ready,
            "withdraw_armed": state.helper_withdraw_armed,
            "recovered": state.helper_recovered,
            "positive_sequence_verified": state.helper_positive_sequence_verified,
            "exited_zero": state.helper_exited_zero,
            "cleanup_pipe_closed": state.helper_cleanup_closed,
        },
        "non_claims": {
            "credential_read_by_python": False,
            "credential_written_to_report": False,
            "raw_host_output_retained": False,
            "raw_guest_output_retained": False,
            "target_hashes_written_to_report": False,
            "network_configuration_changed_outside_bounded_switcher": False,
            "additional_withdrawal_attempted": False,
        },
    }


def write_report(path: Path, state: RuntimeState) -> None:
    payload = (json.dumps(report_document(state), indent=2, sort_keys=True) + "\n").encode("utf-8")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags, 0o600)
    except OSError as error:
        raise RunnerError("output-write") from error
    try:
        offset = 0
        while offset < len(payload):
            written = os.write(descriptor, payload[offset:])
            if written <= 0:
                raise OSError("short write")
            offset += written
        os.fsync(descriptor)
    except OSError as error:
        raise RunnerError("output-write") from error
    finally:
        os.close(descriptor)


def is_complete_pass(state: RuntimeState) -> bool:
    return all((
        state.candidate_public_bound, state.candidate_loaded_bound, state.fresh_loaded_identity_bound,
        state.public_stage_bound, state.guest_helper_reverified,
        state.current_source_candidate_bound, state.broker_materialized_from_head,
        state.stdin_fifo_verified, state.host_preflight_passed, state.host_credential_ready,
        state.host_setup_started,
        state.host_activated, state.host_status_initial_verified, state.initial_lease_sufficient,
        state.host_status_before_withdraw_verified, state.withdraw_lease_sufficient,
        state.host_lease_renewed_after_helper_arm, state.host_renewed_lease_fresh,
        state.host_withdraw_called_after_helper_armed, state.host_withdrawn_verified,
        state.host_rollback_verified, state.host_retired_verified, state.broker_host_fed,
        state.broker_started, state.broker_armed, state.broker_released,
        state.broker_exited_zero, state.helper_initial_ready, state.helper_withdraw_armed,
        state.helper_recovered, state.helper_positive_sequence_verified,
        state.helper_exited_zero,
    ))


def make_helper_line(state: str, flags: tuple[int, int, int, int, int, int]) -> bytes:
    return (
        "public_corewlan_recovery=" + state + " endpoint_binding=airport-itlwm-bsd "
        "discovery_attempts=1 matching_records=1 alternate_bss_count=2 "
        "alternate_band_count=2 alternate_ready=1 scan_error_present=0 "
        "association_error_present=0 initial_identity_exact=1 "
        f"withdrawal_arm_accepted={flags[0]} pre_withdrawal_identity_exact={flags[1]} "
        f"withdrawal_control_accepted={flags[2]} recovery_same_ssid={flags[3]} "
        f"recovery_different_bss={flags[4]} cleanup_disassociate_attempted={flags[5]}\n"
    ).encode("ascii")


def self_test() -> int:
    if HOST_FED_TIMEOUT_SECONDS < 15 + 15 + 15 + 15:
        raise SystemExit("self-test HOST_FED acknowledgement margin")
    if HOST_SETUP_STARTED_TIMEOUT_SECONDS + SETUP_STARTED_TO_START_BUDGET_SECONDS != \
            HOST_FED_TO_START_BUDGET_SECONDS:
        raise SystemExit("self-test HOST_FED to SETUP_STARTED budget")
    if HOST_FED_TO_START_BUDGET_SECONDS + BROKER_HOST_FED_HANDOFF_MARGIN_SECONDS > \
            BROKER_HOST_FED_CAP_SECONDS:
        raise SystemExit("self-test native HOST_FED margin")
    if (HOST_ACTIVATION_TIMEOUT_SECONDS + HOST_STATUS_TIMEOUT_SECONDS +
            BROKER_START_CONTROL_TIMEOUT_SECONDS + BROKER_START_SAFETY_SECONDS
            > SETUP_STARTED_TO_START_BUDGET_SECONDS):
        raise SystemExit("self-test SETUP_STARTED to START budget")
    if START_TO_ARM_TIMEOUT_SECONDS + BROKER_CONTROL_TIMEOUT_SECONDS >= \
            BROKER_STARTED_TO_ARM_CAP_SECONDS:
        raise SystemExit("self-test STARTED to ARM budget")
    if (ARM_TO_WITHDRAW_TIMEOUT_SECONDS + POST_ARM_RELEASE_PATH_SECONDS +
            ARMED_PHASE_SAFETY_SECONDS >=
            HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS):
        raise SystemExit("self-test ARMED to RELEASE budget")
    if HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS + ARMED_NATIVE_ACK_HANDOFF_MARGIN_SECONDS > \
            BROKER_ARMED_TO_RELEASE_CAP_SECONDS:
        raise SystemExit("self-test native ARMED acknowledgement margin")
    if BROKER_STARTED_TO_ARM_CAP_SECONDS + BROKER_ARMED_TO_RELEASE_CAP_SECONDS >= \
            BROKER_POST_START_CAP_SECONDS:
        raise SystemExit("self-test post-START broker budget")
    if (ARM_TO_WITHDRAW_TIMEOUT_SECONDS + POST_ARM_RELEASE_PATH_SECONDS +
            ARMED_PHASE_SAFETY_SECONDS >= HELPER_WITHDRAW_CONTROL_TIMEOUT_SECONDS):
        raise SystemExit("self-test helper withdrawal-control margin")
    if (MINIMUM_INITIAL_LEASE_REMAINING_SECONDS != 285 or
            MINIMUM_INITIAL_LEASE_REMAINING_SECONDS >= LEASE_SECONDS):
        raise SystemExit("self-test initial fresh lease floor")
    if MINIMUM_WITHDRAW_LEASE_REMAINING_SECONDS != (
            HOST_WITHDRAW_TIMEOUT_SECONDS + BROKER_CONTROL_TIMEOUT_SECONDS +
            HELPER_RECOVERY_TIMEOUT_SECONDS + WATCHDOG_HANDOFF_RESERVE_SECONDS):
        raise SystemExit("self-test withdrawal reserve")
    if MINIMUM_WITHDRAW_LEASE_REMAINING_SECONDS >= LEASE_SECONDS:
        raise SystemExit("self-test renewed lease budget")
    if (MINIMUM_RENEWED_LEASE_REMAINING_SECONDS != 285 or
            MINIMUM_RENEWED_LEASE_REMAINING_SECONDS >= LEASE_SECONDS):
        raise SystemExit("self-test fresh renewed lease floor")
    if WATCHDOG_RETIRE_PROOF_TIMEOUT_SECONDS < (
            LEASE_SECONDS + ROLLBACK_RESTORE_TIMEOUT_SECONDS):
        raise SystemExit("self-test watchdog retirement proof")
    target = parse_hash_only_status(
        b"LABAP_BSS_STATUS schema=tahoe-labap-bss-status/v1 active=1 "
        b"target_ssid_sha256=" + b"a" * 64 + b" target_bssid_sha256=" + b"b" * 64 +
        b" lease_seconds=300 lease_remaining_seconds=285\n", "self-test"
    )
    if target.remaining_seconds != 285:
        raise SystemExit("self-test host status")
    try:
        parse_hash_only_status(
            b"LABAP_BSS_STATUS schema=tahoe-labap-bss-status/v1 active=1 "
            b"target_ssid_sha256=" + b"a" * 64 + b" target_bssid_sha256=" + b"b" * 64 +
            b" lease_seconds=299 lease_remaining_seconds=285\n", "self-test"
        )
    except RunnerError:
        pass
    else:
        raise SystemExit("self-test nonexact lease accepted")
    HelperOutput._validate_line(make_helper_line("initial-ready", (0, 0, 0, 0, 0, 0)), "initial-ready")
    HelperOutput._validate_line(make_helper_line("withdraw-armed", (1, 1, 0, 0, 0, 0)), "withdraw-armed")
    HelperOutput._validate_line(make_helper_line("recovered", (1, 1, 1, 1, 1, 1)), "recovered")
    try:
        HelperOutput._validate_line(make_helper_line("withdraw-armed", (0, 1, 0, 0, 0, 0)), "withdraw-armed")
    except RunnerError:
        pass
    else:
        raise SystemExit("self-test malformed helper flags accepted")
    order = ["initial-ready", "withdraw-armed", "recovered"]
    if order.index("withdraw-armed") <= order.index("initial-ready"):
        raise SystemExit("self-test withdrawal ordering")
    print("PASS: Tahoe IWN public recovery supervisor self-test")
    return 0


def parse_arguments(argv: list[str]) -> tuple[Optional[Config], bool]:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-receipt", type=Path)
    parser.add_argument("--public-recovery-receipt", type=Path)
    parser.add_argument("--loaded-identity", type=Path)
    parser.add_argument("--stage-report", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        if any(value is not None for value in (
            args.candidate_receipt, args.public_recovery_receipt, args.loaded_identity,
            args.stage_report, args.output,
        )):
            parser.error("--self-test accepts no runtime inputs")
        return None, True
    if any(value is None for value in (
        args.candidate_receipt, args.public_recovery_receipt, args.loaded_identity,
        args.stage_report, args.output,
    )):
        parser.error("all receipt and output arguments are required")
    return Config(
        args.candidate_receipt, args.public_recovery_receipt, args.loaded_identity,
        args.stage_report, args.output,
    ), False


def install_interrupt_handler() -> None:
    def interrupted(_signum: int, _frame: object) -> None:
        raise RunnerError("interrupted")

    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)


def main(argv: Optional[list[str]] = None) -> int:
    config, self_test_requested = parse_arguments(sys.argv[1:] if argv is None else argv)
    if self_test_requested:
        return self_test()
    assert config is not None
    state = RuntimeState()
    supervisor: Optional[Supervisor] = None
    report_path: Optional[Path] = None
    try:
        report_path = require_fresh_output(config.output)
        config.output = report_path
        install_interrupt_handler()
        supervisor = Supervisor(config, state)
        supervisor.execute()
    except RunnerError as error:
        state.failure_phase = error.phase
        state.result = "INCONCLUSIVE"
    except (OSError, subprocess.SubprocessError):
        state.failure_phase = "controller-error"
        state.result = "INCONCLUSIVE"
    except Exception:
        state.failure_phase = "controller-error"
        state.result = "INCONCLUSIVE"
    finally:
        if supervisor is not None:
            try:
                supervisor.cleanup_after_failure()
            except Exception:
                if state.result == "PASS":
                    state.result = "INCONCLUSIVE"
                    state.failure_phase = "cleanup-error"
        if state.result == "PASS" and not is_complete_pass(state):
            state.result = "INCONCLUSIVE"
            state.failure_phase = "completion-check"
    if report_path is None:
        return 2
    try:
        write_report(report_path, state)
    except RunnerError:
        return 2
    return 0 if state.result == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
