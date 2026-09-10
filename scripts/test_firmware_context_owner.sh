#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CONTEXT_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$CONTEXT_TEST_DIR/context-defines.inc" "$CONTEXT_TEST_DIR/context-host-commands.inc" "$CONTEXT_TEST_DIR/context-methods.inc" "$CONTEXT_TEST_DIR/context-test"; rm -rf "$CONTEXT_TEST_DIR/context-test.dSYM"; rmdir "$CONTEXT_TEST_DIR"' EXIT
awk '
    /^#define[[:space:]]+IW[MX]_FLAG_(MAC_ACTIVE|BINDING_ACTIVE|STA_ACTIVE|TE_ACTIVE|SHUTDOWN)[[:space:]]/ { print }
    /^#define[[:space:]]+IEEE80211_(NWID_LEN|ADDR_LEN|DUR_TU|HTOP1_PROT_MASK|NODE_QOS|NODE_HT|NODE_HE|F_SHPREAMBLE|F_SHSLOT|F_USEPROT)[[:space:]]/ { print }
    /^#define[[:space:]]+EDCA_NUM_AC[[:space:]]/ { print }
    /^struct ieee80211_frame \{/ { selected=1 }
    /^enum ieee80211_(edca_ac|htprot) \{/ { selected=1 }
    selected { print } selected && /^}( __packed)?;/ { selected=0 }
' "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.h" \
  "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_node.h" \
  "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" \
  "$PROJECT_DIR/itlwm/hal_iwm/if_iwmvar.h" \
  "$PROJECT_DIR/itlwm/hal_iwx/if_iwxvar.h" > "$CONTEXT_TEST_DIR/context-defines.inc"
awk '
    /^#define[[:space:]]+IW[MX]_MAX_CMD_TBS_PER_TFD[[:space:]]/ { print }
    /^struct iw[mx]_host_cmd \{/ { selected=1 }
    /^enum IW[MX]_CMD_MODE \{/ { selected=1 }
    selected { print } selected && /^};/ { selected=0 }
' "$PROJECT_DIR/itlwm/hal_iwm/if_iwmvar.h" "$PROJECT_DIR/itlwm/hal_iwx/if_iwxvar.h" > "$CONTEXT_TEST_DIR/context-host-commands.inc"
awk '
    /^bool ItlIw[mx]::$/ { type=$0 }
    /^firmwareContextCommandCurrentLocked\(/ { selected=1; print type }
    selected { print } selected && /^}/ { selected=0 }
' "$PROJECT_DIR/itlwm/hal_iwm/ItlIwm.cpp" "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp" > "$CONTEXT_TEST_DIR/context-methods.inc"
for source_file in itlwm/hal_iwm/mac80211.cpp itlwm/hal_iwm/phy.cpp itlwm/hal_iwx/ItlIwx.cpp; do
    if [ -n "${CONTEXT_NEGATIVE_REF:-}" ]; then
        git -C "$PROJECT_DIR" show "$CONTEXT_NEGATIVE_REF:$source_file"
    else
        sed -n '1,$p' "$PROJECT_DIR/$source_file"
    fi | awk '
        /^(int|void|bool) ItlIw[mx]::$/ { type=$0 }
        /^iw[mx]_(mac_ctxt_cmd|mac_ctxt_cmd_common|mac_ctxt_cmd_fill_sta|binding_cmd|send_cmd_status)\(/ { selected=1; print type }
        selected { print } selected && /^}/ { selected=0 }
    '
done >> "$CONTEXT_TEST_DIR/context-methods.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function \
    -Wno-gnu-variable-sized-type-not-at-end -Wno-sign-compare \
    -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$CONTEXT_TEST_DIR" \
    "$PROJECT_DIR/tests/firmware_context_owner_test.cpp" -o "$CONTEXT_TEST_DIR/context-test"
"$CONTEXT_TEST_DIR/context-test" "${CONTEXT_CASE:-all}" "${CONTEXT_FAMILY:-all}"
