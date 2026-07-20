#!/usr/bin/env bash
# One invocation for the Tahoe SAE/PMF quarantine layer.
#
# It first runs the product SAE/PMF quarantine and Tahoe build-admission
# contracts, then (unless --static-only is selected) transfers the current
# tree to an isolated Tahoe guest directory and builds both the kext and
# Agent there. The separate test-only SAE foundation aggregate is intentionally
# local-only and is not part of this remote build gate. This runner never
# installs, loads, releases, or reboots anything.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
STATIC_ONLY=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --static-only)
            STATIC_ONLY=1
            ;;
        *)
            echo "usage: $0 [--static-only]" >&2
            exit 2
            ;;
    esac
    shift
done

REMOTE="${TAHOE_SAE_GATE_REMOTE:-devops@127.0.0.1}"
PORT="${TAHOE_SAE_GATE_PORT:-3322}"
REMOTE_SDK="${TAHOE_SAE_GATE_SDK:-/Users/devops/Projects/itlwm/MacKernelSDK}"
BOOTKC="${TAHOE_SAE_GATE_BOOTKC:-/System/Library/KernelCollections/BootKernelExtensions.kc}"
SSH=(ssh -o BatchMode=yes -p "$PORT" "$REMOTE")

echo "[1/4] static SAE/PMF, PMK, raw-BSD, and MFP contracts"
bash "$ROOT/scripts/test_tahoe_sae_quarantine_contract.sh"
bash "$ROOT/scripts/test_tahoe_boot_thread_call_auxkc_contract.sh"
bash "$ROOT/scripts/test_tahoe_auxkc_admission_preflight_contract.sh"
bash -n "$ROOT/scripts/build_tahoe.sh"
bash -n "$ROOT/scripts/tahoe_auxkc_admission_preflight.sh"
bash -n "$ROOT/scripts/capture_tahoe_sae_layer.sh"
git -C "$ROOT" diff --check

if [ "$STATIC_ONLY" -eq 1 ]; then
    echo "PASS: static-only Tahoe SAE/PMF layer gate"
    exit 0
fi

# Remote staging must remain an exact source tree. Static-only validation is
# read-only and is intentionally usable while unrelated work is present.
UNTRACKED_FILES="$(git -C "$ROOT" ls-files --others --exclude-standard)"
if [ -n "$UNTRACKED_FILES" ]; then
    echo "ERROR: Tahoe SAE layer gate refuses untracked source files; stage or ignore them first" >&2
    printf '%s\n' "$UNTRACKED_FILES" >&2
    exit 1
fi
SOURCE_ID="$(git -C "$ROOT" rev-parse --short HEAD)"
SOURCE_DIRTY=0
if ! git -C "$ROOT" diff --quiet; then
    SOURCE_DIRTY=1
fi
if ! git -C "$ROOT" diff --cached --quiet; then
    SOURCE_DIRTY=1
fi
if [ "$SOURCE_DIRTY" -ne 0 ]; then
    if command -v sha256sum >/dev/null 2>&1; then
        SOURCE_ID="dirty$({ git -C "$ROOT" diff --binary; git -C "$ROOT" diff --cached --binary; } | sha256sum | awk '{print substr($1, 1, 12)}')"
    else
        SOURCE_ID="dirty$({ git -C "$ROOT" diff --binary; git -C "$ROOT" diff --cached --binary; } | shasum -a 256 | awk '{print substr($1, 1, 12)}')"
    fi
fi

echo "[2/4] allocating isolated Tahoe build directory"
REMOTE_DIR="$("${SSH[@]}" 'mktemp -d /tmp/aiam-tahoe-sae-layer-gate.XXXXXX')"
if [ -z "$REMOTE_DIR" ]; then
    echo "ERROR: Tahoe guest did not return an isolated build directory" >&2
    exit 1
fi

echo "[3/4] staging source and project-local MacKernelSDK"
rsync -a -e "ssh -o BatchMode=yes -p $PORT" \
    --exclude='.git' \
    --exclude='Build' \
    --exclude='DerivedData' \
    --exclude='DerivedData-optout' \
    --exclude='MacKernelSDK' \
    "$ROOT/" "$REMOTE:$REMOTE_DIR/"
"${SSH[@]}" "test -f '$REMOTE_SDK/Headers/IOKit/network/IONetworkController.h'"
"${SSH[@]}" "cp -R '$REMOTE_SDK' '$REMOTE_DIR/MacKernelSDK'"

echo "[4/4] Tahoe kext BootKC gate, Agent clean build, and RegDiag client"
"${SSH[@]}" "cd '$REMOTE_DIR' && ITLWM_SOURCE_ID_OVERRIDE='$SOURCE_ID' ./scripts/build_tahoe.sh '$BOOTKC'"
"${SSH[@]}" "cd '$REMOTE_DIR/AirportItlwmAgent' && make clean && make"
"${SSH[@]}" "cd '$REMOTE_DIR' && ./scripts/build_regdiag.sh"

echo "PASS: Tahoe SAE/PMF layer gate"
echo "  source identity: $SOURCE_ID"
echo "  isolated guest build: $REMOTE:$REMOTE_DIR"
echo "  no kext was installed, loaded, published, or released"
