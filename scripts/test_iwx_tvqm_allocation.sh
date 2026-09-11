#!/usr/bin/env bash
# Complete extracted allocation/retirement transaction. Firmware execution,
# interrupt scheduling and real DMA remain explicit on-hardware test gates.
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TVQM_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TVQM_TEST_DIR/production.inc" "$TVQM_TEST_DIR/defines.inc" "$TVQM_TEST_DIR/test"; rm -rf "$TVQM_TEST_DIR/test.dSYM"; rmdir "$TVQM_TEST_DIR"' EXIT
awk '
    /^#define[[:space:]]+IWX_(DEVICE_FAMILY_AX210|STATION_ID|MONITOR_STA_ID|AUX_STA_ID|FLAG_SHUTDOWN|INVALID_QUEUE|MAX_CMD_TBS_PER_TFD|CMD_RESP_MAX|QID_MGMT)[[:space:]]/ { print }
    /^#define[[:space:]]+IEEE80211_(NWID_LEN|ADDR_LEN)[[:space:]]/ { print }
    /^#define[[:space:]]+EDCA_AC_(BE|BK|VI|VO)[[:space:]]/ { print }
    /^struct ieee80211_frame \{/ { frame=1 }
    /^enum ieee80211_edca_ac \{/ { frame=1 }
    frame { print } frame && /^}( __packed)?;/ { frame=0 }
    /^struct iwx_host_cmd \{/ { selected=1 }
    /^enum IWX_CMD_MODE \{/ { selected=1 }
    selected { print } selected && /^};/ { selected=0 }
' "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.h" \
  "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" \
  "$PROJECT_DIR/itlwm/hal_iwx/if_iwxvar.h" > "$TVQM_TEST_DIR/defines.inc"
if [ -n "${TVQM_NEGATIVE_REF:-}" ]; then
    git -C "$PROJECT_DIR" show "$TVQM_NEGATIVE_REF:itlwm/hal_iwx/ItlIwx.cpp"
else
    sed -n '1,$p' "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp"
fi | awk '
    /^(int|void|bool) ItlIwx::$/ { type=$0 }
    /^iwx_(tvqm_alloc_txq|tvqm_enable_txq|tvqm_enable_txq_for_sta|tx_ring_init|alloc_tx_ring|allocate_tx_queue|tvqm_allocate_memory|enable_txq)\(/ { selected=1; print type }
    /^iwx_ap_exchange_tx_ring_carrier\(/ { selected=1; print "static int" }
    selected { print } selected && /^}/ { selected=0 }
' > "$TVQM_TEST_DIR/production.inc"
awk '
    /^(int|void|bool) ItlIwx::$/ { type=$0 }
    /^(beginTxQueueAllocation|txQueueAllocationCurrentLocked|finishTxQueueAllocation|resetTxQueueAllocation|iwx_disable_txq|iwx_retire_station_tx_queues|iwx_flush_sta_tids|iwx_ap_remove_internal_sta)\(/ { selected=1; print type }
    selected { print } selected && /^}/ { selected=0 }
' "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp" >> "$TVQM_TEST_DIR/production.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function \
    -Wno-sign-compare -Wno-gnu-variable-sized-type-not-at-end \
    -Wno-reorder-init-list -Wno-missing-field-initializers \
    -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$TVQM_TEST_DIR" \
    "$PROJECT_DIR/tests/iwx_tvqm_allocation_test.cpp" -o "$TVQM_TEST_DIR/test"
if [ "${TVQM_TEST_CASE:-all}" = all ]; then
    for version in 0 3 99; do
        for scenario in control local-dma preallocated ap-control transport short retry collision \
            missing flags bad-id stale-context closed pre-reset post-reset superseded reentrant fixed aux \
            retire-ap retire-primary retire-fixed retire-timeout retire-missing retire-failed \
            retire-flush retire-queue retire-reset retire-reentrant; do
            "$TVQM_TEST_DIR/test" "$scenario" "$version"
        done
    done
else
    "$TVQM_TEST_DIR/test" "${TVQM_TEST_CASE:-control}" "${TVQM_TEST_VERSION:-0}"
fi
