#!/usr/bin/env python3
"""Run one opaque-input direct-IWN-SAE laboratory observation.

This is deliberately separate from the saved-profile/WCL runners.  It never
uses a wireless profile or framework API, does not scan, does not toggle a
radio, and does not modify routes.  One fixed-size request is forwarded as the
stdin of one constrained remote UserClient process; the sealed driver trace is
the only authority for the on-air result.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shlex
import stat
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Optional

from capture_tahoe_iwn_direct_sae_runtime_artifact_receipt import (
    ARTIFACT_DIR_RE,
    PINNED_HOST_KEY_FINGERPRINT,
    ROOT_ARTIFACT_DIR,
    ROOT_ARTIFACT_PARENT,
    SCHEMA_VERSION as ARTIFACT_RECEIPT_SCHEMA,
    load_artifact_receipt,
    trusted_host_environment,
)
from capture_tahoe_iwn_lab_loaded_identity import (
    PINNED_QEMU_BUILD,
    PINNED_QEMU_GUEST,
    PINNED_QEMU_HOST_KEY,
    PINNED_QEMU_PORT,
)


RUNTIME_SCHEMA = "itlwm-tahoe-iwn-direct-isae-runtime/v2"
DIRECT_CLIENT_NAME = "airport_itlwm_iwn_direct_sae_lab_client"
TRACE_CLIENT_NAME = "airport_itlwm_post_plti_trace"
SHA256_RE = re.compile(r"[0-9a-f]{64}")
UTC_SECONDS_RE = re.compile(
    r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\+00:00"
)
LAB_QUERY_STATUS_RE = re.compile(
    r"lab-client=(ready|not-ready|unsupported|query-failed|open-unavailable)\n"
)
LAB_SUBMIT_STATUS_RE = re.compile(
    r"lab-client=(rejected|not-ready|unsupported|query-failed|open-unavailable)\n"
    r"|lab-client=(queued)\nlab-client-outcome="
    r"(pending|started|rejected-precondition|rejected-request-begin|"
    r"rejected-association-owner|rejected-stage|"
    r"rejected-auth-type|rejected-scan-resume|cancelled|query-failed)\n"
)
DIRECT_VERDICTS = {
    "DIRECT_SAE_4WAY_PORT_VALID", "BRANCH_NOT_OBSERVED",
    "FRESH_SCAN_NOT_OBSERVED", "REQUEST_NO_BSS_SELECTION",
    "JOIN_BSS_NOT_OBSERVED", "NODE_MFP_NOT_NEGOTIATED",
    "AUTH_STATE_NOT_OBSERVED", "COMMIT_TX_NOT_COMPLETE",
    "PEER_COMMIT_NOT_ACCEPTED", "CONFIRM_TX_NOT_COMPLETE",
    "PEER_CONFIRM_NOT_VALIDATED", "PMK_NOT_CLAIMED",
    "ASSOC_DESCRIPTOR_NOT_ACCEPTED", "ASSOC_EXCHANGE_NOT_COMPLETE",
    "FOUR_WAY_NOT_COMPLETE", "PMF_PTK_SOFTWARE_CCMP_NOT_OBSERVED",
    "PMF_GTK_SOFTWARE_CCMP_NOT_OBSERVED", "PMF_IGTK_STAGE_NOT_OBSERVED",
    "PMF_IGTK_PUBLICATION_NOT_OBSERVED",
    "PMF_KEYSET_PUBLICATION_NOT_OBSERVED", "BACKEND_UNSUPPORTED",
    "INTEGRITY_INCONCLUSIVE",
}
DIRECT_STAGES = {
    "none", "capture-seal", "fresh-scan", "bss-selection", "join-bss",
    "node-mfp", "auth-state", "commit-tx", "peer-commit", "confirm-tx",
    "peer-confirm", "pmk-claim", "assoc-descriptor", "assoc-exchange",
    "four-way", "pmf-ptk-software-ccmp", "pmf-gtk-software-ccmp",
    "pmf-igtk-stage", "pmf-igtk-publication", "pmf-keyset-publication",
    "port-valid", "unknown",
}


class RunnerError(Exception):
    """A categorical operational failure safe to place in aggregate evidence."""

    def __init__(self, phase: str):
        super().__init__(phase)
        self.phase = phase


@dataclass
class State:
    direct_sha256: str = ""
    trace_sha256: str = ""
    host_dtrace_before: bool = False
    host_dtrace_after: bool = False
    guest_dtrace_before: bool = False
    guest_dtrace_after: bool = False
    artifacts_pre_bound: bool = False
    artifacts_post_bound: bool = False
    readiness_observed: bool = False
    submission_category: str = "not-invoked"
    dispatch_outcome: str = "not-invoked"
    trace_reset_ack: bool = False
    initial_snapshot_synchronized: bool = False
    trace_seal_ack: bool = False
    trace_final_disabled: bool = False
    report_one_read: bool = False
    report_two_read: bool = False
    report_double_read_stable: bool = False
    capture_generation: int = 0
    trace_backend: str = "unknown"
    trace_entry_count: int = 0
    trace_dropped: int = 0
    trace_integrity: str = "inconclusive"
    trace_episode_count: int = 0
    trace_active_episode: int = 0
    trace_verdict: str = "INTEGRITY_INCONCLUSIVE"
    trace_first_missing_stage: str = "unknown"
    trace_reset_may_be_active: bool = False
    trace_cleanup_fallback_attempted: bool = False
    trace_cleanup_seal_confirmed: bool = False
    trace_cleanup_off_attempted: bool = False
    trace_cleanup_disabled_confirmed: bool = False
    result: str = "INCONCLUSIVE"
    failure_phase: str = "preflight"
    operational_failure: bool = False


def require(condition: bool, phase: str) -> None:
    if not condition:
        raise RunnerError(phase)


def is_u32(value: object) -> bool:
    return type(value) is int and 0 <= value <= 0xFFFFFFFF


def require_safe_artifact_dir(value: str) -> str:
    if not isinstance(value, str) or ARTIFACT_DIR_RE.fullmatch(value) is None:
        raise RunnerError("artifact-directory")
    return value


def require_fifo_stdin() -> BinaryIO:
    try:
        descriptor = sys.stdin.fileno()
        mode = os.fstat(descriptor).st_mode
    except OSError as error:
        raise RunnerError("stdin-descriptor") from error
    if not stat.S_ISFIFO(mode):
        raise RunnerError("stdin-not-pipe")
    return sys.stdin.buffer


def require_fresh_output(path: Path) -> None:
    if not path.is_absolute() or path.name in {"", ".", ".."}:
        raise RunnerError("output-path")
    try:
        parent = path.parent.lstat()
    except OSError as error:
        raise RunnerError("output-parent") from error
    if stat.S_ISLNK(parent.st_mode) or not stat.S_ISDIR(parent.st_mode):
        raise RunnerError("output-parent")
    if path.exists() or path.is_symlink():
        raise RunnerError("output-not-fresh")
    try:
        os.mkdir(path, 0o700)
    except OSError as error:
        raise RunnerError("output-create") from error
    os.chmod(path, 0o700)


def host_dtrace_zero() -> bool:
    result = subprocess.run(
        ["/usr/bin/pgrep", "-x", "dtrace"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
        text=True,
        env=trusted_host_environment(),
    )
    return result.returncode == 1 and result.stdout == ""


REMOTE_VERIFY_LIBRARY = r'''
set -euo pipefail
root_owned_nonwritable() {
  path="$1"
  owner="$(/usr/bin/stat -f '%u' "$path")"
  mode="$(/usr/bin/stat -f '%Lp' "$path")"
  test "$owner" = 0
  printf '%s\n' "$mode" | /usr/bin/grep -Eq '^[0-7][0145][0145]$'
}
root_owned_exact_mode() {
  path="$1"
  expected_mode="$2"
  test "$(/usr/bin/stat -f '%u' "$path")" = 0
  test "$(/usr/bin/stat -f '%Lp' "$path")" = "$expected_mode"
}
verify_root_ancestry() {
  artifact_dir="$1"
  test "$artifact_dir" = "__ROOT_ARTIFACT_DIR__"
  for root_path in /private /private/var /private/var/db __ROOT_ARTIFACT_PARENT__; do
    test -d "$root_path" && test ! -L "$root_path"
    root_owned_nonwritable "$root_path"
  done
  test -d "$artifact_dir" && test ! -L "$artifact_dir"
  physical_dir="$(CDPATH= cd -P -- "$artifact_dir" && pwd -P)"
  test "$physical_dir" = "$artifact_dir"
  root_owned_exact_mode "$artifact_dir" 700
}
verify_tool() {
  artifact_dir="$1"
  tool_name="$2"
  expected_sha256="$3"
  case "$tool_name" in
    airport_itlwm_iwn_direct_sae_lab_client|airport_itlwm_post_plti_trace) ;;
    *) exit 64 ;;
  esac
  printf '%s\n' "$expected_sha256" | /usr/bin/grep -Eq '^[0-9a-f]{64}$' || exit 64
  verify_root_ancestry "$artifact_dir"
  tool="$artifact_dir/$tool_name"
  test -f "$tool" && test ! -L "$tool" && test -x "$tool"
  root_owned_exact_mode "$tool" 500
  observed="$(LC_ALL=C PATH=/usr/bin:/bin /usr/bin/shasum -a 256 "$tool" |
    /usr/bin/awk -v path="$tool" '
      function hex64(value) { return length(value) == 64 && value !~ /[^0-9a-f]/ }
      NR == 1 && NF == 2 && $2 == path && hex64($1) { value = $1; next }
      { invalid = 1 }
      END { if (NR != 1 || invalid || value == "") exit 1; print value }
    ')"
  test "$observed" = "$expected_sha256"
}
'''.replace("__ROOT_ARTIFACT_PARENT__", ROOT_ARTIFACT_PARENT).replace(
    "__ROOT_ARTIFACT_DIR__", ROOT_ARTIFACT_DIR
)

REMOTE_ARTIFACTS_BOUND = REMOTE_VERIFY_LIBRARY + r'''
verify_tool "$1" airport_itlwm_iwn_direct_sae_lab_client "$2"
verify_tool "$1" airport_itlwm_post_plti_trace "$3"
'''

REMOTE_TRACE = REMOTE_VERIFY_LIBRARY + r'''
artifact_dir="$1"
expected_sha256="$2"
shift 2
first=""
second=""
if [ "$#" -ge 1 ]; then
  first="$1"
fi
if [ "$#" -ge 2 ]; then
  second="$2"
fi
case "$#:$first:$second" in
  1:reset:|1:seal:|1:off:) ;;
  2:get:control|2:get:snapshot|2:get:iwn-direct-sae-report) ;;
  *) exit 64 ;;
esac
verify_tool "$artifact_dir" airport_itlwm_post_plti_trace "$expected_sha256"
exec "$tool" "$@"
'''

REMOTE_QUERY = REMOTE_VERIFY_LIBRARY + r'''
verify_tool "$1" airport_itlwm_iwn_direct_sae_lab_client "$2"
exec "$tool" --query-ready
'''

REMOTE_SUBMIT = REMOTE_VERIFY_LIBRARY + r'''
artifact_dir="$1"
expected_sha256="$2"
hold_milliseconds="$3"
case "$hold_milliseconds" in ""|*[!0-9]*) exit 64 ;; esac
test "$hold_milliseconds" -ge 1000 && test "$hold_milliseconds" -le 60000
verify_tool "$artifact_dir" airport_itlwm_iwn_direct_sae_lab_client "$expected_sha256"
exec "$tool" --submit-stdin --hold-milliseconds "$hold_milliseconds"
'''

REMOTE_DTRACE_ZERO = r'''
set -u
set +e
matches="$(LC_ALL=C PATH=/usr/bin:/bin /usr/bin/pgrep -x dtrace 2>/dev/null)"
status="$?"
set -e
test "$status" = 1
test -z "$matches"
'''

REMOTE_GUEST_BUILD = r'''
set -eu
test "$(/usr/bin/sw_vers -buildVersion 2>/dev/null)" = "$1"
'''


class PinnedGuest:
    """Pinned SSH transport whose no-submit calls always supply their own stdin."""

    def __init__(self) -> None:
        self.known_hosts: Optional[Path] = None
        self.base: list[str] = []

    def open(self) -> None:
        handle = tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            prefix="aiam-isae-runtime-known-hosts-",
            delete=False,
        )
        try:
            handle.write(PINNED_QEMU_HOST_KEY + "\n")
            handle.close()
            os.chmod(handle.name, 0o600)
            verified = subprocess.run(
                ["/usr/bin/ssh-keygen", "-lf", handle.name, "-E", "sha256"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                text=True,
                env=trusted_host_environment(),
            )
            fields = verified.stdout.split()
            if (
                verified.returncode != 0 or len(fields) < 2 or
                fields[1] != PINNED_HOST_KEY_FINGERPRINT
            ):
                raise RunnerError("hostkey-pin")
            self.known_hosts = Path(handle.name)
            self.base = [
                "/usr/bin/ssh", "-F", "/dev/null", "-T", "-p", str(PINNED_QEMU_PORT),
                "-o", "BatchMode=yes", "-o", "ConnectTimeout=8",
                "-o", "StrictHostKeyChecking=yes",
                "-o", f"UserKnownHostsFile={self.known_hosts}",
                "-o", "GlobalKnownHostsFile=/dev/null",
                "-o", "UpdateHostKeys=no", "-o", "LogLevel=ERROR",
                PINNED_QEMU_GUEST,
            ]
        except RunnerError:
            Path(handle.name).unlink(missing_ok=True)
            raise
        except (OSError, subprocess.SubprocessError) as error:
            Path(handle.name).unlink(missing_ok=True)
            raise RunnerError("hostkey-pin") from error

    def close(self) -> None:
        if self.known_hosts is not None:
            self.known_hosts.unlink(missing_ok=True)
        self.known_hosts = None
        self.base = []

    def run_root_script(
        self, script: str, arguments: list[str], timeout: int = 20
    ) -> subprocess.CompletedProcess[bytes]:
        try:
            return subprocess.run(
                [*self.base, "/usr/bin/sudo", "-n", "/bin/bash", "-s", "--", *arguments],
                input=script.encode("utf-8"),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=timeout,
                env=trusted_host_environment(),
            )
        except subprocess.TimeoutExpired as error:
            raise RunnerError("guest-root-script-timeout") from error
        except OSError as error:
            raise RunnerError("guest-root-script") from error

    def run_root_command(
        self, command: str, stdin: BinaryIO, timeout: int
    ) -> subprocess.CompletedProcess[bytes]:
        try:
            return subprocess.run(
                [*self.base, command],
                stdin=stdin,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=timeout,
                env=trusted_host_environment(),
            )
        except subprocess.TimeoutExpired as error:
            raise RunnerError("submit-timeout") from error
        except OSError as error:
            raise RunnerError("guest-root-submit") from error


def decoded_stdout(result: subprocess.CompletedProcess[bytes], phase: str) -> str:
    try:
        return result.stdout.decode("utf-8", "strict")
    except UnicodeDecodeError as error:
        raise RunnerError(phase) from error


def remote_artifacts_bound(
    guest: PinnedGuest, artifact_dir: str, direct_digest: str, trace_digest: str
) -> bool:
    result = guest.run_root_script(
        REMOTE_ARTIFACTS_BOUND, [artifact_dir, direct_digest, trace_digest]
    )
    return result.returncode == 0 and result.stdout == b""


def remote_trace(
    guest: PinnedGuest, artifact_dir: str, trace_digest: str, arguments: tuple[str, ...]
) -> str:
    result = guest.run_root_script(REMOTE_TRACE, [artifact_dir, trace_digest, *arguments])
    if result.returncode != 0:
        raise RunnerError("trace-command")
    return decoded_stdout(result, "trace-output")


def remote_query(
    guest: PinnedGuest, artifact_dir: str, direct_digest: str
) -> tuple[int, str]:
    result = guest.run_root_script(REMOTE_QUERY, [artifact_dir, direct_digest])
    return result.returncode, decoded_stdout(result, "readiness-output")


def remote_submit(
    guest: PinnedGuest, artifact_dir: str, direct_digest: str,
    hold_milliseconds: int, stream: BinaryIO
) -> tuple[int, str]:
    command = "exec /usr/bin/sudo -n /bin/bash -c " + shlex.quote(REMOTE_SUBMIT)
    command += " -- " + " ".join(
        shlex.quote(value)
        for value in (artifact_dir, direct_digest, str(hold_milliseconds))
    )
    result = guest.run_root_command(
        command, stream, timeout=hold_milliseconds // 1000 + 20
    )
    return result.returncode, decoded_stdout(result, "submit-output")


def parse_query_status(text: str) -> str:
    match = LAB_QUERY_STATUS_RE.fullmatch(text)
    if match is None:
        raise RunnerError("lab-client-output")
    return match.group(1)


def parse_submit_status(text: str) -> tuple[str, str]:
    match = LAB_SUBMIT_STATUS_RE.fullmatch(text)
    if match is None:
        raise RunnerError("lab-client-output")
    if match.group(1) is not None:
        return match.group(1), "not-invoked"
    return match.group(2), match.group(3)


def parse_control(
    text: str, enable: int, reset: int, seal: int, expected_generation: int
) -> int:
    try:
        fields: dict[str, str] = {}
        for token in text.strip().split():
            key, value = token.split("=", 1)
            if key in fields:
                raise ValueError("duplicate")
            fields[key] = value
        expected = {
            "seq", "applied", "enable", "reset", "seal", "bound", "generation",
            "backend",
        }
        if set(fields) != expected:
            raise ValueError("shape")
        if any(not value.isdecimal() or int(value) > 0xFFFFFFFF for value in fields.values()):
            raise ValueError("number")
        generation = int(fields["generation"])
        if (
            int(fields["seq"]) == 0 or fields["applied"] != "1" or
            fields["bound"] != "1" or fields["backend"] != "1" or
            (int(fields["enable"]), int(fields["reset"]), int(fields["seal"])) !=
            (enable, reset, seal) or generation == 0 or
            (expected_generation != 0 and generation != expected_generation)
        ):
            raise ValueError("control")
        return generation
    except (KeyError, ValueError):
        raise RunnerError("trace-control-output") from None


def parse_snapshot(text: str, generation: int, enabled: int) -> dict[str, int | str]:
    try:
        fields: dict[str, str] = {}
        for token in text.strip().split():
            key, value = token.split("=", 1)
            if key in fields:
                raise ValueError("duplicate")
            fields[key] = value
        expected = {
            "version", "capture_generation", "backend", "enabled", "target_bound",
            "active_episode", "episode_count", "entry_count", "dropped",
            "first_sequence", "latest_sequence",
        }
        if set(fields) != expected:
            raise ValueError("shape")
        numbers = expected - {"backend"}
        if any(not fields[key].isdecimal() or int(fields[key]) > 0xFFFFFFFF for key in numbers):
            raise ValueError("number")
        if (
            fields["version"] != "7" or int(fields["capture_generation"]) != generation or
            fields["backend"] != "IWN" or int(fields["enabled"]) != enabled or
            fields["target_bound"] != "1" or fields["active_episode"] != "0" or
            fields["dropped"] != "0"
        ):
            raise ValueError("snapshot")
        return {
            "entry_count": int(fields["entry_count"]),
            "dropped": int(fields["dropped"]),
            "episode_count": int(fields["episode_count"]),
            "active_episode": int(fields["active_episode"]),
        }
    except (KeyError, ValueError):
        raise RunnerError("trace-snapshot-output") from None


def parse_direct_report(text: str, generation: int) -> dict[str, int | str]:
    try:
        fields: dict[str, str] = {}
        for token in text.split():
            key, value = token.split("=", 1)
            if key in fields:
                raise ValueError("duplicate")
            fields[key] = value
        expected = {
            "capture_generation", "backend", "entries", "integrity", "episode_count",
            "active_episode", "iwn_direct_sae_verdict", "first_missing_stage",
        }
        if set(fields) != expected:
            raise ValueError("shape")
        for key in ("capture_generation", "entries", "episode_count", "active_episode"):
            if not fields[key].isdecimal() or int(fields[key]) > 0xFFFFFFFF:
                raise ValueError("number")
        if int(fields["capture_generation"]) != generation or fields["backend"] != "IWN":
            raise ValueError("binding")
        if (
            fields["integrity"] not in {"ok", "inconclusive"} or
            fields["iwn_direct_sae_verdict"] not in DIRECT_VERDICTS or
            fields["first_missing_stage"] not in DIRECT_STAGES
        ):
            raise ValueError("classification")
        return {
            "entry_count": int(fields["entries"]),
            "integrity": fields["integrity"],
            "episode_count": int(fields["episode_count"]),
            "active_episode": int(fields["active_episode"]),
            "verdict": fields["iwn_direct_sae_verdict"],
            "first_missing_stage": fields["first_missing_stage"],
        }
    except (KeyError, ValueError):
        raise RunnerError("direct-report-output") from None


def wait_control(
    guest: PinnedGuest, artifact_dir: str, trace_digest: str,
    enable: int, reset: int, seal: int, expected_generation: int, attempts: int
) -> int:
    for _ in range(attempts):
        try:
            output = remote_trace(
                guest, artifact_dir, trace_digest, ("get", "control")
            )
            return parse_control(output, enable, reset, seal, expected_generation)
        except RunnerError:
            time.sleep(1)
    raise RunnerError("trace-control-ack")


def wait_snapshot(
    guest: PinnedGuest, artifact_dir: str, trace_digest: str,
    generation: int, enabled: int, attempts: int
) -> dict[str, int | str]:
    for _ in range(attempts):
        try:
            output = remote_trace(
                guest, artifact_dir, trace_digest, ("get", "snapshot")
            )
            return parse_snapshot(output, generation, enabled)
        except RunnerError:
            time.sleep(1)
    raise RunnerError("trace-snapshot")


def guest_dtrace_zero(guest: PinnedGuest) -> bool:
    result = guest.run_root_script(REMOTE_DTRACE_ZERO, [])
    return result.returncode == 0 and result.stdout == b""


def guest_build_is_pinned(guest: PinnedGuest) -> bool:
    result = guest.run_root_script(REMOTE_GUEST_BUILD, [PINNED_QEMU_BUILD])
    return result.returncode == 0 and result.stdout == b""


def positive_trace(state: State) -> bool:
    return (
        state.artifacts_pre_bound and state.artifacts_post_bound and
        state.host_dtrace_before and state.host_dtrace_after and
        state.guest_dtrace_before and state.guest_dtrace_after and
        state.readiness_observed and state.submission_category == "queued" and
        state.dispatch_outcome == "started" and
        state.trace_reset_ack and state.initial_snapshot_synchronized and
        state.trace_seal_ack and state.trace_final_disabled and
        not state.trace_cleanup_fallback_attempted and
        state.report_one_read and state.report_two_read and
        state.report_double_read_stable and state.capture_generation > 0 and
        state.trace_backend == "IWN" and state.trace_integrity == "ok" and
        state.trace_entry_count > 0 and state.trace_dropped == 0 and
        state.trace_episode_count == 1 and state.trace_active_episode == 0 and
        state.trace_verdict == "DIRECT_SAE_4WAY_PORT_VALID" and
        state.trace_first_missing_stage == "none"
    )


def evidence_document(state: State) -> dict[str, object]:
    return {
        "schema": RUNTIME_SCHEMA,
        "created_at_utc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
        "artifact_binding": {
            "receipt_schema": ARTIFACT_RECEIPT_SCHEMA,
            "direct_sae_client_sha256": state.direct_sha256 if SHA256_RE.fullmatch(state.direct_sha256) else "",
            "trace_client_sha256": state.trace_sha256 if SHA256_RE.fullmatch(state.trace_sha256) else "",
            "pre_bound": state.artifacts_pre_bound,
            "post_bound": state.artifacts_post_bound,
        },
        "input_handling": {
            "stdin_only": True,
            "runner_read_or_persisted_request": False,
            "tty_rejected": True,
            "helper_query_before_read": True,
            "readiness_observed": state.readiness_observed,
            "submit_category": state.submission_category,
            "dispatch_outcome": state.dispatch_outcome,
        },
        "trace": {
            "reset_may_be_active": state.trace_reset_may_be_active,
            "reset_acknowledged": state.trace_reset_ack,
            "initial_snapshot_synchronized": state.initial_snapshot_synchronized,
            "seal_acknowledged": state.trace_seal_ack,
            "final_control_disabled": state.trace_final_disabled,
            "cleanup_fallback_attempted": state.trace_cleanup_fallback_attempted,
            "cleanup_seal_confirmed": state.trace_cleanup_seal_confirmed,
            "cleanup_off_attempted": state.trace_cleanup_off_attempted,
            "cleanup_disabled_confirmed": state.trace_cleanup_disabled_confirmed,
            "report_one_read": state.report_one_read,
            "report_two_read": state.report_two_read,
            "double_read_stable": state.report_double_read_stable,
            "capture_generation": state.capture_generation,
            "backend": state.trace_backend,
            "entry_count": state.trace_entry_count,
            "dropped_entries": state.trace_dropped,
            "integrity": state.trace_integrity,
            "episode_count": state.trace_episode_count,
            "active_episode": state.trace_active_episode,
            "verdict": state.trace_verdict,
            "first_missing_stage": state.trace_first_missing_stage,
        },
        "environment": {
            "disposable_guest_only": True,
            "physical_host_touched": False,
            "profile_or_route_changed": False,
            "wireless_identity_collected": False,
            "network_input_collected": False,
            "host_dtrace_zero_before": state.host_dtrace_before,
            "host_dtrace_zero_after": state.host_dtrace_after,
            "guest_dtrace_zero_before": state.guest_dtrace_before,
            "guest_dtrace_zero_after": state.guest_dtrace_after,
        },
        "retention": {
            "opaque_request_retained": False,
            "unparsed_transport_output_retained": False,
            "artifact_path_retained": False,
        },
        "result": state.result,
        "failure_phase": state.failure_phase,
        "non_claims": [
            "saved-profile or application-join provenance",
            "WCL product join",
            "data-plane traffic",
            "reconnect roaming or multi-AP replacement",
            "physical-host validation",
            "exact loaded-kext source identity",
        ],
    }


def require_exact_keys(actual: object, expected: set[str], label: str) -> dict[str, object]:
    if not isinstance(actual, dict) or set(actual) != expected:
        raise ValueError(f"{label} shape")
    return actual


def require_bool(section: dict[str, object], key: str, label: str) -> bool:
    value = section.get(key)
    if type(value) is not bool:
        raise ValueError(f"{label}.{key}")
    return value


def require_evidence_u32(section: dict[str, object], key: str, label: str) -> int:
    value = section.get(key)
    if not is_u32(value):
        raise ValueError(f"{label}.{key}")
    return int(value)


def validate_evidence_document(document: object) -> dict[str, object]:
    root = require_exact_keys(
        document,
        {
            "schema", "created_at_utc", "artifact_binding", "input_handling",
            "trace", "environment", "retention", "result", "failure_phase",
            "non_claims",
        },
        "document",
    )
    if root["schema"] != RUNTIME_SCHEMA or not isinstance(root["created_at_utc"], str):
        raise ValueError("document schema")
    timestamp = str(root["created_at_utc"])
    if UTC_SECONDS_RE.fullmatch(timestamp) is None:
        raise ValueError("document timestamp")
    try:
        parsed_timestamp = dt.datetime.fromisoformat(timestamp)
    except ValueError as error:
        raise ValueError("document timestamp") from error
    if parsed_timestamp.tzinfo != dt.timezone.utc or parsed_timestamp.microsecond != 0:
        raise ValueError("document timestamp")
    artifacts = require_exact_keys(
        root["artifact_binding"],
        {
            "receipt_schema", "direct_sae_client_sha256", "trace_client_sha256",
            "pre_bound", "post_bound",
        },
        "artifact_binding",
    )
    if artifacts["receipt_schema"] != ARTIFACT_RECEIPT_SCHEMA:
        raise ValueError("artifact receipt schema")
    for key in ("direct_sae_client_sha256", "trace_client_sha256"):
        if not isinstance(artifacts[key], str) or SHA256_RE.fullmatch(artifacts[key]) is None:
            raise ValueError(f"artifact binding {key}")
    for key in ("pre_bound", "post_bound"):
        require_bool(artifacts, key, "artifact_binding")
    input_handling = require_exact_keys(
        root["input_handling"],
        {
            "stdin_only", "runner_read_or_persisted_request", "tty_rejected",
            "helper_query_before_read", "readiness_observed", "submit_category",
            "dispatch_outcome",
        },
        "input_handling",
    )
    if (
        input_handling["stdin_only"] is not True or
        input_handling["runner_read_or_persisted_request"] is not False or
        input_handling["tty_rejected"] is not True or
        input_handling["helper_query_before_read"] is not True or
        type(input_handling["readiness_observed"]) is not bool or
        input_handling["submit_category"] not in {
            "queued", "rejected", "not-ready", "unsupported", "query-failed",
            "open-unavailable", "not-invoked",
        } or
        input_handling["dispatch_outcome"] not in {
            "pending", "started", "rejected-precondition",
            "rejected-request-begin", "rejected-association-owner",
            "rejected-stage", "rejected-auth-type",
            "rejected-scan-resume", "cancelled", "query-failed",
            "not-invoked",
        }
    ):
        raise ValueError("input handling")
    trace = require_exact_keys(
        root["trace"],
        {
            "reset_may_be_active", "reset_acknowledged", "initial_snapshot_synchronized",
            "seal_acknowledged", "final_control_disabled", "report_one_read",
            "cleanup_fallback_attempted", "cleanup_seal_confirmed",
            "cleanup_off_attempted", "cleanup_disabled_confirmed", "report_two_read",
            "double_read_stable", "capture_generation",
            "backend", "entry_count", "dropped_entries", "integrity",
            "episode_count", "active_episode", "verdict", "first_missing_stage",
        },
        "trace",
    )
    for key in (
        "reset_may_be_active", "reset_acknowledged",
        "initial_snapshot_synchronized", "seal_acknowledged", "final_control_disabled",
        "cleanup_fallback_attempted", "cleanup_seal_confirmed",
        "cleanup_off_attempted", "cleanup_disabled_confirmed", "report_one_read",
        "report_two_read", "double_read_stable",
    ):
        require_bool(trace, key, "trace")
    for key in (
        "capture_generation", "entry_count", "dropped_entries", "episode_count",
        "active_episode",
    ):
        require_evidence_u32(trace, key, "trace")
    if (
        trace["backend"] not in {"IWN", "unknown"} or
        trace["integrity"] not in {"ok", "inconclusive"} or
        trace["verdict"] not in DIRECT_VERDICTS or
        trace["first_missing_stage"] not in DIRECT_STAGES
    ):
        raise ValueError("trace classification")
    if (
        (trace["reset_acknowledged"] and not trace["reset_may_be_active"]) or
        (trace["cleanup_fallback_attempted"] and not trace["reset_may_be_active"]) or
        (trace["cleanup_seal_confirmed"] and not trace["cleanup_fallback_attempted"]) or
        (trace["cleanup_off_attempted"] and not trace["cleanup_fallback_attempted"]) or
        (trace["cleanup_disabled_confirmed"] and not (
            trace["cleanup_seal_confirmed"] or trace["cleanup_off_attempted"]
        ))
    ):
        raise ValueError("trace cleanup invariants")
    environment = require_exact_keys(
        root["environment"],
        {
            "disposable_guest_only", "physical_host_touched", "profile_or_route_changed",
            "wireless_identity_collected", "network_input_collected",
            "host_dtrace_zero_before", "host_dtrace_zero_after",
            "guest_dtrace_zero_before", "guest_dtrace_zero_after",
        },
        "environment",
    )
    expected_environment = {
        "disposable_guest_only": True,
        "physical_host_touched": False,
        "profile_or_route_changed": False,
        "wireless_identity_collected": False,
        "network_input_collected": False,
    }
    for key, expected in expected_environment.items():
        if environment[key] is not expected:
            raise ValueError(f"environment {key}")
    for key in (
        "host_dtrace_zero_before", "host_dtrace_zero_after",
        "guest_dtrace_zero_before", "guest_dtrace_zero_after",
    ):
        require_bool(environment, key, "environment")
    retention = require_exact_keys(
        root["retention"],
        {
            "opaque_request_retained", "unparsed_transport_output_retained",
            "artifact_path_retained",
        },
        "retention",
    )
    if any(value is not False for value in retention.values()):
        raise ValueError("retention")
    if root["result"] not in {"PASS", "INCONCLUSIVE"}:
        raise ValueError("result")
    if not isinstance(root["failure_phase"], str) or re.fullmatch(
        r"[a-z][a-z0-9-]*", root["failure_phase"]
    ) is None:
        raise ValueError("failure phase")
    non_claims = [
        "saved-profile or application-join provenance",
        "WCL product join",
        "data-plane traffic",
        "reconnect roaming or multi-AP replacement",
        "physical-host validation",
        "exact loaded-kext source identity",
    ]
    if root["non_claims"] != non_claims:
        raise ValueError("non-claims")
    if root["result"] == "PASS":
        required_true = (
            artifacts["pre_bound"], artifacts["post_bound"],
            environment["host_dtrace_zero_before"],
            environment["host_dtrace_zero_after"],
            environment["guest_dtrace_zero_before"],
            environment["guest_dtrace_zero_after"],
            trace["reset_acknowledged"], trace["initial_snapshot_synchronized"],
            trace["seal_acknowledged"], trace["final_control_disabled"],
            trace["report_one_read"], trace["report_two_read"],
            trace["double_read_stable"],
        )
        if (
            not all(required_true) or input_handling["readiness_observed"] is not True or
            trace["cleanup_fallback_attempted"] is not False or
            input_handling["submit_category"] != "queued" or
            input_handling["dispatch_outcome"] != "started" or
            trace["capture_generation"] == 0 or trace["backend"] != "IWN" or
            trace["entry_count"] == 0 or trace["dropped_entries"] != 0 or
            trace["integrity"] != "ok" or trace["episode_count"] != 1 or
            trace["active_episode"] != 0 or
            trace["verdict"] != "DIRECT_SAE_4WAY_PORT_VALID" or
            trace["first_missing_stage"] != "none" or root["failure_phase"] != "none"
        ):
            raise ValueError("PASS invariants")
    return root


def write_evidence(output_dir: Optional[Path], state: State) -> None:
    if output_dir is None or not output_dir.is_dir():
        return
    destination = output_dir / "runtime-attestation.json"
    if destination.exists() or destination.is_symlink():
        return
    document = evidence_document(state)
    validate_evidence_document(document)
    descriptor = os.open(
        str(destination), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            descriptor = -1
            handle.write(json.dumps(document, indent=2, sort_keys=True) + "\n")
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    os.chmod(destination, 0o600)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--guest-artifact-dir", required=True)
    parser.add_argument("--artifact-receipt", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--hold-seconds", type=int, default=45)
    parser.add_argument("--settle-seconds", type=int, default=15)
    parser.add_argument("--ack-attempts", type=int, default=20)
    parser.add_argument("--stable-read-delay-seconds", type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.hold_seconds <= 60:
        parser.error("--hold-seconds must be in 1..60")
    if not 1 <= args.settle_seconds <= 60:
        parser.error("--settle-seconds must be in 1..60")
    if not 1 <= args.ack_attempts <= 60:
        parser.error("--ack-attempts must be in 1..60")
    if not 1 <= args.stable_read_delay_seconds <= 10:
        parser.error("--stable-read-delay-seconds must be in 1..10")
    return args


def run(args: argparse.Namespace) -> int:
    state = State()
    output_dir: Optional[Path] = None
    guest: Optional[PinnedGuest] = None
    request_stream = require_fifo_stdin()
    artifact_dir = require_safe_artifact_dir(args.guest_artifact_dir)
    try:
        receipt = load_artifact_receipt(args.artifact_receipt)
    except Exception as error:
        raise RunnerError("artifact-receipt") from error
    state.direct_sha256 = str(receipt["direct_sae_client_sha256"])
    state.trace_sha256 = str(receipt["trace_client_sha256"])
    require_fresh_output(args.out)
    output_dir = args.out

    try:
        require(host_dtrace_zero(), "host-dtrace-preflight")
        state.host_dtrace_before = True
        guest = PinnedGuest()
        guest.open()
        require(guest_dtrace_zero(guest), "guest-dtrace-preflight")
        state.guest_dtrace_before = True
        require(guest_build_is_pinned(guest), "guest-build-pin")
        require(
            remote_artifacts_bound(
                guest, artifact_dir, state.direct_sha256, state.trace_sha256
            ),
            "artifact-binding-pre",
        )
        state.artifacts_pre_bound = True

        state.trace_reset_may_be_active = True
        remote_trace(guest, artifact_dir, state.trace_sha256, ("reset",))
        state.capture_generation = wait_control(
            guest, artifact_dir, state.trace_sha256, 1, 1, 0, 0,
            args.ack_attempts,
        )
        state.trace_reset_ack = True
        initial = wait_snapshot(
            guest, artifact_dir, state.trace_sha256, state.capture_generation, 1,
            args.ack_attempts,
        )
        require(initial["entry_count"] == 0 and initial["episode_count"] == 0, "trace-initial-state")
        state.initial_snapshot_synchronized = True
        state.trace_backend = "IWN"

        query_return, query_stdout = remote_query(
            guest, artifact_dir, state.direct_sha256
        )
        query_category = parse_query_status(query_stdout)
        if query_category == "ready":
            require(query_return == 0, "readiness-status")
            state.readiness_observed = True
            submit_return, submit_stdout = remote_submit(
                guest, artifact_dir, state.direct_sha256,
                args.hold_seconds * 1000, request_stream,
            )
            state.submission_category, state.dispatch_outcome = \
                parse_submit_status(submit_stdout)
            if state.submission_category == "queued":
                require(submit_return == 0, "submit-status")
                if state.dispatch_outcome != "started":
                    state.failure_phase = "dispatch-" + state.dispatch_outcome
                time.sleep(args.settle_seconds)
            else:
                require(
                    state.submission_category in {
                        "rejected", "not-ready", "unsupported", "query-failed",
                        "open-unavailable",
                    } and submit_return != 0,
                    "submit-status",
                )
        else:
            require(
                query_category in {
                    "not-ready", "unsupported", "query-failed", "open-unavailable",
                } and query_return != 0,
                "readiness-status",
            )
            state.failure_phase = "readiness-not-ready"

        remote_trace(guest, artifact_dir, state.trace_sha256, ("seal",))
        state.capture_generation = wait_control(
            guest, artifact_dir, state.trace_sha256, 0, 0, 1,
            state.capture_generation, args.ack_attempts,
        )
        state.trace_seal_ack = True
        final = wait_snapshot(
            guest, artifact_dir, state.trace_sha256, state.capture_generation, 0,
            args.ack_attempts,
        )
        state.trace_final_disabled = True
        state.trace_entry_count = int(final["entry_count"])
        state.trace_dropped = int(final["dropped"])
        state.trace_episode_count = int(final["episode_count"])
        state.trace_active_episode = int(final["active_episode"])

        report_one = remote_trace(
            guest, artifact_dir, state.trace_sha256, ("get", "iwn-direct-sae-report")
        )
        first = parse_direct_report(report_one, state.capture_generation)
        state.report_one_read = True
        require(
            final["entry_count"] == first["entry_count"] and
            final["episode_count"] == first["episode_count"] and
            final["active_episode"] == first["active_episode"],
            "sealed-snapshot-report",
        )
        time.sleep(args.stable_read_delay_seconds)
        report_two = remote_trace(
            guest, artifact_dir, state.trace_sha256, ("get", "iwn-direct-sae-report")
        )
        second = parse_direct_report(report_two, state.capture_generation)
        state.report_two_read = True
        require(report_one == report_two and first == second, "direct-report-double-read")
        state.report_double_read_stable = True
        state.trace_entry_count = int(first["entry_count"])
        state.trace_integrity = str(first["integrity"])
        state.trace_episode_count = int(first["episode_count"])
        state.trace_active_episode = int(first["active_episode"])
        state.trace_verdict = str(first["verdict"])
        state.trace_first_missing_stage = str(first["first_missing_stage"])

        require(
            remote_artifacts_bound(
                guest, artifact_dir, state.direct_sha256, state.trace_sha256
            ),
            "artifact-binding-post",
        )
        state.artifacts_post_bound = True
        require(guest_dtrace_zero(guest), "guest-dtrace-postflight")
        state.guest_dtrace_after = True
        require(host_dtrace_zero(), "host-dtrace-postflight")
        state.host_dtrace_after = True

        if positive_trace(state):
            state.result = "PASS"
            state.failure_phase = "none"
            print("PASS: sealed direct IWN SAE four-way port-valid trace observed")
        else:
            state.result = "INCONCLUSIVE"
            if state.failure_phase == "preflight":
                state.failure_phase = "trace-verdict-diagnostic"
            print("INCONCLUSIVE: sealed direct IWN SAE aggregate retained")
        return 0
    except RunnerError as error:
        state.failure_phase = error.phase
        state.operational_failure = True
        print(f"INCONCLUSIVE: phase={error.phase}", file=sys.stderr)
        return 1
    finally:
        if guest is not None:
            if state.trace_reset_may_be_active and not state.trace_final_disabled:
                state.trace_cleanup_fallback_attempted = True
                cleanup_attempts = min(args.ack_attempts, 5)
                try:
                    remote_trace(guest, artifact_dir, state.trace_sha256, ("seal",))
                    wait_control(
                        guest, artifact_dir, state.trace_sha256, 0, 0, 1,
                        state.capture_generation, cleanup_attempts,
                    )
                    state.trace_cleanup_seal_confirmed = True
                    state.trace_cleanup_disabled_confirmed = True
                except Exception:
                    state.trace_cleanup_off_attempted = True
                    try:
                        remote_trace(guest, artifact_dir, state.trace_sha256, ("off",))
                        wait_control(
                            guest, artifact_dir, state.trace_sha256, 0, 0, 0,
                            0, cleanup_attempts,
                        )
                        state.trace_cleanup_disabled_confirmed = True
                    except Exception:
                        pass
            try:
                state.guest_dtrace_after = guest_dtrace_zero(guest)
            except Exception:
                pass
        try:
            state.host_dtrace_after = host_dtrace_zero()
        except Exception:
            pass
        write_evidence(output_dir, state)
        if guest is not None:
            guest.close()


def main() -> int:
    args = parse_arguments()
    try:
        return run(args)
    except RunnerError as error:
        print(f"ERROR: {error.phase}", file=sys.stderr)
        return 2
    except (OSError, subprocess.SubprocessError):
        print("ERROR: runtime setup failed", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
