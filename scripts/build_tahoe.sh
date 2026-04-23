#!/bin/bash
#
#  build_tahoe.sh
#  itlwm
#
#  Builds only AirportItlwm.kext for Tahoe and stages it into:
#    Build/Debug/Tahoe/AirportItlwm.kext
#  Then verifies all undefined symbols resolve against the target
#  kernel's BootKernelExtensions.kc.
#
#  Usage:
#    ./scripts/build_tahoe.sh [BOOTKC_PATH]
#
#  BOOTKC_PATH defaults to /Volumes/macos-750/System/Library/KernelCollections/BootKernelExtensions.kc
#  If the BootKC is not found, the build succeeds but the symbol check is skipped.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TARGET="AirportItlwm-Tahoe"
CONFIGURATION="Debug"
DERIVED_DATA="$PROJECT_DIR/DerivedData"
OUTPUT_ROOT="$PROJECT_DIR/Build/$CONFIGURATION/Tahoe"
OUTPUT_KEXT="$OUTPUT_ROOT/AirportItlwm.kext"
OUTPUT_BINARY="$OUTPUT_KEXT/Contents/MacOS/AirportItlwm"
BUILD_KEXT="$DERIVED_DATA/Build/Products/$CONFIGURATION/Tahoe/AirportItlwm.kext"
BUILD_BINARY="$BUILD_KEXT/Contents/MacOS/AirportItlwm"

BOOTKC="${1:-/Volumes/macos-750/System/Library/KernelCollections/BootKernelExtensions.kc}"

# ── Step 1: Patch MacKernelSDK if needed ─────────────────────────────
patch_mackernelsdk() {
    local header="$PROJECT_DIR/MacKernelSDK/Headers/IOKit/network/IONetworkController.h"

    if [ ! -f "$header" ]; then
        echo "WARNING: MacKernelSDK not found, skipping patch"
        return
    fi

    # IONetworkController reserved slots 6/7: promoted to real methods
    # in macOS 26.x (allocatePacketNoWait, setHardwareAssists).
    # The upstream MacKernelSDK still has them as ReservedUnused.
    if grep -q 'OSMetaClassDeclareReservedUnused( IONetworkController,  6)' "$header"; then
        echo "Patching IONetworkController.h: slots 6/7 -> ReservedUsed"
        sed -i '' '/^#endif.*!__PRIVATE_SPI__/{
N
s|\(#endif.*!__PRIVATE_SPI__.*\)\n[[:space:]]*OSMetaClassDeclareReservedUnused( IONetworkController,  6);|\1\
\
    // Slot 6: allocatePacketNoWait — promoted from ReservedUnused in macOS 26.x\
    virtual mbuf_t allocatePacketNoWait(UInt32 size);\
    OSMetaClassDeclareReservedUsed( IONetworkController,  6);|
}' "$header"
        sed -i '' 's/^[[:space:]]*OSMetaClassDeclareReservedUnused( IONetworkController,  7);/\
    \/\/ Slot 7: setHardwareAssists — promoted from ReservedUnused in macOS 26.x\
    virtual IOReturn setHardwareAssists(UInt32 hardwareAssists, UInt32 hardwareAssistsMask);\
    OSMetaClassDeclareReservedUsed( IONetworkController,  7);/' "$header"
        echo "  done"
    else
        echo "IONetworkController.h: slots 6/7 already patched"
    fi
}

patch_mackernelsdk

# ── Step 2: Build ────────────────────────────────────────────────────
GIT_HASH=$(cd "$PROJECT_DIR" && git rev-parse --short HEAD 2>/dev/null || echo "unknown")
BUILD_SETTINGS=$(xcodebuild -project "$PROJECT_DIR/itlwm.xcodeproj" \
    -scheme "$TARGET" \
    -configuration "$CONFIGURATION" \
    -showBuildSettings \
    GCC_PREPROCESSOR_DEFINITIONS='$(inherited) ITLWM_COMMIT_HASH='"$GIT_HASH")
if ! printf '%s\n' "$BUILD_SETTINGS" | grep -q 'USE_APPLE_SUPPLICANT'; then
    echo "ERROR: Tahoe target missing USE_APPLE_SUPPLICANT in effective GCC_PREPROCESSOR_DEFINITIONS"
    exit 1
fi

echo ""
echo "Building only AirportItlwm.kext via $TARGET ($CONFIGURATION) commit=$GIT_HASH..."
xcodebuild -project "$PROJECT_DIR/itlwm.xcodeproj" \
    -scheme "$TARGET" \
    -configuration "$CONFIGURATION" \
    -derivedDataPath "$DERIVED_DATA" \
    GCC_PREPROCESSOR_DEFINITIONS='$(inherited) ITLWM_COMMIT_HASH='"$GIT_HASH" \
    2>&1 | tail -5

if [ ! -f "$BUILD_BINARY" ]; then
    echo "ERROR: build output not found at $BUILD_BINARY"
    exit 1
fi

mkdir -p "$OUTPUT_ROOT"
rm -rf "$OUTPUT_KEXT"
cp -R "$BUILD_KEXT" "$OUTPUT_KEXT"

if [ ! -f "$OUTPUT_BINARY" ]; then
    echo "ERROR: staged build output not found at $OUTPUT_BINARY"
    exit 1
fi

echo ""
echo "Build succeeded: $OUTPUT_BINARY"

# ── Step 3: Verify symbols ───────────────────────────────────────────
echo ""
echo "Verifying symbols..."

if [ ! -f "$BOOTKC" ]; then
    echo "WARNING: BootKC not found at $BOOTKC"
    echo "  Symbol verification skipped. Pass the path as argument:"
    echo "  $0 /path/to/BootKernelExtensions.kc"
    exit 0
fi

TMPDIR_SYM=$(mktemp -d)
trap 'rm -rf "$TMPDIR_SYM"' EXIT

nm -u "$OUTPUT_BINARY" | sort -u > "$TMPDIR_SYM/kext_undef.txt"
nm -g "$BOOTKC" | awk '{print $3}' | sort -u > "$TMPDIR_SYM/bootkc_exports.txt"

UNRESOLVED=$(comm -23 "$TMPDIR_SYM/kext_undef.txt" "$TMPDIR_SYM/bootkc_exports.txt")

if [ -n "$UNRESOLVED" ]; then
    COUNT=$(echo "$UNRESOLVED" | wc -l | tr -d ' ')
    echo "FAIL: $COUNT unresolved symbol(s):"
    echo ""
    echo "$UNRESOLVED" | while read -r sym; do
        demangled=$(c++filt "$sym" 2>/dev/null || echo "$sym")
        echo "  $sym"
        echo "    -> $demangled"
    done
    echo ""
    echo "These symbols are referenced by the kext but not exported by the kernel."
    echo "Fix the header declarations before deploying."
    exit 1
fi

TOTAL=$(wc -l < "$TMPDIR_SYM/kext_undef.txt" | tr -d ' ')
echo "OK: all $TOTAL undefined symbols resolve against BootKC"
