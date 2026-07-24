#!/usr/bin/env bash
# Validate the local-only receipt emitted by the Tahoe overlay preparer.
#
# This validates only a JSON aggregate.  It does not inspect an image, launch
# QEMU, contact the guest, or mutate any storage.
set -euo pipefail

MODE="self-test"
ATTESTATION=""

usage() {
    cat >&2 <<'EOF'
usage: test_tahoe_disposable_overlay_evidence_contract.sh [--self-test]
       test_tahoe_disposable_overlay_evidence_contract.sh --attestation LOCAL_RECEIPT.json
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --self-test)
            MODE="self-test"
            shift
            ;;
        --attestation)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            MODE="validate"
            ATTESTATION="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

if [ "$MODE" = "validate" ]; then
    [ -f "$ATTESTATION" ] && [ ! -L "$ATTESTATION" ] || {
        printf 'FAIL: Tahoe disposable-overlay evidence: attestation missing or symlinked\n' >&2
        exit 2
    }
fi

python3 - "$MODE" "$ATTESTATION" <<'PY'
import json
import re
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: Tahoe disposable-overlay evidence: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def exact_keys(mapping: dict, expected: set[str], label: str) -> None:
    require(isinstance(mapping, dict), f"{label} section missing")
    require(set(mapping) == expected, f"{label} shape changed")


def exact_literals(mapping: object, expected: dict[str, object], label: str) -> None:
    exact_keys(mapping, set(expected), label)
    assert isinstance(mapping, dict)
    for key, wanted in expected.items():
        actual = mapping[key]
        if isinstance(wanted, bool):
            require(actual is wanted, f"{label} value changed: {key}")
        elif isinstance(wanted, str):
            require(isinstance(actual, str) and actual == wanted,
                    f"{label} value changed: {key}")
        else:
            require(actual == wanted, f"{label} value changed: {key}")


def positive_integer(value: object, label: str) -> None:
    require(isinstance(value, int) and not isinstance(value, bool) and value > 0,
            f"{label} is invalid")


def sha256(value: object, label: str) -> None:
    require(isinstance(value, str) and
            re.fullmatch(r"[0-9a-f]{64}", value) is not None,
            f"{label} is malformed")


SCOPE = {
    "qemu_guest_started_by_helper": False,
    "guest_rebooted_by_helper": False,
    "base_image_mutated_by_helper": False,
    "candidate_or_auxkc_mutated_by_helper": False,
    "physical_validation_host_touched": False,
}
LOCAL_ONLY = {
    "image_paths_retained_local_only": True,
    "wireless_identity_or_credential_recorded": False,
}
NON_CLAIMS = [
    "candidate activation",
    "guest boot or reboot",
    "PMF/BIP association",
    "traffic reachability",
    "physical-host validation",
]
OVERLAY_KEYS = {
    "format",
    "one_direct_backing_image_verified",
    "base_has_no_backing_image",
    "top_overlay_data_allocated",
    "base_virtual_size",
    "overlay_virtual_size",
    "base_metadata_sha256",
    "overlay_metadata_sha256",
}
V1_LAUNCH_CONTRACT = {
    "pinned_vm_root_required": True,
    "disk_selector_environment_variable": "ITLWM_DISK",
    "guest_boot_performed_by_helper": False,
}
V2_LAUNCH_CONTRACT = {
    **V1_LAUNCH_CONTRACT,
    "ovmf_vars_selector_environment_variable": "ITLWM_OVMF_VARS",
    "ovmf_vars_file_name": "OVMF_VARS-1920x1080.fd",
    "ovmf_vars_sha256_must_match_before_first_boot": True,
}
OVMF_VARS_KEYS = {
    "format",
    "file_name",
    "per_overlay_copy_created",
    "copy_mode_0600",
    "copy_has_distinct_inode",
    "template_not_in_use_precheck",
    "template_stable_during_copy",
    "template_sha256",
    "copy_sha256",
    "template_copy_sha256_match",
    "copy_size_bytes",
}


def validate_overlay(overlay: object) -> None:
    exact_keys(overlay, OVERLAY_KEYS, "overlay")
    assert isinstance(overlay, dict)
    require(overlay["format"] == "qcow2", "overlay format changed")
    require(overlay["one_direct_backing_image_verified"] is True,
            "receipt lacks direct backing verification")
    require(overlay["base_has_no_backing_image"] is True,
            "receipt lacks root-base verification")
    require(overlay["top_overlay_data_allocated"] is False,
            "receipt does not describe a fresh top layer")
    for key in ("base_virtual_size", "overlay_virtual_size"):
        positive_integer(overlay[key], f"overlay {key}")
    require(overlay["base_virtual_size"] == overlay["overlay_virtual_size"],
            "base/overlay virtual sizes differ")
    for key in ("base_metadata_sha256", "overlay_metadata_sha256"):
        sha256(overlay[key], f"overlay {key}")


def validate_ovmf_vars(ovmf_vars: object) -> None:
    exact_keys(ovmf_vars, OVMF_VARS_KEYS, "ovmf_vars")
    assert isinstance(ovmf_vars, dict)
    require(ovmf_vars["format"] == "raw-pflash", "OVMF vars format changed")
    require(ovmf_vars["file_name"] == "OVMF_VARS-1920x1080.fd",
            "OVMF vars file name changed")
    for key in (
        "per_overlay_copy_created",
        "copy_mode_0600",
        "copy_has_distinct_inode",
        "template_not_in_use_precheck",
        "template_stable_during_copy",
        "template_copy_sha256_match",
    ):
        require(ovmf_vars[key] is True, f"OVMF vars fact missing: {key}")
    sha256(ovmf_vars["template_sha256"], "OVMF template SHA-256")
    sha256(ovmf_vars["copy_sha256"], "OVMF copy SHA-256")
    require(ovmf_vars["template_sha256"] == ovmf_vars["copy_sha256"],
            "OVMF template/copy SHA-256 mismatch")
    positive_integer(ovmf_vars["copy_size_bytes"], "OVMF copy size")


