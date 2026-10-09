#!/usr/bin/env bash
# Execute full production MVM worker failure ownership and retirement.
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
family=${1:?iwm or iwx required}
case "$family" in iwm) mixed=Iwm; upper=IWM ;; iwx) mixed=Iwx; upper=IWX ;; *) exit 2 ;; esac
MVM_JOIN_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$MVM_JOIN_TEST_DIR/owner.inc" "$MVM_JOIN_TEST_DIR/worker.inc" "$MVM_JOIN_TEST_DIR/test"; rm -rf "$MVM_JOIN_TEST_DIR/test.dSYM"; rmdir "$MVM_JOIN_TEST_DIR"' EXIT
normalize() { sed -e "s/$family/iwn/g" -e "s/$mixed/Iwn/g" -e "s/$upper/IWN/g"; }
awk -v name="${family}_sae_engine_owner" '
    $0 ~ "^struct " name " " { selected=1 }
    selected { print } selected && /^};/ { exit }' \
    "$PROJECT_DIR/itlwm/hal_$family/if_${family}var.h" | normalize > "$MVM_JOIN_TEST_DIR/owner.inc"
awk '/^ieee80211_wcl_join_copy_current\(/ { selected=1; print "int" }
    selected { print } selected && /^}/ { selected=0 }' \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_proto.c" > "$MVM_JOIN_TEST_DIR/worker.inc"
awk -v family="$family" -v mixed="$mixed" '
    $0 ~ "^struct " mixed "SaeEngineCancellation " { selected=1 }
    $0 ~ "^" family "_sae_engine_(owner_clear_locked|cancel_owned|worker_retire|request_join_retirement|finish_join_retirement|wake_join_retirement|peer_exhausted)\\(" { selected=1; print "static void" }
    $0 ~ "^" family "_sae_tx_finish_join_retirement\\(" { selected=1; print "static void" }
    $0 ~ "^" family "_sae_engine_claim_peer_failure\\(" { selected=1; print "static u_int64_t" }
    $0 ~ "^" family "_sae_engine_(owner_matches_peer_locked|peer_owner_current_locked|take_peer_retry)\\(" { selected=1; print "static bool" }
    $0 ~ "^" family "_sae_engine_task\\(" && ENVIRON["MVM_SAE_WORKER_NEGATIVE_REF"] == "" { selected=1; print "void Itl" mixed "::" }
    $0 ~ "^" family "_sae_auth_hold\\(" { selected=1; print "int Itl" mixed "::" }
    selected { print }
    selected && /^};?$/ { selected=0 }' \
    "$PROJECT_DIR/itlwm/hal_$family/${mixed}SaeEngine.inc" | normalize >> "$MVM_JOIN_TEST_DIR/worker.inc"
if [ -n "${MVM_SAE_WORKER_NEGATIVE_REF:-}" ]; then
    # Replace only the complete worker with its historical production body;
    # keep actual current AUTH admission and owner/retirement helpers.
    git -C "$PROJECT_DIR" show "$MVM_SAE_WORKER_NEGATIVE_REF:itlwm/hal_$family/${mixed}SaeEngine.inc" |
        awk -v family="$family" -v mixed="$mixed" '
            $0 ~ "^" family "_sae_engine_task\\(" { selected=1; print "void Itl" mixed "::" }
            selected { print } selected && /^}/ { selected=0 }' |
        normalize >> "$MVM_JOIN_TEST_DIR/worker.inc"
fi
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-unused-but-set-variable -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$MVM_JOIN_TEST_DIR" \
    "$PROJECT_DIR/tests/mvm_sae_peer_failure_test.cpp" -o "$MVM_JOIN_TEST_DIR/test"
"$MVM_JOIN_TEST_DIR/test"
