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

PINNED_GUEST="devops@127.0.0.1"
PINNED_PORT=3322
PINNED_REMOTE_SDK="/Users/devops/Projects/itlwm/MacKernelSDK"
PINNED_BOOTKC="/System/Library/KernelCollections/BootKernelExtensions.kc"
EXPECTED_GUEST_BUILD="25C56"
EXPECTED_BOOTKC_SHA256="eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d"
EXPECTED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
EXPECTED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"
# Keep these two environment names as a fail-closed compatibility check for
# existing callers; any destination other than the pinned local QEMU guest is
# rejected before a connection is attempted.
REMOTE="${TAHOE_SAE_GATE_REMOTE:-$PINNED_GUEST}"
PORT="${TAHOE_SAE_GATE_PORT:-$PINNED_PORT}"
REMOTE_SDK="${TAHOE_SAE_GATE_SDK:-$PINNED_REMOTE_SDK}"
BOOTKC="${TAHOE_SAE_GATE_BOOTKC:-$PINNED_BOOTKC}"
KNOWN_HOSTS=""

cleanup_known_hosts() {
    if [ -n "$KNOWN_HOSTS" ]; then
        rm -f "$KNOWN_HOSTS"
    fi
}

prepare_guest_transport() {
    if [ "$REMOTE" != "$PINNED_GUEST" ] || [ "$PORT" != "$PINNED_PORT" ] ||
            [ "$REMOTE_SDK" != "$PINNED_REMOTE_SDK" ] || [ "$BOOTKC" != "$PINNED_BOOTKC" ]; then
        echo "ERROR: Tahoe SAE gate only accepts the pinned laboratory guest and paths" >&2
        exit 2
    fi

    KNOWN_HOSTS="$(mktemp /tmp/aiam-tahoe-sae-gate-known-hosts.XXXXXX)"
    chmod 600 "$KNOWN_HOSTS"
    printf '%s\n' "$EXPECTED_GUEST_HOSTKEY_LINE" > "$KNOWN_HOSTS"
    local observed
    observed="$(ssh-keygen -lf "$KNOWN_HOSTS" -E sha256 2>/dev/null |
        awk 'NR == 1 { print $2; exit }')"
    if [ "$observed" != "$EXPECTED_GUEST_HOSTKEY_SHA256" ]; then
        echo "ERROR: pinned Tahoe guest host-key fingerprint mismatch" >&2
        exit 1
    fi

    SSH=(
        ssh -F /dev/null -o BatchMode=yes -o ConnectTimeout=8
        -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$KNOWN_HOSTS"
        -o GlobalKnownHostsFile=/dev/null -o UpdateHostKeys=no -o LogLevel=ERROR
        -p "$PORT" "$REMOTE"
    )
}

assert_pinned_guest_provenance() {
    local observed_build observed_bootkc
    observed_build="$("${SSH[@]}" 'sw_vers -buildVersion')"
    observed_bootkc="$("${SSH[@]}" "shasum -a 256 '$BOOTKC' | awk '{ print \$1 }'")"
    if [ "$observed_build" != "$EXPECTED_GUEST_BUILD" ]; then
        echo "ERROR: pinned Tahoe guest build mismatch" >&2
        exit 1
    fi
    if [ "$observed_bootkc" != "$EXPECTED_BOOTKC_SHA256" ]; then
        echo "ERROR: pinned Tahoe guest BootKC digest mismatch" >&2
        exit 1
    fi
}

echo "[1/5] static SAE/PMF, WCL PMK-resume, owner-context, raw-BSD, and MFP contracts"
bash "$ROOT/scripts/test_tahoe_sae_quarantine_contract.sh"
bash "$ROOT/scripts/test_tahoe_link_handoff_diagnostic_contract.sh"
bash "$ROOT/scripts/test_tahoe_link_context_census_contract.sh"
bash "$ROOT/scripts/test_tahoe_link_handoff_lab_result_contract.sh"
bash "$ROOT/scripts/test_tahoe_appstore_iobuiltin_contract.sh"
bash "$ROOT/scripts/test_tahoe_boot_thread_call_auxkc_contract.sh"
bash "$ROOT/scripts/test_tahoe_auxkc_admission_preflight_contract.sh"
bash "$ROOT/scripts/test_tahoe_release_auxkc_preflight_result_contract.sh"
bash -n "$ROOT/scripts/build_tahoe.sh"
bash -n "$ROOT/scripts/build_post_plti_trace.sh"
bash -n "$ROOT/scripts/tahoe_auxkc_admission_preflight.sh"
bash -n "$ROOT/scripts/capture_tahoe_sae_layer.sh"
git -C "$ROOT" diff --check

if [ "$STATIC_ONLY" -eq 1 ]; then
    echo "PASS: static-only Tahoe SAE/PMF layer gate"
    exit 0
fi

trap cleanup_known_hosts EXIT
prepare_guest_transport
echo "[2/5] verifying pinned Tahoe guest provenance"
assert_pinned_guest_provenance

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

echo "[3/5] allocating isolated Tahoe build directory"
REMOTE_DIR="$("${SSH[@]}" 'mktemp -d /tmp/aiam-tahoe-sae-layer-gate.XXXXXX')"
if [ -z "$REMOTE_DIR" ]; then
    echo "ERROR: Tahoe guest did not return an isolated build directory" >&2
    exit 1
fi

echo "[4/5] staging source and project-local MacKernelSDK"
RSYNC_RSH="ssh -F /dev/null -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=yes -o UserKnownHostsFile=$KNOWN_HOSTS -o GlobalKnownHostsFile=/dev/null -o UpdateHostKeys=no -o LogLevel=ERROR -p $PORT"
rsync -a -e "$RSYNC_RSH" \
    --exclude='.git' \
    --exclude='Build' \
    --exclude='DerivedData' \
    --exclude='DerivedData-optout' \
    --exclude='MacKernelSDK' \
    "$ROOT/" "$REMOTE:$REMOTE_DIR/"
"${SSH[@]}" "test -f '$REMOTE_SDK/Headers/IOKit/network/IONetworkController.h'"
"${SSH[@]}" "cp -R '$REMOTE_SDK' '$REMOTE_DIR/MacKernelSDK'"

echo "[5/5] Tahoe kext BootKC gate, trace producer audit, Agent clean build, and RegDiag"
# build_post_plti_trace.sh inspects the actual producer objects with nm; build
# the isolated kext first so it cannot accidentally pass against stale objects
# from a prior guest directory.
"${SSH[@]}" "cd '$REMOTE_DIR' && ITLWM_SOURCE_ID_OVERRIDE='$SOURCE_ID' ./scripts/build_tahoe.sh '$BOOTKC'"
"${SSH[@]}" "cd '$REMOTE_DIR' && ./scripts/build_post_plti_trace.sh"
"${SSH[@]}" "cd '$REMOTE_DIR/AirportItlwmAgent' && make clean && make"
"${SSH[@]}" "cd '$REMOTE_DIR' && ./scripts/build_regdiag.sh"

echo "PASS: Tahoe SAE/PMF layer gate"
echo "  source identity: $SOURCE_ID"
echo "  isolated guest build: $REMOTE:$REMOTE_DIR"
echo "  no kext was installed, loaded, published, or released"
