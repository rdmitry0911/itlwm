#!/usr/bin/env python3
"""Bind the two fixed guest-local direct-SAE laboratory executables.

This capture is deliberately narrower than a Tahoe candidate receipt.  It
does not install or identify a kext, inspect a network profile, or perform an
association.  It only reads the two separately staged, restricted-path tools
twice through the pinned disposable guest transport and writes an aggregate,
local-only receipt containing their SHA-256 values.  The guest path itself is
never serialized.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

# Reuse only the pinned transport constants.  In particular, this module does
# not call the older loaded-identity capture, whose probe has a broader
# network-interface observation surface that is inappropriate here.
from capture_tahoe_iwn_lab_loaded_identity import (
    PINNED_QEMU_BUILD,
    PINNED_QEMU_GUEST,
    PINNED_QEMU_HOST_KEY,
    PINNED_QEMU_PORT,
)


SCHEMA_VERSION = "itlwm-tahoe-iwn-direct-sae-lab-artifacts/v1"
RECEIPT_KIND = "guest-local-direct-sae-lab-artifact-binding"
ARTIFACT_PATH_POLICY = "pinned-guest-private-isae-runtime-dir/v1"
DIRECT_CLIENT_NAME = "airport_itlwm_iwn_direct_sae_lab_client"
TRACE_CLIENT_NAME = "airport_itlwm_post_plti_trace"
SHA256_RE = re.compile(r"[0-9a-f]{64}")
ARTIFACT_DIR_RE = re.compile(
    r"/Users/devops/\.aiam-isae-runtime-[A-Za-z0-9][A-Za-z0-9._-]*"
)

VALIDATION_KEYS = {
    "guest_build_pinned",
    "artifact_parent_not_symlink",
    "direct_client_regular_executable",
    "trace_client_regular_executable",
    "direct_client_stable_during_capture",
    "trace_client_stable_during_capture",
}
NON_CLAIM_KEYS = {
    "driver_identity_bound",
    "userclient_invoked",
    "sae_submitted",
    "wireless_identity_collected",
    "network_input_collected",
}


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def require_regular_file(path: Path, label: str) -> Path:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise ValueError(f"{label} is missing") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise ValueError(f"{label} must be a regular non-symlink file")
    return path.resolve(strict=True)


def require_new_output(path: Path) -> Path:
    if not path.is_absolute() or path.name in {"", ".", ".."}:
        raise ValueError("receipt output must be an absolute file path")
    try:
        parent = path.parent.lstat()
    except OSError as error:
        raise ValueError("receipt output parent is missing") from error
    if stat.S_ISLNK(parent.st_mode) or not stat.S_ISDIR(parent.st_mode):
        raise ValueError("receipt output parent is unsafe")
    if path.exists() or path.is_symlink():
        raise ValueError("receipt output must be a new non-symlink path")
    return path


def require_artifact_dir(value: str) -> str:
    if not isinstance(value, str) or "\x00" in value:
        raise ValueError("guest artifact directory is malformed")
    if ARTIFACT_DIR_RE.fullmatch(value) is None:
        raise ValueError("guest artifact directory does not match the restricted policy")
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


def parse_json_document(text: str) -> object:
    return json.loads(
        text,
        object_pairs_hook=reject_duplicate_object_keys,
        parse_constant=reject_nonfinite_json_constant,
    )


def require_digest(value: object, label: str) -> str:
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise ValueError(f"{label} digest is malformed")
    return value


def artifact_probe_script() -> str:
    """Return a fixed read-only remote probe accepting only a safe directory."""
    return r'''
set -euo pipefail
artifact_dir="$1"
printf '%s\n' "$artifact_dir" |
  /usr/bin/grep -Eq '^/Users/devops/\.aiam-isae-runtime-[A-Za-z0-9][A-Za-z0-9._-]*$' || exit 64
test -d "$artifact_dir" && test ! -L "$artifact_dir"
physical_dir="$(CDPATH= cd -P -- "$artifact_dir" && pwd -P)"
test "$physical_dir" = "$artifact_dir"

digest() {
  file="$1"
  test -f "$file" && test ! -L "$file" && test -x "$file"
  LC_ALL=C PATH=/usr/bin:/bin /usr/bin/shasum -a 256 "$file" |
    /usr/bin/awk -v expected="$file" '
      function hex64(value) { return length(value) == 64 && value !~ /[^0-9a-f]/ }
      NR == 1 && NF == 2 && $2 == expected && hex64($1) { print $1; next }
      { invalid = 1 }
      END { if (NR != 1 || invalid) exit 1 }
    '
}

test "$(/usr/bin/sw_vers -buildVersion 2>/dev/null)" = "__PINNED_BUILD__"
one_direct="$(digest "$artifact_dir/__DIRECT_CLIENT__")"
one_trace="$(digest "$artifact_dir/__TRACE_CLIENT__")"
two_direct="$(digest "$artifact_dir/__DIRECT_CLIENT__")"
two_trace="$(digest "$artifact_dir/__TRACE_CLIENT__")"
printf 'pair-one-direct=%s\n' "$one_direct"
printf 'pair-one-trace=%s\n' "$one_trace"
printf 'pair-two-direct=%s\n' "$two_direct"
printf 'pair-two-trace=%s\n' "$two_trace"
'''.replace("__PINNED_BUILD__", PINNED_QEMU_BUILD).replace(
        "__DIRECT_CLIENT__", DIRECT_CLIENT_NAME
    ).replace("__TRACE_CLIENT__", TRACE_CLIENT_NAME)


def pinned_ssh(known_hosts: Path) -> list[str]:
    return [
        "ssh",
        "-F", "/dev/null",
        "-T",
        "-o", "BatchMode=yes",
        "-o", "ConnectTimeout=8",
        "-o", "StrictHostKeyChecking=yes",
        "-o", f"UserKnownHostsFile={known_hosts}",
        "-o", "GlobalKnownHostsFile=/dev/null",
        "-o", "UpdateHostKeys=no",
        "-o", "LogLevel=ERROR",
        "-p", str(PINNED_QEMU_PORT),
        PINNED_QEMU_GUEST,
    ]


def verified_known_hosts() -> Path:
    handle = tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", prefix="aiam-isae-artifacts-known-hosts-",
        delete=False,
    )
    try:
        handle.write(PINNED_QEMU_HOST_KEY + "\n")
        handle.close()
        os.chmod(handle.name, 0o600)
        result = subprocess.run(
            ["ssh-keygen", "-lf", handle.name, "-E", "sha256"],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        if not result.stdout.split():
            raise ValueError("pinned guest host key cannot be read")
        return Path(handle.name)
    except Exception:
        Path(handle.name).unlink(missing_ok=True)
        raise


def parse_probe_output(output: str) -> tuple[str, str]:
    pattern = re.compile(
        r"pair-one-direct=([0-9a-f]{64})\n"
        r"pair-one-trace=([0-9a-f]{64})\n"
        r"pair-two-direct=([0-9a-f]{64})\n"
        r"pair-two-trace=([0-9a-f]{64})\n"
    )
    match = pattern.fullmatch(output)
    if match is None:
        raise ValueError("guest artifact probe output is malformed")
    one_direct, one_trace, two_direct, two_trace = match.groups()
    if one_direct != two_direct or one_trace != two_trace:
        raise ValueError("guest artifact changed during receipt capture")
    return one_direct, one_trace


def capture_guest_artifacts(artifact_dir: str, timeout_seconds: int = 20) -> tuple[str, str]:
    artifact_dir = require_artifact_dir(artifact_dir)
    known_hosts = verified_known_hosts()
    try:
        result = subprocess.run(
            [*pinned_ssh(known_hosts), "/bin/bash", "-s", "--", artifact_dir],
            input=artifact_probe_script(), text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as error:
        raise ValueError("guest artifact probe timed out") from error
    finally:
        known_hosts.unlink(missing_ok=True)
    if result.returncode != 0:
        raise ValueError("guest artifact probe failed")
    return parse_probe_output(result.stdout)


def make_receipt(direct_sha256: str, trace_sha256: str) -> dict[str, object]:
    direct_sha256 = require_digest(direct_sha256, "direct client")
    trace_sha256 = require_digest(trace_sha256, "trace client")
    document: dict[str, object] = {
        "schema": SCHEMA_VERSION,
        "receipt_kind": RECEIPT_KIND,
        "created_at_utc": utc_now(),
        "guest": {
            "build": PINNED_QEMU_BUILD,
            "artifact_path_policy": ARTIFACT_PATH_POLICY,
        },
        "artifacts": {
            "direct_sae_client_sha256": direct_sha256,
            "trace_client_sha256": trace_sha256,
        },
        "validation": {key: True for key in sorted(VALIDATION_KEYS)},
        "non_claims": {
            "driver_identity_bound": False,
            "userclient_invoked": False,
            "sae_submitted": False,
            "wireless_identity_collected": False,
            "network_input_collected": False,
        },
        "verdict": {
            "artifact_bytes_bound": True,
            "runtime_experiment_performed": False,
        },
    }
    validate_artifact_receipt_document(document)
    return document


def validate_artifact_receipt_document(document: object) -> dict[str, Any]:
    if not isinstance(document, dict) or set(document) != {
        "schema", "receipt_kind", "created_at_utc", "guest", "artifacts",
        "validation", "non_claims", "verdict",
    }:
        raise ValueError("artifact receipt document shape")
    if document.get("schema") != SCHEMA_VERSION or document.get("receipt_kind") != RECEIPT_KIND:
        raise ValueError("artifact receipt schema or kind")
    created = document.get("created_at_utc")
    if not isinstance(created, str) or not re.fullmatch(
        r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\+00:00", created
    ):
        raise ValueError("artifact receipt timestamp")
    guest = document.get("guest")
    if guest != {
        "build": PINNED_QEMU_BUILD,
        "artifact_path_policy": ARTIFACT_PATH_POLICY,
    }:
        raise ValueError("artifact receipt guest projection")
    artifacts = document.get("artifacts")
    if not isinstance(artifacts, dict) or set(artifacts) != {
        "direct_sae_client_sha256", "trace_client_sha256",
    }:
        raise ValueError("artifact receipt artifact shape")
    for key, value in artifacts.items():
        require_digest(value, key)
    validation = document.get("validation")
    if not isinstance(validation, dict) or set(validation) != VALIDATION_KEYS or not all(
        value is True for value in validation.values()
    ):
        raise ValueError("artifact receipt validation")
    non_claims = document.get("non_claims")
    if not isinstance(non_claims, dict) or set(non_claims) != NON_CLAIM_KEYS or not all(
        value is False for value in non_claims.values()
    ):
        raise ValueError("artifact receipt non-claims")
    if document.get("verdict") != {
        "artifact_bytes_bound": True,
        "runtime_experiment_performed": False,
    }:
        raise ValueError("artifact receipt verdict")
    return {
        "direct_sae_client_sha256": str(artifacts["direct_sae_client_sha256"]),
        "trace_client_sha256": str(artifacts["trace_client_sha256"]),
    }


def load_artifact_receipt(path: Path) -> dict[str, Any]:
    receipt = require_regular_file(path, "direct-SAE artifact receipt")
    try:
        document = parse_json_document(receipt.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        raise ValueError("direct-SAE artifact receipt cannot be read") from error
    return validate_artifact_receipt_document(document)


def write_new_json(document: dict[str, object], output: Path) -> None:
    output = require_new_output(output)
    rendered = json.dumps(document, indent=2, sort_keys=True) + "\n"
    descriptor = os.open(str(output), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            descriptor = -1
            handle.write(rendered)
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    os.chmod(output, 0o600)


def self_test() -> int:
    direct = "a" * 64
    trace = "b" * 64
    document = make_receipt(direct, trace)
    if load_artifact_receipt_from_text(json.dumps(document)) != {
        "direct_sae_client_sha256": direct,
        "trace_client_sha256": trace,
    }:
        raise SystemExit("self-test: valid receipt rejected")
    for malformed in (
        json.dumps({**document, "extra": True}),
        json.dumps({**document, "artifacts": {"direct_sae_client_sha256": direct}}),
        json.dumps({**document, "validation": {key: True for key in VALIDATION_KEYS - {"guest_build_pinned"}}}),
        json.dumps({**document, "non_claims": {key: False for key in NON_CLAIM_KEYS - {"sae_submitted"}}}),
        '{"schema":"x","schema":"y"}',
    ):
        try:
            load_artifact_receipt_from_text(malformed)
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: malformed receipt accepted")
    for value in ("relative", "/private/tmp/other", "/Users/devops/.aiam-isae-runtime-"):
        try:
            require_artifact_dir(value)
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: unsafe artifact path accepted")
    with tempfile.TemporaryDirectory(prefix="aiam-isae-artifact-receipt-") as directory:
        output = Path(directory) / "receipt.json"
        write_new_json(document, output)
        if stat.S_IMODE(output.stat().st_mode) != 0o600:
            raise SystemExit("self-test: receipt mode is not private")
        try:
            write_new_json(document, output)
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: receipt overwrite accepted")
        link = Path(directory) / "receipt-link.json"
        link.symlink_to(output)
        try:
            load_artifact_receipt(link)
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: symlink receipt accepted")
    if parse_probe_output(
        f"pair-one-direct={direct}\npair-one-trace={trace}\n"
        f"pair-two-direct={direct}\npair-two-trace={trace}\n"
    ) != (direct, trace):
        raise SystemExit("self-test: paired probe parser rejected valid output")
    for malformed in (
        f"pair-one-direct={direct}\npair-one-trace={trace}\n",
        f"pair-one-direct={direct}\npair-one-trace={trace}\npair-two-direct={trace}\npair-two-trace={trace}\n",
        f"pair-one-direct={direct}\npair-one-trace={trace}\npair-two-direct={direct}\npair-two-trace={trace}\nextra\n",
    ):
        try:
            parse_probe_output(malformed)
        except ValueError:
            pass
        else:
            raise SystemExit("self-test: malformed probe output accepted")
    print("PASS: direct-SAE guest artifact receipt self-test")
    return 0


def load_artifact_receipt_from_text(text: str) -> dict[str, Any]:
    try:
        return validate_artifact_receipt_document(parse_json_document(text))
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError("direct-SAE artifact receipt is malformed") from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--guest-artifact-dir")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.guest_artifact_dir is None or args.output is None:
        parser.error("--guest-artifact-dir and --output are required unless --self-test is used")
    try:
        direct_sha256, trace_sha256 = capture_guest_artifacts(args.guest_artifact_dir)
        write_new_json(make_receipt(direct_sha256, trace_sha256), args.output)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL: direct-SAE guest artifact receipt capture", file=sys.stderr)
        return 2
    print("artifact-receipt=written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
