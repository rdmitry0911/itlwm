#!/bin/bash

set -euo pipefail

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

require_file() {
    local path="$1"
    [ -f "$path" ] || fail "missing required file: $path"
}

require_literal() {
    local needle="$1"
    local path="$2"
    local label="$3"

    grep -Fq -- "$needle" "$path" || fail "missing $label in $path"
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
EXPECTED_TOPLEVEL="/Users/devops/Projects/itlwm"

TOPLEVEL="$(git -C "$PROJECT_DIR" rev-parse --show-toplevel)"
[ "$TOPLEVEL" = "$EXPECTED_TOPLEVEL" ] || fail "unexpected git toplevel: $TOPLEVEL"

if GIT_DIR="$(git -C "$PROJECT_DIR" rev-parse --absolute-git-dir 2>/dev/null)"; then
    :
else
    RAW_GIT_DIR="$(git -C "$PROJECT_DIR" rev-parse --git-dir)"
    case "$RAW_GIT_DIR" in
        /*) GIT_DIR="$RAW_GIT_DIR" ;;
        *) GIT_DIR="$TOPLEVEL/$RAW_GIT_DIR" ;;
    esac
fi
[ "$GIT_DIR" = "$EXPECTED_TOPLEVEL/.git" ] || fail "unexpected git dir: $GIT_DIR"

HEAD_COMMIT="$(git -C "$PROJECT_DIR" rev-parse HEAD)"
git -C "$PROJECT_DIR" remote -v >/dev/null
git -C "$PROJECT_DIR" log --oneline -1 >/dev/null

README="$PROJECT_DIR/README.md"
LICENSE="$PROJECT_DIR/LICENSE"
INFO_PLIST="$PROJECT_DIR/AirportItlwm/Info.plist"
BUILD_SCRIPT="$PROJECT_DIR/scripts/build_tahoe.sh"
PROJECT_FILE="$PROJECT_DIR/itlwm.xcodeproj/project.pbxproj"
REPRO_DOC="$PROJECT_DIR/docs/tahoe_lineage_build_reproducibility.md"

require_file "$README"
require_file "$LICENSE"
require_file "$INFO_PLIST"
require_file "$BUILD_SCRIPT"
require_file "$PROJECT_FILE"
require_file "$REPRO_DOC"

require_literal "OpenIntelWireless/itlwm" "$README" "OpenIntelWireless lineage anchor"
require_literal "OpenBSD" "$README" "OpenBSD base anchor"
require_literal "GNU GENERAL PUBLIC LICENSE" "$LICENSE" "GPL license anchor"
require_literal "Version 2" "$LICENSE" "GPLv2 license anchor"
require_literal "com.zxystd.AirportItlwm" "$INFO_PLIST" "AirportItlwm bundle id"
require_literal "AirportItlwm" "$INFO_PLIST" "AirportItlwm bundle name"

require_literal "AirportItlwm-Tahoe" "$PROJECT_FILE" "Tahoe Xcode target"
require_literal "TARGET=\"AirportItlwm-Tahoe\"" "$BUILD_SCRIPT" "Tahoe build target"
require_literal "VARIANT_LABEL=\"Tahoe\"" "$BUILD_SCRIPT" "default Tahoe variant"
require_literal "Build/Debug/Tahoe/AirportItlwm.kext" "$BUILD_SCRIPT" "default staged kext path"
require_literal "OUTPUT_KEXT=\"\$OUTPUT_ROOT/AirportItlwm.kext\"" "$BUILD_SCRIPT" "staged kext variable"
require_literal "BOOTKC=\"\${1:-/Volumes/macos-750/System/Library/KernelCollections/BootKernelExtensions.kc}\"" "$BUILD_SCRIPT" "default BootKC path"
require_literal "nm -u \"\$OUTPUT_BINARY\"" "$BUILD_SCRIPT" "kext undefined-symbol extraction"
require_literal "nm -g \"\$BOOTKC\"" "$BUILD_SCRIPT" "BootKC export extraction"
require_literal "comm -23 \"\$TMPDIR_SYM/kext_undef.txt\" \"\$TMPDIR_SYM/bootkc_exports.txt\"" "$BUILD_SCRIPT" "undefined-symbol comparison"
require_literal "OK: all \$TOTAL undefined symbols resolve against BootKC" "$BUILD_SCRIPT" "symbol-check success line"

require_literal "timeout 420s ./scripts/build_tahoe.sh" "$REPRO_DOC" "bounded Tahoe build command"
require_literal "Build/Debug/Tahoe/AirportItlwm.kext" "$REPRO_DOC" "documented staged kext path"
require_literal "OK: all <count> undefined symbols resolve against BootKC" "$REPRO_DOC" "documented BootKC success gate"
require_literal "WARNING: BootKC not found" "$REPRO_DOC" "documented skipped-symbol constraint"
require_literal "timeout 120s sudo cp -R Build/Debug/Tahoe/AirportItlwm.kext" "$REPRO_DOC" "documented install envelope"
require_literal "timeout 120s sudo shutdown -r now" "$REPRO_DOC" "documented reboot envelope"
require_literal "unload the currently loaded driver" "$REPRO_DOC" "documented unload prohibition"
require_literal "does not capture final runtime evidence" "$REPRO_DOC" "documented runtime non-claim"

bash -n "$BUILD_SCRIPT" >/dev/null

cat <<EOF
source_toplevel=$TOPLEVEL
git_dir=$GIT_DIR
head_commit=$HEAD_COMMIT
lineage_anchors=PASS
tahoe_target=AirportItlwm-Tahoe
build_command=timeout 420s ./scripts/build_tahoe.sh [BOOTKC_PATH]
staged_kext=$EXPECTED_TOPLEVEL/Build/Debug/Tahoe/AirportItlwm.kext
bootkc_symbol_gate=PASS_TEXT_VERIFIED
install_reboot_envelope=docs/tahoe_lineage_build_reproducibility.md
runtime_evidence_captured=NO
wifi_success_claimed=NO
EOF
