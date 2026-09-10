#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BA_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$BA_TEST_DIR/production.inc" "$BA_TEST_DIR/defines.inc" "$BA_TEST_DIR/test"; rm -rf "$BA_TEST_DIR/test.dSYM"; rmdir "$BA_TEST_DIR"' EXIT
awk '
    /^#define[[:space:]]+IW[MX]_(MAX_RX_BA_SESSIONS|RX_REORDER_DATA_INVALID_BAID|MAX_TID_COUNT|MAX_BAID|FIRST_AGG_TX_QUEUE|DQA_MIN_DATA_QUEUE|SF_INIT_OFF|FW_CTXT_ACTION_MODIFY|UCODE_TLV_CAPA_DYNAMIC_QUOTA|STATION_ID)[[:space:]]/ { print }
' "$PROJECT_DIR/itlwm/hal_iwm/if_iwmreg.h" "$PROJECT_DIR/itlwm/hal_iwm/if_iwmvar.h" \
  "$PROJECT_DIR/itlwm/hal_iwx/if_iwxreg.h" "$PROJECT_DIR/itlwm/hal_iwx/if_iwxvar.h" \
  "$PROJECT_DIR/itlwm/hal_iwm/mac80211.cpp" "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp" > "$BA_TEST_DIR/defines.inc"
for source_file in itlwm/hal_iwm/mac80211.cpp itlwm/hal_iwx/ItlIwx.cpp; do
    if [ -n "${RX_BA_BASELINE:-}" ]; then
        git -C "$PROJECT_DIR" show "$RX_BA_BASELINE:$source_file"
    else
        sed -n '1,$p' "$PROJECT_DIR/$source_file"
    fi | awk -v defines="$BA_TEST_DIR/defines.inc" '
        /^(int|void) ItlIw[mx]::$/ { type=$0 }
        /^static struct iwx_rxba_data \*$/ { type=$0 }
        /^(iw[mx]_(run_stop|sta_rx_agg)|iwx_find_rxba_data)\(/ {
            if ($0 ~ /^iwx_sta_rx_agg\(/) {
                split(type, fields, " "); print "#define IWX_RX_AGG_RETURN " fields[1] >> defines
            }
            selected=1; print type
        }
        selected { print } selected && /^}/ { selected=0 }
    '
done > "$BA_TEST_DIR/production.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-sign-compare -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer -I "$BA_TEST_DIR" \
    "$PROJECT_DIR/tests/primary_rx_ba_teardown_test.cpp" -o "$BA_TEST_DIR/test"
"$BA_TEST_DIR/test" "${RX_BA_FAMILY:-all}"
