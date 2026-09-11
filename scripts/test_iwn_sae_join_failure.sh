#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SAE_JOIN_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$SAE_JOIN_TEST_DIR/source.cpp" "$SAE_JOIN_TEST_DIR/owner.inc" "$SAE_JOIN_TEST_DIR/worker.inc" "$SAE_JOIN_TEST_DIR/test"; rm -rf "$SAE_JOIN_TEST_DIR/test.dSYM"; rmdir "$SAE_JOIN_TEST_DIR"' EXIT
if [ -n "${SAE_JOIN_NEGATIVE_REF:-}" ]; then
    git -C "$PROJECT_DIR" show "$SAE_JOIN_NEGATIVE_REF:itlwm/hal_iwn/ItlIwn.cpp" > "$SAE_JOIN_TEST_DIR/source.cpp"
else
    cp "$PROJECT_DIR/itlwm/hal_iwn/ItlIwn.cpp" "$SAE_JOIN_TEST_DIR/source.cpp"
fi
awk '/^struct iwn_sae_engine_owner / { selected=1 }
     selected { print } selected && /^};/ { exit }' \
    "$PROJECT_DIR/itlwm/hal_iwn/if_iwnvar.h" > "$SAE_JOIN_TEST_DIR/owner.inc"
awk '/^struct IwnSaeEngineCancellation / { selected=1 }
     /^iwn_sae_engine_(owner_clear_locked|cancel_owned|worker_retire|request_join_retirement|finish_join_retirement|peer_exhausted)\(/ { selected=1; print "static void" }
     /^iwn_sae_engine_(owner_matches_peer_locked|peer_owner_current_locked|take_peer_retry)\(/ { selected=1; print "static bool" }
     /^iwn_sae_engine_claim_peer_failure\(/ { selected=1; print "static u_int64_t" }
     /^iwn_sae_engine_task\(/ { selected=1; print "void ItlIwn::" }
     selected { print }
     selected && /^};?$/ { selected=0 }' \
    "$SAE_JOIN_TEST_DIR/source.cpp" > "$SAE_JOIN_TEST_DIR/worker.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$SAE_JOIN_TEST_DIR" \
    "$PROJECT_DIR/tests/iwn_sae_join_failure_test.cpp" -o "$SAE_JOIN_TEST_DIR/test"
"$SAE_JOIN_TEST_DIR/test"
