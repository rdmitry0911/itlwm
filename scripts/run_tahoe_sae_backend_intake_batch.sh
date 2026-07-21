#!/usr/bin/env bash
# Execute the complete safe Tahoe SAE backend intake gate in one guest batch.
#
# The batch intentionally performs only static contracts and local builds.  It
# does not load or install a kext, associate, scan, alter routing or addresses,
# or reboot the guest.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

if [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "x86_64" ]; then
	echo "ERROR: this Tahoe backend intake batch requires an x86_64 Darwin guest" >&2
	exit 2
fi

SOURCE_ID="sae-backend-intake-$(git -C "$ROOT" rev-parse --short HEAD)"

echo "[1/5] static SAE quarantine and product-foundation contracts"
bash "$ROOT/scripts/test_tahoe_sae_backend_intake_batch_contract.sh"
bash "$ROOT/scripts/run_tahoe_sae_quarantine_layer.sh" --static-only
bash "$ROOT/scripts/test_tahoe_sae_product_foundation_contract.sh"

echo "[2/5] source-pinned OpenWrt mbedTLS group-19 vectors"
bash "$ROOT/scripts/run_tahoe_openwrt_mbedtls_sae_group19_kat.sh"

echo "[3/5] Tahoe kext build without load"
ITLWM_SOURCE_ID_OVERRIDE="$SOURCE_ID" "$ROOT/scripts/build_tahoe.sh"

echo "[4/5] RegDiag build"
"$ROOT/scripts/build_regdiag.sh"

echo "[5/5] AirportItlwmAgent clean build"
make -C "$ROOT/AirportItlwmAgent" clean all

echo "PASS: Tahoe SAE backend intake batch completed without runtime network actions"
