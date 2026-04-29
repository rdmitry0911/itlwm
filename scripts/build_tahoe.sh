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
    local tx_header="$PROJECT_DIR/MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkTxSubmissionQueue.h"
    local txc_header="$PROJECT_DIR/MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkTxCompletionQueue.h"
    local rx_header="$PROJECT_DIR/MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkRxCompletionQueue.h"
    local packet_header="$PROJECT_DIR/MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkPacket.h"

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

    # Tahoe BootKC exports Skywalk queue callbacks with UInt32 return type.
    # Some local MacKernelSDK copies declare them as IOReturn, which compiles
    # but links against non-exported mangled symbols.
    if [ -f "$tx_header" ] && grep -q 'typedef IOReturn (\*IOSkywalkTxSubmissionQueueAction)' "$tx_header"; then
        echo "Patching IOSkywalkTxSubmissionQueue.h: callbacks -> BootKC ABI"
        perl -0pi -e 's|typedef IOReturn \(\*IOSkywalkQueryFreeSpaceHandler\) \( OSObject \* owner, IOSkywalkTxSubmissionQueue \* queue, UInt32 \* outSpace \);|typedef UInt32 (*IOSkywalkQueryFreeSpaceHandler) ( OSObject * owner, IOSkywalkTxSubmissionQueue * queue, UInt32 * outSpace );|g; s|typedef IOReturn \(\*IOSkywalkTxSubmissionQueueAction\)\( OSObject \* owner, IOSkywalkTxSubmissionQueue \* queue, const IOSkywalkPacket \*\*, UInt32, void \* \);|typedef UInt32 (*IOSkywalkTxSubmissionQueueAction)( OSObject * owner, IOSkywalkTxSubmissionQueue * queue, IOSkywalkPacket * const *, UInt32, void * );|g' "$tx_header"
        echo "  done"
    elif [ -f "$tx_header" ]; then
        echo "IOSkywalkTxSubmissionQueue.h: callbacks already patched"
    fi

    if [ -f "$rx_header" ] && grep -q 'typedef IOReturn (\*IOSkywalkRxCompletionQueueAction)' "$rx_header"; then
        echo "Patching IOSkywalkRxCompletionQueue.h: callbacks -> BootKC ABI"
        perl -0pi -e 's|typedef IOReturn \(\*IOSkywalkRxCompletionQueueAction\)\( OSObject \* owner, IOSkywalkRxCompletionQueue \*, IOSkywalkPacket \*\*, UInt32, void \* \);|typedef UInt32 (*IOSkywalkRxCompletionQueueAction)( OSObject * owner, IOSkywalkRxCompletionQueue *, IOSkywalkPacket **, UInt32, void * );|g' "$rx_header"
        echo "  done"
    elif [ -f "$rx_header" ]; then
        echo "IOSkywalkRxCompletionQueue.h: callbacks already patched"
    fi

    if [ -f "$txc_header" ] && grep -q 'typedef IOReturn (\*IOSkywalkTxCompletionQueueAction)' "$txc_header"; then
        echo "Patching IOSkywalkTxCompletionQueue.h: callbacks -> BootKC ABI"
        perl -0pi -e 's|typedef IOReturn \(\*IOSkywalkTxCompletionQueueAction\)\( OSObject \* owner, IOSkywalkTxCompletionQueue \*, IOSkywalkPacket \*\*, UInt32, void \* \);|typedef UInt32 (*IOSkywalkTxCompletionQueueAction)( OSObject * owner, IOSkywalkTxCompletionQueue *, IOSkywalkPacket **, UInt32, void * );|g' "$txc_header"
        echo "  done"
    elif [ -f "$txc_header" ]; then
        echo "IOSkywalkTxCompletionQueue.h: callbacks already patched"
    fi

    # Tahoe 26.x has packet-array enqueue at vtable slot 0x2a0 and direct
    # packet/chain-head enqueue at slot 0x2a8.  Older local SDK headers model
    # the second overload as queue_entry*, which can make a packet array call
    # dispatch to the chain-head overload.
    if [ -f "$rx_header" ] && ! grep -Fq 'virtual IOReturn enqueuePackets( IOSkywalkPacket * const * packets, UInt32 packetCount, IOOptionBits options );' "$rx_header"; then
        echo "Patching IOSkywalkRxCompletionQueue.h: enqueuePackets overloads -> Tahoe ABI"
        perl -0pi -e 's|    virtual IOReturn enqueuePackets\( const IOSkywalkPacket \*\* packets, UInt32 packetCount, IOOptionBits options \);\n    virtual IOReturn enqueuePackets\( const queue_entry \* packets, UInt32 packetCount, IOOptionBits options \);|    virtual IOReturn enqueuePackets( IOSkywalkPacket * const * packets, UInt32 packetCount, IOOptionBits options );\n    virtual IOReturn enqueuePackets( IOSkywalkPacket * packet, UInt32 packetCount, IOOptionBits options );|g; s|    virtual IOReturn enqueuePackets\( const queue_entry \* packets, UInt32 packetCount, IOOptionBits options \);\n    virtual IOReturn enqueuePackets\( const IOSkywalkPacket \*\* packets, UInt32 packetCount, IOOptionBits options \);|    virtual IOReturn enqueuePackets( IOSkywalkPacket * const * packets, UInt32 packetCount, IOOptionBits options );\n    virtual IOReturn enqueuePackets( IOSkywalkPacket * packet, UInt32 packetCount, IOOptionBits options );|g' "$rx_header"
        if ! grep -Fq 'virtual IOReturn enqueuePackets( IOSkywalkPacket * const * packets, UInt32 packetCount, IOOptionBits options );' "$rx_header" ||
           ! grep -Fq 'virtual IOReturn enqueuePackets( IOSkywalkPacket * packet, UInt32 packetCount, IOOptionBits options );' "$rx_header"; then
            echo "ERROR: failed to align IOSkywalkRxCompletionQueue.h enqueuePackets overloads"
            exit 1
        fi
        echo "  done"
    elif [ -f "$rx_header" ]; then
        echo "IOSkywalkRxCompletionQueue.h: enqueuePackets overloads already patched"
    fi

    # Tahoe 26.x has five IOSkywalkPacket virtual methods between
    # setDataOffsetAndLength(...) and prepareWithQueue(...).  Older
    # MacKernelSDK headers omit them, so source calls to prepareWithQueue
    # compile to vtable slot 0x160 instead of Tahoe's real slot 0x188.
    if [ -f "$packet_header" ] && ! grep -q 'setDataOff( int64_t offset )' "$packet_header"; then
        echo "Patching IOSkywalkPacket.h: Tahoe packet vtable slots"
        perl -0pi -e 's|virtual IOReturn setDataOffsetAndLength\( UInt16 offset, UInt32 length \);\n\n    IOSkywalkPacketQueue \* getSourceQueue\(\);|virtual IOReturn setDataOffsetAndLength( UInt16 offset, UInt32 length );\n\n    // Tahoe 26.x packet-data virtual slots required before prepareWithQueue.\n    virtual IOReturn setDataOff( int64_t offset );\n    virtual int64_t getDataOff();\n    virtual IOReturn setDataOffAndLen( int64_t offset, uint64_t length );\n    virtual void * getDataVirtualAddress();\n    virtual void * getDataIOVirtualAddress();\n\n    IOSkywalkPacketQueue * getSourceQueue();|g' "$packet_header"
        echo "  done"
    elif [ -f "$packet_header" ]; then
        echo "IOSkywalkPacket.h: Tahoe packet vtable slots already patched"
    fi

    # CR-223: Tahoe BootKC exports the IOSkywalkPacket data-accessor
    # virtuals (and getPacketType) const-qualified (mangled __ZNK...).
    # MacKernelSDK header had them non-const, so any subclass that
    # constructs its own vtable emits __ZN... refs that the kernel
    # does not export. Adding `const` keeps the vtable slot layout
    # identical (these are pure accessors) and aligns the mangled
    # signatures with kernel exports. setDataOffAndLen's second param
    # is `unsigned long` in BootKC (mangled `m`), not `uint64_t` (`y`)
    # — fix the type for the same reason.
    if [ -f "$packet_header" ] && ! grep -q 'getDataLength() const' "$packet_header"; then
        echo "Patching IOSkywalkPacket.h: const-qualify Tahoe accessor virtuals"
        perl -0pi -e 's|    virtual UInt32 getPacketBuffers\( IOSkywalkPacketBuffer \*\* buffers, UInt32 maxBuffers \);|    virtual UInt32 getPacketBuffers( IOSkywalkPacketBuffer ** buffers, UInt32 maxBuffers ) const;|g' "$packet_header"
        perl -0pi -e 's|    virtual UInt32 getPacketBufferCount\(\);|    virtual UInt32 getPacketBufferCount() const;|g' "$packet_header"
        perl -0pi -e 's|    virtual IOMemoryDescriptor \* getMemoryDescriptor\(\);|    virtual IOMemoryDescriptor * getMemoryDescriptor() const;|g' "$packet_header"
        perl -0pi -e 's|    virtual UInt32 getDataLength\(\);|    virtual UInt32 getDataLength() const;|g' "$packet_header"
        perl -0pi -e 's|    virtual UInt16 getDataOffset\(\);|    virtual UInt16 getDataOffset() const;|g' "$packet_header"
        perl -0pi -e 's|    virtual int64_t getDataOff\(\);|    virtual int64_t getDataOff() const;|g' "$packet_header"
        perl -0pi -e 's|    virtual void \* getDataVirtualAddress\(\);|    virtual void * getDataVirtualAddress() const;|g' "$packet_header"
        perl -0pi -e 's|    virtual void \* getDataIOVirtualAddress\(\);|    virtual void * getDataIOVirtualAddress() const;|g' "$packet_header"
        perl -0pi -e 's|    virtual UInt32 getPacketType\(\);|    virtual UInt32 getPacketType() const;|g' "$packet_header"
        perl -0pi -e 's|    virtual IOReturn setDataOffAndLen\( int64_t offset, uint64_t length \);|    virtual IOReturn setDataOffAndLen( int64_t offset, unsigned long length );|g' "$packet_header"
        echo "  done"
    elif [ -f "$packet_header" ]; then
        echo "IOSkywalkPacket.h: const-qualified accessors already patched"
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
if [[ "$BUILD_SETTINGS" != *USE_APPLE_SUPPLICANT* ]]; then
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
