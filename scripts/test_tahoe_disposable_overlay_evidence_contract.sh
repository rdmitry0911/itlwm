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


def boolean_fields(mapping: dict, *keys: str) -> None:
    for key in keys:
        require(isinstance(mapping.get(key), bool), f"boolean missing: {key}")


def validate(document: dict) -> None:
    require(document.get("schema") == "itlwm-tahoe-disposable-overlay/v1",
            "unexpected schema")
    require(document.get("result") == "PASS", "receipt result is not PASS")

    scope = document.get("scope")
    require(isinstance(scope, dict), "scope section missing")
    require(scope == {
        "qemu_guest_started_by_helper": False,
        "guest_rebooted_by_helper": False,
        "base_image_mutated_by_helper": False,
        "candidate_or_auxkc_mutated_by_helper": False,
        "physical_validation_host_touched": False,
    }, "storage-boundary scope changed")

    overlay = document.get("overlay")
    require(isinstance(overlay, dict), "overlay section missing")
    require(overlay.get("format") == "qcow2", "overlay format changed")
    boolean_fields(overlay, "one_direct_backing_image_verified",
                   "base_has_no_backing_image", "top_overlay_data_allocated")
    require(overlay["one_direct_backing_image_verified"] is True,
            "receipt lacks direct backing verification")
    require(overlay["base_has_no_backing_image"] is True,
            "receipt lacks root-base verification")
    require(overlay["top_overlay_data_allocated"] is False,
            "receipt does not describe a fresh top layer")
    for key in ("base_virtual_size", "overlay_virtual_size"):
        value = overlay.get(key)
        require(isinstance(value, int) and not isinstance(value, bool) and value > 0,
                f"overlay {key} is invalid")
    require(overlay["base_virtual_size"] == overlay["overlay_virtual_size"],
            "base/overlay virtual sizes differ")
    for key in ("base_metadata_sha256", "overlay_metadata_sha256"):
        require(re.fullmatch(r"[0-9a-f]{64}", str(overlay.get(key, ""))) is not None,
                f"overlay {key} is malformed")

    launch = document.get("launch_contract")
    require(launch == {
        "pinned_vm_root_required": True,
        "disk_selector_environment_variable": "ITLWM_DISK",
        "guest_boot_performed_by_helper": False,
    }, "launch contract changed")

    local_only = document.get("local_only")
    require(local_only == {
        "image_paths_retained_local_only": True,
        "wireless_identity_or_credential_recorded": False,
    }, "local-only boundary changed")

    require(set(document.get("non_claims", [])) >= {
        "candidate activation",
        "guest boot or reboot",
        "PMF/BIP association",
        "traffic reachability",
        "physical-host validation",
    }, "required non-claims are missing")

    serialized = json.dumps(document, sort_keys=True)
    require(re.search(r'(?:"|\s)/(?:[^"\s]+)', serialized) is None,
            "local filesystem path escaped into receipt")
    require(re.search(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b", serialized) is None,
            "IPv4 literal escaped into receipt")
    require(re.search(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", serialized) is None,
            "MAC literal escaped into receipt")
    require(re.search(r"(?i)\b(?:ssid|bssid|passphrase|password)\b", serialized) is None,
            "wireless identity or credential label escaped into receipt")


def fixture() -> dict:
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


if sys.argv[1] == "self-test":
    validate(fixture())
else:
    try:
        document = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read attestation: {type(error).__name__}")
    validate(document)

print("PASS: Tahoe disposable-overlay evidence contract")
PY
