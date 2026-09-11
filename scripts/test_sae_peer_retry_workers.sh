#!/usr/bin/env bash
# Execute each complete HAL worker plus actual RX/TX identity and timer claims.
set -euo pipefail
ulimit -c 0
task_root=$(cd "$(dirname "$0")/.." && pwd)
task_out=$(mktemp -d "${TMPDIR:-/tmp}/itlwm-sae-peer-workers.XXXXXX")
for family in iwn iwm iwx; do
  case "$family" in
    iwn) mixed=Iwn; upper=IWN; source=ItlIwn.cpp ;;
    iwm) mixed=Iwm; upper=IWM; source=IwmSaeEngine.inc ;;
    iwx) mixed=Iwx; upper=IWX; source=IwxSaeEngine.inc ;;
  esac
  mkdir "$task_out/$family"
  normalize() { sed -e "s/$family/iwn/g" -e "s/$mixed/Iwn/g" -e "s/$upper/IWN/g"; }
  awk -v name="${family}_sae_engine_owner" '
    $0 ~ "^struct " name " " { selected=1 }
    selected { print } selected && /^};/ { exit }' \
    "$task_root/itlwm/hal_$family/if_${family}var.h" | normalize > "$task_out/$family/owner.inc"
  awk -v family="$family" -v mixed="$mixed" '
    $0 ~ "^struct " mixed "SaeEngineCancellation " { selected=1 }
    $0 ~ "^" family "_sae_engine_(owner_clear_locked|cancel_owned|worker_retire|request_join_retirement|finish_join_retirement|peer_exhausted)\\(" { selected=1; print "static void" }
    $0 ~ "^" family "_sae_engine_(owner_matches_peer_locked|peer_owner_current_locked|owner_matches_terminal_locked|queue_terminal|take_peer_retry)\\(" { selected=1; print "static bool" }
    $0 ~ "^" family "_sae_engine_claim_peer_failure\\(" { selected=1; print "static u_int64_t" }
    $0 ~ "^" family "_sae_engine_task\\(" { selected=1; print "void Itl" mixed "::" }
    $0 ~ "^void Itl" mixed "::" family "_sae_peer_timer_drain\\(" { selected=1 }
    selected { print }
    selected && /^};?$/ { selected=0 }' \
    "$task_root/itlwm/hal_$family/$source" | normalize > "$task_out/$family/worker.inc"
  "${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-unused-but-set-variable -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$task_root" -I"$task_root/include" -I"$task_out/$family" \
    "$task_root/tests/sae_peer_retry_worker_test.cpp" -o "$task_out/$family/test"
  printf '%s: ' "$upper"
  "$task_out/$family/test"
done
printf 'SAE worker test artifacts: %s\n' "$task_out"
