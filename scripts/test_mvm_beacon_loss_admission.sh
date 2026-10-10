#!/usr/bin/env bash
# Execute complete IWM/IWX firmware missed-beacon handlers. Firmware delivery,
# generic state callbacks and credential recovery are explicit test boundaries.
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BEACON_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$BEACON_TEST_DIR/registers.inc" "$BEACON_TEST_DIR/handler.inc" "$BEACON_TEST_DIR/test"; rm -rf "$BEACON_TEST_DIR/test.dSYM"; rmdir "$BEACON_TEST_DIR"' EXIT
for family in ${1:-iwm iwx}; do
    case "$family" in
        iwm) owner=ItlIwm; source_file=itlwm/hal_iwm/mac80211.cpp ;;
        iwx) owner=ItlIwx; source_file=itlwm/hal_iwx/ItlIwx.cpp ;;
        *) echo "unknown beacon family: $family" >&2; exit 2 ;;
    esac
    awk -v family="$family" '
        $0 ~ "^struct " family "_(cmd_header|rx_packet|missed_beacons_notif) \\{" { selected=1 }
        $0 ~ "^#define[[:space:]]+" toupper(family) "_FH_RSCSR_FRAME_SIZE_MSK[[:space:]]" { print }
        $0 ~ "^" family "_rx_packet_(len|payload_len)\\(" { selected=1; print "static uint32_t" }
        selected { print }
        selected && /^}/ { selected=0 }
    ' "$PROJECT_DIR/itlwm/hal_$family/if_${family}reg.h" > "$BEACON_TEST_DIR/registers.inc"
    if [ -n "${BEACON_BASELINE:-}" ]; then
        git -C "$PROJECT_DIR" show "$BEACON_BASELINE:$source_file"
    else
        sed -n '1,$p' "$PROJECT_DIR/$source_file"
    fi | awk -v family="$family" -v owner="$owner" '
        $0 ~ "^" family "_rx_bmiss\\(" { selected=1; print "void " owner "::" }
        selected { print } selected && /^}/ { selected=0 }
    ' > "$BEACON_TEST_DIR/handler.inc"
    "${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror \
        -Wno-unused-parameter -Wno-unused-function -g \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -D"BEACON_FAMILY_$family" -I "$BEACON_TEST_DIR" \
        "$PROJECT_DIR/tests/mvm_beacon_loss_admission_test.cpp" \
        -o "$BEACON_TEST_DIR/test"
    "$BEACON_TEST_DIR/test"
done
