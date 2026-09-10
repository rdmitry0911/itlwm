#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
STA_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$STA_TEST_DIR/sta-commands.inc" "$STA_TEST_DIR/sta-defines.inc" "$STA_TEST_DIR/sta-host-commands.inc" "$STA_TEST_DIR/sta-test"; rm -rf "$STA_TEST_DIR/sta-test.dSYM"; rmdir "$STA_TEST_DIR"' EXIT
awk '
    /^#define[[:space:]]+IW[MX]_(FLAG_STA_ACTIVE|FLAG_TXFLUSH|FLAG_SHUTDOWN|STATION_ID|MONITOR_STA_ID|INVALID_QUEUE)[[:space:]]/ { print }
    /^#define[[:space:]]+IEEE80211_(NWID_LEN|ADDR_LEN)[[:space:]]/ { print }
    /^#define[[:space:]]+EDCA_AC_(BE|BK|VI|VO)[[:space:]]/ { print }
    /^struct ieee80211_frame \{/ { selected=1 }
    /^enum ieee80211_edca_ac \{/ { selected=1 }
    selected { print } selected && /^}( __packed)?;/ { selected=0 }
' \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.h" \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" \
    "$PROJECT_DIR/itlwm/hal_iwm/if_iwmvar.h" \
    "$PROJECT_DIR/itlwm/hal_iwx/if_iwxvar.h" > "$STA_TEST_DIR/sta-defines.inc"
awk '
    /^#define[[:space:]]+IW[MX]_MAX_CMD_TBS_PER_TFD[[:space:]]/ { print }
    /^struct iw[mx]_host_cmd \{/ { selected=1 }
    /^enum IW[MX]_CMD_MODE \{/ { selected=1 }
    selected { print } selected && /^};/ { selected=0 }
' "$PROJECT_DIR/itlwm/hal_iwm/if_iwmvar.h" "$PROJECT_DIR/itlwm/hal_iwx/if_iwxvar.h" > "$STA_TEST_DIR/sta-host-commands.inc"
awk '
    /^(int|bool) ItlIw[mx]::$/ { type=$0 }
    /^(beginPrimaryStationCleanup|finishPrimaryStationCleanup|firmwareContextCommandCurrentLocked|primaryStationCleanupCurrent)\(/ { selected=1; print type }
    selected { print } selected && /^}/ { selected=0 }
' "$PROJECT_DIR/itlwm/hal_iwm/ItlIwm.cpp" "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp" > "$STA_TEST_DIR/sta-commands.inc"
for source_file in itlwm/hal_iwm/power.cpp itlwm/hal_iwx/ItlIwx.cpp; do
    sed -n '1,$p' "$PROJECT_DIR/$source_file" | awk '
        /^int ItlIw[mx]::$/ { type=$0 }
        /^(iw[mx]_(rm_sta_cmd|drain_sta)|iwx_(flush_sta|rm_sta|flush_station|remove_station|flush_sta_tids|disable_txq))\(/ { selected=1; print type }
        selected { print } selected && /^}/ { selected=0 }
    '
    if [ -n "${STA_COMMAND_NEGATIVE_REF:-}" ]; then
        git -C "$PROJECT_DIR" show "$STA_COMMAND_NEGATIVE_REF:$source_file"
    else
        sed -n '1,$p' "$PROJECT_DIR/$source_file"
    fi | awk '
        /^int ItlIw[mx]::$/ { type=$0 }
        /^iw[mx]_add_sta_cmd\(/ { selected=1; print type }
        selected { print } selected && /^}/ { selected=0 }
    '
done >> "$STA_TEST_DIR/sta-commands.inc"
awk '
    /^int ItlIwm::$/ { type=$0 }
    /^iwm_(flush_tx_path|disable_txq)\(/ { selected=1; print type }
    selected { print } selected && /^}/ { selected=0 }
' "$PROJECT_DIR/itlwm/hal_iwm/mac80211.cpp" "$PROJECT_DIR/itlwm/hal_iwm/tx.cpp" >> "$STA_TEST_DIR/sta-commands.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function \
    -Wno-gnu-variable-sized-type-not-at-end -Wno-sign-compare \
    -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$STA_TEST_DIR" \
    "$PROJECT_DIR/tests/sta_command_completion_test.cpp" -o "$STA_TEST_DIR/sta-test"
"$STA_TEST_DIR/sta-test" "${STA_COMMAND_CASE:-all}" "${STA_COMMAND_FAMILY:-all}"
