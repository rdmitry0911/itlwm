#!/usr/bin/env bash
# Required parity gate, intentionally red until the real MVM cleanup owner
# is wired. Not included in the passing aggregate while that work is open.
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
awk -v family="$family" -v mixed="$mixed" '
    $0 ~ "^struct " mixed "SaeEngineCancellation " { selected=1 }
    $0 ~ "^" family "_sae_engine_(owner_clear_locked|cancel_owned|worker_retire)\\(" { selected=1; print "static void" }
    $0 ~ "^" family "_sae_engine_(owner_matches_peer_locked|peer_owner_current_locked)\\(" { selected=1; print "static bool" }
    $0 ~ "^" family "_sae_engine_task\\(" { selected=1; print "void Itl" mixed "::" }
    selected { print }
    selected && /^};?$/ { selected=0 }' \
    "$PROJECT_DIR/itlwm/hal_$family/${mixed}SaeEngine.inc" | normalize > "$MVM_JOIN_TEST_DIR/worker.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-unused-but-set-variable -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$MVM_JOIN_TEST_DIR" \
    "$PROJECT_DIR/tests/mvm_sae_peer_failure_test.cpp" -o "$MVM_JOIN_TEST_DIR/test"
"$MVM_JOIN_TEST_DIR/test"
