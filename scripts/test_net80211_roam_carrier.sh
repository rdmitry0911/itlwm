#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
ROAM_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$ROAM_TEST_DIR/production.inc" "$ROAM_TEST_DIR/constants.inc" "$ROAM_TEST_DIR/controller.inc" "$ROAM_TEST_DIR/test"; rm -rf "$ROAM_TEST_DIR/test.dSYM"; rmdir "$ROAM_TEST_DIR"' EXIT
PROTO="$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_proto.c"
awk '/^#define[ \t]+IEEE80211_(F_RSNON|F_WEPON|F_DESBSSID|CAPINFO_PRIVACY|NODE_MFP|RSNCAP_MFPC|PROTO_RSN|EVT_STA_ROAM_LINK_LOST|WCL_REASSOC_OWNER_LEAF_ROAM_STARTED)[ \t]/' \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.h" \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_node.h" > "$ROAM_TEST_DIR/constants.inc"
sed -n '/^struct ieee80211_roam_link_loss {/,/^};/p' \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" >> "$ROAM_TEST_DIR/constants.inc"
awk '/^ieee80211_bss_switch_identity_current_locked\(/ { selected=1; print "int" }
    selected { print } selected && /^}/ { selected=0 }' \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_node.c" > "$ROAM_TEST_DIR/production.inc"
awk -v baseline="${ROAM_LOSS_BASELINE:-}" '
    /^ieee80211_bssid_is_unicast_nonzero\(/ { selected=1; print "int" }
    /^ieee80211_roam_link_(take_loss_locked|take_loss|loss_current)\(/ { selected=1; print "int" }
    /^ieee80211_roam_link_(loss_deliver|cancel)\(/ { selected=1; print "void" }
    /^ieee80211_pae_assoc_epoch_begin_internal\(/ && baseline == "" { selected=1; print "uint64_t" }
    /^ieee80211_pae_assoc_epoch_note_newstate\(/ { selected=1; print "void" }
    /^ieee80211_sae_wcl_fresh_carrier_accepted\(/ { selected=1; print "void" }
    /^ieee80211_roam_link_source_epoch\(/ { selected=1; print "uint64_t" }
    /^ieee80211_roam_link_(begin|failed|note_terminal)\(/ { selected=1; print "void" }
    /^ieee80211_roam_link_progress\(/ { selected=1; print "int" }
    /^ieee80211_set_link_state\(/ { selected=1; print "void" }
    selected { print }
    selected && /^}/ { selected=0 }
' "$PROTO" >> "$ROAM_TEST_DIR/production.inc"
if [ -n "${ROAM_LOSS_BASELINE:-}" ]; then
    # Replace only this exact production function with the unchanged old one.
    # The new helper/consumer fixture is retained, so failure must be semantic.
    git -C "$PROJECT_DIR" show "$ROAM_LOSS_BASELINE:itl80211/openbsd/net80211/ieee80211_proto.c" |
        awk '/^ieee80211_pae_assoc_epoch_begin_internal\(/ { selected=1; print "uint64_t" }
             selected { print } selected && /^}/ { selected=0 }' >> "$ROAM_TEST_DIR/production.inc"
fi
sed -n '/^struct TahoeWclLinkChangedPayload {/,/^} __attribute__((packed));/p' \
    "$PROJECT_DIR/AirportItlwm/AirportItlwmV2.cpp" >> "$ROAM_TEST_DIR/constants.inc"
awk '/^static IOReturn postTahoeWclRoamLinkLossGated\(/ { selected=1 }
     selected { print } selected && /^}/ { selected=0 }' \
    "$PROJECT_DIR/AirportItlwm/AirportItlwmV2.cpp" > "$ROAM_TEST_DIR/controller.inc"
if [ -n "${ROAM_CARRIER_BASELINE:-}" ]; then
    git -C "$PROJECT_DIR" show "$ROAM_CARRIER_BASELINE:itl80211/openbsd/net80211/ieee80211_node.c"
else
    sed -n '/^ieee80211_node_join_bss(/,/^struct ieee80211_node \*/p' \
        "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_node.c"
fi | awk '
    /^ieee80211_node_join_bss\(/ { selected=1; print "void" }
    selected { print }
    selected && /^}/ { selected=0 }
' >> "$ROAM_TEST_DIR/production.inc"
ROAM_BASELINE_FLAGS=(-DROAM_CURRENT_EPOCH)
if [ -n "${ROAM_LOSS_BASELINE:-}" ]; then
    ROAM_BASELINE_FLAGS=(-DROAM_LOSS_BASELINE)
fi
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    "${ROAM_BASELINE_FLAGS[@]}" \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$ROAM_TEST_DIR" "$PROJECT_DIR/tests/net80211_roam_carrier_test.cpp" \
    -o "$ROAM_TEST_DIR/test"
"$ROAM_TEST_DIR/test"

# Verify the tested decision/terminal functions remain wired to the real
# common bridge and all three hardware failure paths.
sed -n '/^ieee80211_newstate(/,/^ieee80211_set_link_state(/p' "$PROTO" |
    grep -F 'if (!ieee80211_roam_link_progress(ic, ostate, nstate))' >/dev/null
grep -Fq 'ieee80211_roam_link_failed(ic, roam_epoch);' "$PROJECT_DIR/itlwm/hal_iwn/ItlIwn.cpp"
for family in Iwm Iwx; do
    case "$family" in
        Iwm) backend=iwm; worker=itlwm/hal_iwm/mac80211.cpp ;;
        Iwx) backend=iwx; worker=itlwm/hal_iwx/ItlIwx.cpp ;;
    esac
    # The terminal now crosses an identity-owned async mailbox. Preserve the
    # failure wiring check at its real, main-workloop-serialized consumer.
    sed -n "/^${backend}_newstate_task(/,/^}/p" "$PROJECT_DIR/$worker" |
        grep -F 'postStateTransitionCommit(request, err)' >/dev/null
    sed -n '/^drainStateTransitionCommit(/,/^}/p' \
        "$PROJECT_DIR/itlwm/hal_$backend/Itl$family.cpp" |
        grep -F 'ieee80211_roam_link_failed(&com.sc_ic, request.identity.associationEpoch);' >/dev/null
done

# Explicit WCL teardown owns its own terminal; all early returns follow
# cancellation of the replacement-only notification lease.
for method in setDISASSOCIATE setWCL_LEAVE_NETWORK setWCL_JOIN_ABORT; do
    awk -v method="$method" '
        $0 ~ "^" method "\\(" { selected=1 }
        selected && /ieee80211_roam_link_cancel\(ic\)/ { canceled=1 }
        selected && /clearExternalPmkEligibilityLocked\(/ {
            if (!canceled) exit 1; found=1
        }
        selected && /^}/ { exit !found }
        END { if (!found) exit 1 }
    ' "$PROJECT_DIR/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
done
sed -n '/case IEEE80211_EVT_STA_BEACON_LOSS:/,/case IEEE80211_EVT_STA_ROAM_LINK_LOST:/p' \
    "$PROJECT_DIR/AirportItlwm/AirportItlwmV2.cpp" | grep -F 'ieee80211_roam_link_cancel(ic)' >/dev/null
sed -n '/if (action == kAirportItlwmDeferredPowerAvailabilityPublishOff)/,/postTahoeDriverAvailabilityTransition(/p' \
    "$PROJECT_DIR/AirportItlwm/AirportItlwmV2.cpp" | grep -F 'ieee80211_roam_link_cancel(' >/dev/null