def validate(document: object) -> None:
    require(isinstance(document, dict), "receipt is not an object")
    schema = document.get("schema")
    if schema == "itlwm-tahoe-disposable-overlay/v1":
        exact_keys(document, {
            "schema", "result", "scope", "overlay", "launch_contract",
            "local_only", "non_claims",
        }, "v1 receipt")
    elif schema == "itlwm-tahoe-disposable-overlay/v2":
        exact_keys(document, {
            "schema", "result", "scope", "overlay", "launch_contract",
            "local_only", "non_claims", "ovmf_vars",
        }, "v2 receipt")
    else:
        fail("unexpected schema")
    require(document.get("result") == "PASS", "receipt result is not PASS")

    exact_literals(document.get("scope"), SCOPE, "scope")

    validate_overlay(document.get("overlay"))

    launch = document.get("launch_contract")
    if schema == "itlwm-tahoe-disposable-overlay/v1":
        exact_literals(launch, V1_LAUNCH_CONTRACT, "v1 launch contract")
    else:
        exact_literals(launch, V2_LAUNCH_CONTRACT, "v2 launch contract")
        validate_ovmf_vars(document.get("ovmf_vars"))

    exact_literals(document.get("local_only"), LOCAL_ONLY, "local-only")

    require(document.get("non_claims") == NON_CLAIMS,
            "required non-claims are missing")

    serialized = json.dumps(document, sort_keys=True)
    require(re.search(r'(?:"|\s)/(?:[^"\s]+)', serialized) is None,
            "local filesystem path escaped into receipt")
    require(re.search(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b", serialized) is None,
            "IPv4 literal escaped into receipt")
    require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", serialized) is None,
            "MAC literal escaped into receipt")
    require(re.search(r"(?i)\b(?:ssid|bssid|passphrase|password)\b", serialized) is None,
            "wireless identity or credential label escaped into receipt")


def fixture_v1() -> dict:
    return {
        "schema": "itlwm-tahoe-disposable-overlay/v1",
        "result": "PASS",
        "scope": {
            "qemu_guest_started_by_helper": False,
            "guest_rebooted_by_helper": False,
            "base_image_mutated_by_helper": False,
            "candidate_or_auxkc_mutated_by_helper": False,
            "physical_validation_host_touched": False,
        },
        "overlay": {
            "format": "qcow2",
            "one_direct_backing_image_verified": True,
            "base_has_no_backing_image": True,
            "top_overlay_data_allocated": False,
            "base_virtual_size": 107374182400,
            "overlay_virtual_size": 107374182400,
            "base_metadata_sha256": "a" * 64,
            "overlay_metadata_sha256": "b" * 64,
        },
        "launch_contract": {
            "pinned_vm_root_required": True,
            "disk_selector_environment_variable": "ITLWM_DISK",
            "guest_boot_performed_by_helper": False,
        },
        "local_only": {
            "image_paths_retained_local_only": True,
            "wireless_identity_or_credential_recorded": False,
        },
        "non_claims": [
            "candidate activation",
            "guest boot or reboot",
            "PMF/BIP association",
            "traffic reachability",
            "physical-host validation",
        ],
    }


def fixture_v2() -> dict:
    document = fixture_v1()
    document["schema"] = "itlwm-tahoe-disposable-overlay/v2"
    document["launch_contract"] = V2_LAUNCH_CONTRACT
    document["ovmf_vars"] = {
        "format": "raw-pflash",
        "file_name": "OVMF_VARS-1920x1080.fd",
        "per_overlay_copy_created": True,
        "copy_mode_0600": True,
        "copy_has_distinct_inode": True,
        "template_not_in_use_precheck": True,
        "template_stable_during_copy": True,
        "template_sha256": "c" * 64,
        "copy_sha256": "c" * 64,
        "template_copy_sha256_match": True,
        "copy_size_bytes": 65536,
    }
    return document


def must_reject(document: dict, label: str) -> None:
    try:
        validate(document)
    except SystemExit:
        return
    fail(f"self-test accepted {label}")


if sys.argv[1] == "self-test":
    validate(fixture_v1())
    validate(fixture_v2())
    invalid_v2 = fixture_v2()
    invalid_v2["ovmf_vars"]["unexpected"] = True
    must_reject(invalid_v2, "v2 receipt with an extra OVMF field")
else:
    try:
        def no_duplicate_keys(pairs: list[tuple[str, object]]) -> dict:
            document: dict[str, object] = {}
            for key, value in pairs:
                if key in document:
                    raise ValueError("duplicate JSON key")
                document[key] = value
            return document

        document = json.loads(
            Path(sys.argv[2]).read_text(encoding="utf-8"),
            object_pairs_hook=no_duplicate_keys,
            parse_constant=lambda value: (_ for _ in ()).throw(
                ValueError(f"non-finite JSON value: {value}")),
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        fail(f"cannot read attestation: {type(error).__name__}")
    validate(document)

print("PASS: Tahoe disposable-overlay evidence contract")
PY
