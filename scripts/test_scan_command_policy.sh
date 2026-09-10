#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
POLICY_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$POLICY_TEST_DIR/scan-policy-declarations.inc" "$POLICY_TEST_DIR/scan-policy-common.inc" "$POLICY_TEST_DIR/scan-policy-builders.inc" "$POLICY_TEST_DIR/scan-policy-host-cmd.inc" "$POLICY_TEST_DIR/scan-policy-defines.inc" "$POLICY_TEST_DIR/common" "$POLICY_TEST_DIR/builders"; rm -rf "$POLICY_TEST_DIR/common.dSYM" "$POLICY_TEST_DIR/builders.dSYM"; rmdir "$POLICY_TEST_DIR"' EXIT

awk '
    /^#define[[:space:]]+IEEE80211_(CHAN_MAX|NWID_LEN|ADDR_LEN)[[:space:]]/ { print }
    /^#define[[:space:]]+EDCA_AC_(BE|BK|VI|VO)[[:space:]]/ { print }
    /^struct ieee80211_frame \{/ { selected=3 }
    /^enum ieee80211_edca_ac \{/ { selected=2 }
    /^#define IEEE80211_WCL_SCAN_REQUEST_MAX_CHANNELS/ { selected=1 }
    /^struct ieee80211_channel \{/ { selected=2 }
    selected { print }
    selected == 1 && /^}/ { selected=0 }
    selected == 2 && /^};/ { selected=0 }
    selected == 3 && /^} __packed;/ { selected=0 }
    /^static inline u_int32_t/ { type=$0 }
    /^ieee80211_wcl_scan_time_or_default\(/ { selected=1; print type; print }
    /^#define IEEE80211_CHAN_[25]GHZ/ { print }
    /^#define[[:space:]]+IEEE80211_IS_CHAN_[25]GHZ/ { print; getline; print }
' "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.h" \
  "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" \
  > "$POLICY_TEST_DIR/scan-policy-declarations.inc"

if [ -n "${SCAN_POLICY_COMMON_NEGATIVE_REF:-}" ]; then
    git -C "$PROJECT_DIR" show "$SCAN_POLICY_COMMON_NEGATIVE_REF:itl80211/openbsd/net80211/ieee80211.c"
else
    sed -n '1,$p' "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.c"
fi | awk '
    /^(int|void)$/ { type=$0 }
    /^ieee80211_wcl_scan_plan_(stage|snapshot|clear|channel_allowed)\(/ { selected=1; print type }
    selected { print }
    selected && /^}/ { selected=0 }
' > "$POLICY_TEST_DIR/scan-policy-common.inc"

"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -pthread \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$POLICY_TEST_DIR" \
    "$PROJECT_DIR/tests/scan_policy_snapshot_test.cpp" -o "$POLICY_TEST_DIR/common"
"$POLICY_TEST_DIR/common"

for family in iwm iwx; do
    awk '
        /^#define[[:space:]]+IW[MX]_(MAX_CMD_TBS_PER_TFD|AUX_STA_ID|STATION_COUNT)[[:space:]]/ { print }
        /^enum IW[MX]_CMD_MODE \{/ { selected=1 }
        /^struct iw[mx]_host_cmd \{/ { selected=1 }
        selected { print }
        selected && /^};/ { selected=0 }
    ' "$PROJECT_DIR/itlwm/hal_$family/if_${family}var.h"
done > "$POLICY_TEST_DIR/scan-policy-host-cmd.inc"
awk '/^#define (IWM_SCAN_ADWELL|IWX_SCAN_ADWELL|IWL_SCAN_DWELL)/ { print }' \
    "$PROJECT_DIR/itlwm/hal_iwm/scan.cpp" \
    "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp" > "$POLICY_TEST_DIR/scan-policy-defines.inc"
for builder_file in itlwm/hal_iwm/scan.cpp itlwm/hal_iwx/ItlIwx.cpp; do
    if [ -n "${SCAN_POLICY_BUILDERS_NEGATIVE_REF:-}" ]; then
        git -C "$PROJECT_DIR" show "$SCAN_POLICY_BUILDERS_NEGATIVE_REF:$builder_file"
    else
        sed -n '1,$p' "$PROJECT_DIR/$builder_file"
    fi | awk '
        /ItlIw[mx]::$/ { type=$0 }
        /^iw[mx]_(lmac_scan|umac_scan|umac_scan_v12|umac_scan_v14|lmac_scan_fill_channels|umac_scan_fill_channels|umac_scan_size|get_scan_req_umac_chan_param|get_scan_req_umac_data)\(/ { selected=1; print type }
        selected { print }
        selected && /^}/ { selected=0 }
    '
done > "$POLICY_TEST_DIR/scan-policy-builders.inc"
negative_flag=-USCAN_POLICY_BUILDERS_NEGATIVE
if [ -n "${SCAN_POLICY_BUILDERS_NEGATIVE_REF:-}" ]; then
    negative_flag=-DSCAN_POLICY_BUILDERS_NEGATIVE
fi
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-reorder-init-list -Wno-missing-field-initializers -Wno-sign-compare \
    -Wno-gnu-variable-sized-type-not-at-end \
    -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$negative_flag" \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$POLICY_TEST_DIR" \
    "$PROJECT_DIR/tests/scan_command_policy_test.cpp" -o "$POLICY_TEST_DIR/builders"
"$POLICY_TEST_DIR/builders"
