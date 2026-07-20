#!/usr/bin/env bash
# Contract for the strictly read-only Tahoe release-kext identity gate.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CAPTURE="$ROOT/scripts/capture_tahoe_lab_kext_identity.py"

[ -x "$CAPTURE" ] || {
    echo "FAIL: missing executable read-only identity capture" >&2
    exit 1
}

python3 "$CAPTURE" --self-test

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
text = (root / "scripts/capture_tahoe_lab_kext_identity.py").read_text()


def require(needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"identity binding contract: missing {label}: {needle}")


def forbid(needle: str, label: str) -> None:
    if needle in text:
        raise SystemExit(f"identity binding contract: forbidden {label}: {needle}")


for needle, label in (
    ("itlwm-tahoe-lab-kext-identity-binding/v1", "schema"),
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
):
    require(needle, label)

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
    ("release_zip_path", "artifact path in public evidence"),
):
    forbid(needle, label)

print("PASS: Tahoe lab kext identity binding contract")
PY
