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

TOPLEVEL="$(git -C "$PROJECT_DIR" rev-parse --show-toplevel)"

GIT_ENTRY="$TOPLEVEL/.git"
if [ -f "$GIT_ENTRY" ]; then
    # A normal linked worktree keeps this pointer inside the checkout while
    # Git stores its administrative data under the common worktree's
    # .git/worktrees directory. Rejecting that standard layout made this
    # otherwise source-only smoke check unusable in the project's scratch
    # worktrees.
    RAW_GIT_DIR="$(sed -n 's/^gitdir: //p' "$GIT_ENTRY")"
    [ -n "$RAW_GIT_DIR" ] || fail "linked worktree .git entry lacks gitdir pointer"
    case "$RAW_GIT_DIR" in
        /*) GIT_DIR="$RAW_GIT_DIR" ;;
        *) GIT_DIR="$TOPLEVEL/$RAW_GIT_DIR" ;;
    esac
    [ -d "$GIT_DIR" ] || fail "linked worktree gitdir is missing: $GIT_DIR"
    GIT_LAYOUT="linked-worktree"
elif [ -d "$GIT_ENTRY" ]; then
    GIT_DIR="$GIT_ENTRY"
    GIT_LAYOUT="standalone-worktree"
else
    fail "missing worktree-local .git entry: $GIT_ENTRY"
fi
git -C "$PROJECT_DIR" rev-parse --is-inside-work-tree | grep -qx true ||
    fail "git does not recognize the audited directory as a worktree"

HEAD_COMMIT="$(git -C "$PROJECT_DIR" rev-parse HEAD)"
ORIGIN_URL="$(git -C "$PROJECT_DIR" remote get-url origin 2>/dev/null || true)"
case "$(printf '%s' "$ORIGIN_URL" | tr '[:upper:]' '[:lower:]')" in
    "") ORIGIN_KIND="unset" ;;
    /*|file://*|*127.0.0.1*|*localhost*|*host-mirror*|*private-mirror*|*synthetic*)
        ORIGIN_KIND="local-or-private-mirror"
        ;;
    *) ORIGIN_KIND="configured-remote" ;;
esac
git -C "$PROJECT_DIR" log --oneline -1 >/dev/null

README="$PROJECT_DIR/README.md"
LICENSE="$PROJECT_DIR/LICENSE"
INFO_PLIST="$PROJECT_DIR/AirportItlwm/Info.plist"
BUILD_SCRIPT="$PROJECT_DIR/scripts/build_tahoe.sh"
PREFLIGHT_SCRIPT="$PROJECT_DIR/scripts/tahoe_auxkc_admission_preflight.sh"
PROJECT_FILE="$PROJECT_DIR/itlwm.xcodeproj/project.pbxproj"
REPRO_DOC="$PROJECT_DIR/docs/tahoe_lineage_build_reproducibility.md"

require_file "$README"
require_file "$LICENSE"
require_file "$INFO_PLIST"
require_file "$BUILD_SCRIPT"
require_file "$PREFLIGHT_SCRIPT"
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
require_literal "_thread_call_cancel_wait" "$BUILD_SCRIPT" "AuxKC-private-symbol rejection"

require_literal "timeout 420s ./scripts/build_tahoe.sh" "$REPRO_DOC" "bounded Tahoe build command"
require_literal "Build/Debug/Tahoe/AirportItlwm.kext" "$REPRO_DOC" "documented staged kext path"
require_literal "OK: all <count> undefined symbols resolve against BootKC" "$REPRO_DOC" "documented BootKC success gate"
require_literal "WARNING: BootKC not found" "$REPRO_DOC" "documented skipped-symbol constraint"
require_literal "Private AuxKC Admission Preflight" "$REPRO_DOC" "documented private admission boundary"
require_literal "scripts/tahoe_auxkc_admission_preflight.sh" "$REPRO_DOC" "documented private preflight helper"
require_literal "canonical AirportItlwm bundle and canonical AuxKC remain read-only" "$REPRO_DOC" "documented canonical non-mutation"
require_literal "--explicit-only" "$REPRO_DOC" "documented five-path preflight"
require_literal "does not call \`--no-authorization\`" "$REPRO_DOC" "documented private-preflight scope"
require_literal "sudo kmutil create -n aux --arch x86_64" "$REPRO_DOC" "documented full AuxKC rebuild"
require_literal "--elide-identifier com.apple.driver.AppleSunrise" "$REPRO_DOC" "documented full-repository elision"
require_literal "-r /Library/Apple/System/Library/Extensions" "$REPRO_DOC" "documented second repository"
require_literal 'Do not use an explicit one-bundle `--bundle-path` collection' "$REPRO_DOC" "documented one-bundle prohibition"
require_literal "AuxiliaryKernelExtensions.kc.preinstall-bak" "$REPRO_DOC" "documented AuxKC rollback backup"
require_literal "sudo /sbin/reboot" "$REPRO_DOC" "documented reboot envelope"
require_literal "kmutil showloaded | grep -i AirportItlwm" "$REPRO_DOC" "documented loaded-kext verification"
require_literal "unload the currently loaded driver" "$REPRO_DOC" "documented unload prohibition"
require_literal "does not capture final runtime evidence" "$REPRO_DOC" "documented runtime non-claim"
require_literal "current git worktree" "$REPRO_DOC" "portable source checkout wording"
require_literal '`.git` entry must remain inside that worktree' "$REPRO_DOC" "same-worktree git metadata boundary"

bash -n "$BUILD_SCRIPT" >/dev/null
bash -n "$PREFLIGHT_SCRIPT" >/dev/null

cat <<EOF
source_toplevel=$TOPLEVEL
git_dir=$GIT_DIR
git_layout=$GIT_LAYOUT
source_repo=$ORIGIN_URL
source_repo_kind=$ORIGIN_KIND
head_commit=$HEAD_COMMIT
lineage_anchors=PASS
tahoe_target=AirportItlwm-Tahoe
build_command=timeout 420s ./scripts/build_tahoe.sh [BOOTKC_PATH]
staged_kext=Build/Debug/Tahoe/AirportItlwm.kext
bootkc_symbol_gate=PASS_TEXT_VERIFIED
install_reboot_envelope=docs/tahoe_lineage_build_reproducibility.md
runtime_evidence_captured=NO
wifi_success_claimed=NO
EOF
