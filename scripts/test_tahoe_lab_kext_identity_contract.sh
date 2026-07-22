#!/usr/bin/env bash
# Contract for the strictly read-only Tahoe release-kext identity gate.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CAPTURE="$ROOT/scripts/capture_tahoe_lab_kext_identity.py"
PROVENANCE="$ROOT/scripts/create_tahoe_candidate_provenance.py"

[ -x "$CAPTURE" ] || {
    echo "FAIL: missing executable read-only identity capture" >&2
    exit 1
}
[ -x "$PROVENANCE" ] || {
    echo "FAIL: missing candidate provenance generator" >&2
    exit 1
}

python3 "$CAPTURE" --self-test
python3 "$PROVENANCE" --self-test

python3 - "$ROOT" "$PROVENANCE" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
text = (root / "scripts/capture_tahoe_lab_kext_identity.py").read_text()
provenance = Path(sys.argv[2]).read_text()
source_identity = (root / "scripts/tahoe_source_identity.py").read_text()


def require(needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"identity binding contract: missing {label}: {needle}")


def forbid(needle: str, label: str) -> None:
    if needle in text:
        raise SystemExit(f"identity binding contract: forbidden {label}: {needle}")


for needle, label in (
    ("itlwm-tahoe-lab-kext-identity-binding/v2", "schema"),
    ("--candidate-provenance", "mandatory candidate provenance input"),
    ("release_identity_from_candidate_provenance", "archive/provenance binding"),
    ("source_commit", "identity-bound source commit"),
    ("StrictHostKeyChecking=yes", "strict host-key check"),
    ("GlobalKnownHostsFile=/dev/null", "isolated global known-hosts rule"),
    ("PINNED_HOST_KEY_SHA256", "pinned host-key fingerprint"),
    ("PINNED_GUEST = \"devops@127.0.0.1\"", "pinned guest"),
    ("PINNED_PORT = 3322", "pinned SSH port"),
    ("PINNED_INTERFACE = \"en1\"", "pinned Wi-Fi interface"),
    ("PINNED_KEXT_PATH = \"/Library/Extensions/AirportItlwm.kext\"", "pinned kext path"),
    ("AirportItlwm.kext/Contents/Info.plist", "exact Info.plist archive member"),
    ("AirportItlwm.kext/Contents/MacOS/AirportItlwm", "exact binary archive member"),
    ("LC_UUID", "Mach-O UUID parser"),
    ("macho_uuid", "Mach-O identity function"),
    ("installed_binary_sha256_matches_release", "installed binary hash equality"),
    ("loaded_uuid_matches_release", "loaded UUID equality"),
    ("candidate_kext_bound", "binding verdict"),
    ("raw_guest_stdout_retained\": False", "raw stdout non-retention"),
    ("candidate_kext_loaded_by_capture\": False", "load non-claim"),
    ("association_tested\": False", "association non-claim"),
    ("data_transfer_tested\": False", "traffic non-claim"),
    ("itlwm-tahoe-candidate-provenance/v2", "IWX receipt compatibility"),
):
    require(needle, label)

for needle, label in (
    ("itlwm-tahoe-candidate-provenance/v1", "legacy provenance schema"),
    ("itlwm-tahoe-candidate-provenance/v2", "IWX provenance schema"),
    ("New candidate provenance is always v2", "new provenance migration rule"),
    ('"schema": SCHEMA_V2', "new manifest v2 emission"),
    ("require_clean_head", "clean committed source boundary"),
    ("source_identity", "committed source identity"),
    ("bind_candidate_provenance", "archive digest binding"),
    ("--trace-client", "IWX trace-client receipt input"),
    ("trace_client_sha256", "IWX trace-client digest"),
    ("trace_client_sha256_from_candidate_provenance", "IWX trace-client receipt reader"),
    ("sha256_regular_trace_client", "local regular trace-client fence"),
    ("candidate source worktree is not clean", "dirty-tree rejection"),
    ("candidate_kext_installed\": False", "install non-claim"),
):
    if needle not in provenance:
        raise SystemExit(f"identity binding contract: missing provenance {label}: {needle}")

for needle, label in (
    ("AirportItlwmPostPltiTrace", "trace-client source identity input"),
    ("scripts/build_post_plti_trace.sh", "trace-client build identity input"),
    ("tahoe-airportitlwm-source-identity-v2", "expanded source-identity domain"),
):
    if needle not in source_identity:
        raise SystemExit(f"identity binding contract: missing source identity {label}: {needle}")

for needle, label in (
    ("StrictHostKeyChecking=no", "permissive host-key setting"),
    ("UserKnownHostsFile=/dev/null", "discarded host-key store"),
    ("kmutil load", "kext load"),
    ("kextload", "legacy kext load"),
    ("kextutil", "kext utility load"),
    ("sudo ", "privilege escalation"),
    ("networksetup -set", "network setup mutation"),
    ("route add", "route mutation"),
    ("route delete", "route mutation"),
    ("route change", "route mutation"),
    ("ipconfig ", "IP configuration mutation"),
    ("ifconfig ", "interface configuration mutation"),
    ("parser.add_argument(\"--guest\"", "caller-selectable guest"),
    ("parser.add_argument(\"--port\"", "caller-selectable port"),
    ("parser.add_argument(\"--release-tag\"", "free release tag"),
    ("parser.add_argument(\"--source-commit\"", "free source commit"),
    ("release_zip_path", "artifact path in public evidence"),
):
    forbid(needle, label)

print("PASS: Tahoe lab kext identity binding contract")
PY
