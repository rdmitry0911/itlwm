#!/usr/bin/env bash
# Static safety and coverage contract for the one-pass Tahoe SAE backend gate.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_sae_backend_intake_batch.sh"

[ -f "$RUNNER" ] || {
	echo "FAIL: missing Tahoe SAE backend intake batch runner" >&2
	exit 1
}
bash -n "$RUNNER"

python3 - "$RUNNER" <<'PY'
from pathlib import Path
import sys


runner = Path(sys.argv[1]).read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe SAE backend intake batch contract: {message}")


def require(needle: str, label: str) -> None:
    if needle not in runner:
        fail(f"missing {label}: {needle}")


def forbid(needle: str, label: str) -> None:
    if needle in runner:
        fail(f"forbidden {label}: {needle}")


def ordered(label: str, *needles: str) -> None:
    position = -1
    for needle in needles:
        position = runner.find(needle, position + 1)
        if position < 0:
            fail(f"missing ordered {label}: {needle}")


require("#!/usr/bin/env bash", "Bash runner")
require("requires an x86_64 Darwin guest", "guest platform fence")
require("does not load or install a kext, associate, scan, alter routing or addresses,",
        "non-runtime safety boundary")
require("or reboot the guest.", "reboot safety boundary")
ordered(
    "five-stage backend coverage",
    'run_tahoe_sae_quarantine_layer.sh" --static-only',
    'test_tahoe_sae_product_foundation_contract.sh',
    'run_tahoe_openwrt_mbedtls_sae_group19_kat.sh',
    'ITLWM_SOURCE_ID_OVERRIDE="$SOURCE_ID" "$ROOT/scripts/build_tahoe.sh"',
    '"$ROOT/scripts/build_regdiag.sh"',
    'make -C "$ROOT/AirportItlwmAgent" clean all',
)
require("PASS: Tahoe SAE backend intake batch completed without runtime network actions",
        "success scope")

for needle in (
    "kextload", "kextutil", "kmutil", "networksetup", "airport -s",
    "wdutil scan", "route add", "route delete", "route change",
    "ifconfig ", "ipconfig ", "/sbin/reboot", "shutdown -r",
    "sudo ", "ssh ", "scp ", "rsync ", "osascript ",
):
    forbid(needle, "unsafe or remote batch capability")
PY

echo "PASS: Tahoe SAE backend intake batch contract"
