#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IWN_SOFTWARE_PMF_LAB=0
if [ "${BUILD_IWN_SOFTWARE_PMF_LAB:-0}" = "1" ]; then
    IWN_SOFTWARE_PMF_LAB=1
fi

while [ "$#" -gt 0 ]; do
    case "$1" in
        --iwn-software-pmf-lab)
            IWN_SOFTWARE_PMF_LAB=1
            ;;
        *)
            echo "usage: $0 [--iwn-software-pmf-lab]" >&2
            exit 2
            ;;
    esac
    shift
done

VARIANT_LABEL="Tahoe"
DEFAULT_DERIVED_DATA="$PROJECT_DIR/DerivedData"
if [ "$IWN_SOFTWARE_PMF_LAB" -eq 1 ]; then
    VARIANT_LABEL="Tahoe-IwnSoftwarePmfLab"
    DEFAULT_DERIVED_DATA="$PROJECT_DIR/DerivedData-iwn-software-pmf-lab"
fi

OUTPUT_DIR="$PROJECT_DIR/Build/Debug/$VARIANT_LABEL"
OUTPUT="$OUTPUT_DIR/airport_itlwm_post_plti_trace"
DERIVED_DATA="${ITLWM_DERIVED_DATA_OVERRIDE:-$DEFAULT_DERIVED_DATA}"
OBJECT_DIR="$DERIVED_DATA/Build/Intermediates.noindex/itlwm.build/Debug/AirportItlwm-Tahoe.build/Objects-normal/x86_64"

case "$DERIVED_DATA" in
    /*) ;;
    *)
        echo "ERROR: ITLWM_DERIVED_DATA_OVERRIDE must be an absolute path" >&2
        exit 2
        ;;
esac

mkdir -p "$OUTPUT_DIR"
clang -std=c11 -Wall -Wextra -Werror \
    -I"$PROJECT_DIR/include" \
    "$PROJECT_DIR/AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c" \
    -framework IOKit \
    -framework CoreFoundation \
    -o "$OUTPUT"

require_external_bridge() {
    local unit="$1"
    local symbol="$2"
    local object="$OBJECT_DIR/$unit.o"

    if [ ! -f "$object" ]; then
        echo "ERROR: Tahoe trace producer object is missing: $object" >&2
        exit 1
    fi
    if ! nm -arch x86_64 "$object" | \
            grep -E "[[:space:]]U[[:space:]]+_${symbol}$" >/dev/null; then
        echo "ERROR: Tahoe trace producer $unit did not link external bridge $symbol" >&2
        exit 1
    fi
    if nm -arch x86_64 "$object" | \
            grep -E '__ZL.*AirportItlwmPostPltiTrace' >/dev/null; then
        echo "ERROR: Tahoe trace producer $unit compiled a local trace no-op stub" >&2
        exit 1
    fi
}

# Prove every production event producer that includes the bridge is linked to
# the Tahoe implementation.  A source-only include check would miss an
# availability-dependent inline fallback in any one of these objects.
require_external_bridge ieee80211_input AirportItlwmPostPltiTraceRecord
require_external_bridge ieee80211_node AirportItlwmPostPltiTraceRecord
require_external_bridge ieee80211_output AirportItlwmPostPltiTraceRecord
require_external_bridge ieee80211_pae_input AirportItlwmPostPltiTraceCompleteEpisode
require_external_bridge ieee80211_pae_output AirportItlwmPostPltiTraceRecord
require_external_bridge ieee80211_proto AirportItlwmPostPltiTraceNoteStateRequest
require_external_bridge ieee80211_crypto_bip AirportItlwmPostPltiTraceRecordIgtkPublicationSelection
require_external_bridge ItlIwn AirportItlwmPostPltiTraceRecord
require_external_bridge ItlIwn AirportItlwmPostPltiTraceRecordWclPhysicalScan
require_external_bridge ItlIwn AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode
require_external_bridge ItlIwx AirportItlwmPostPltiTraceRecord
require_external_bridge AirportItlwmSkywalkInterface AirportItlwmPostPltiTraceBeginWclPhysicalScanEpisode

# The pure-SAE WCL entry is not compiled in the ordinary product artifact, so
# prove its external bridge only against the separately built lab object.
# This makes the runtime client and its candidate kext use the same lab
# DerivedData lineage without making a normal build depend on a lab-only call.
if [ "$IWN_SOFTWARE_PMF_LAB" -eq 1 ]; then
    require_external_bridge AirportItlwmSkywalkInterface \
        AirportItlwmPostPltiTraceBeginDirectSaeEpisode
fi

echo "Built $OUTPUT"
echo "OK: all Tahoe trace producer objects link external trace bridges"
if [ "$IWN_SOFTWARE_PMF_LAB" -eq 1 ]; then
    echo "OK: IWN direct-SAE WCL producer links the lab trace begin bridge"
fi
