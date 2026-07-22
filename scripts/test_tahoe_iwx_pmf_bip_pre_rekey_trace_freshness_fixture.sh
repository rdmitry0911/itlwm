#!/usr/bin/env bash
# Local-only regression fixture for the IWX pre-rekey trace-freshness gate.
#
# It extracts only the runner's local shell helpers, substitutes synthetic
# trace-client records, and writes private temporary witnesses.  It never
# opens SSH, invokes the AP helper, changes radio state, or sends traffic.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_iwx_pmf_bip_runtime.sh"

fail() {
    printf 'FAIL: IWX pre-rekey freshness fixture: %s\n' "$*" >&2
    exit 1
}

[ -f "$RUNNER" ] || fail 'runner is missing'

# This first assertion is deliberately the deterministic pre-fix proof.  The
# old runner had an initial-prefix poll, traffic, and a rekey request, but no
# one-shot trace revalidation between the last invariant and the stimulus.
grep -Fq 'verify_pre_rekey_trace_freshness() {' "$RUNNER" ||
    fail 'runner lacks a one-shot post-traffic trace revalidation before bounded rekey'

extract_function() {
    local name="$1"
    awk -v marker="$name() {" '
        $0 == marker { capture = 1 }
        capture { print }
        capture && /^}$/ { exit }
    ' "$RUNNER"
}

CAPTURE_SOURCE="$(extract_function capture_live_initial_pmf_progress)"
WAIT_SOURCE="$(extract_function wait_for_initial_pmf_progress)"
VERIFY_SOURCE="$(extract_function verify_pre_rekey_trace_freshness)"
EXTRACT_TOKEN_SOURCE="$(extract_function extract_token)"
EXTRACT_U32_SOURCE="$(extract_function extract_u32)"
FILE_HAS_TOKEN_SOURCE="$(extract_function file_has_token)"
[ -n "$CAPTURE_SOURCE" ] || fail 'shared initial-progress capture helper is missing'
[ -n "$WAIT_SOURCE" ] || fail 'initial-progress waiter is missing'
[ -n "$VERIFY_SOURCE" ] || fail 'pre-rekey freshness verifier is missing'
[ -n "$EXTRACT_TOKEN_SOURCE" ] || fail 'runner token extractor is missing'
[ -n "$EXTRACT_U32_SOURCE" ] || fail 'runner integer extractor is missing'
[ -n "$FILE_HAS_TOKEN_SOURCE" ] || fail 'runner token matcher is missing'

FIXTURE_DIR="$(mktemp -d /tmp/aiam-iwx-pre-rekey-freshness.XXXXXX)"

cleanup() {
    local case_dir label
    for case_dir in "$FIXTURE_DIR"/*; do
        [ -d "$case_dir" ] || continue
        for label in \
            initial-progress-1-snapshot initial-progress-1-report \
            pre-rekey-fresh-snapshot pre-rekey-fresh-report; do
            unlink "$case_dir/out/$label.stdout" 2>/dev/null || true
            unlink "$case_dir/out/$label.stderr" 2>/dev/null || true
        done
        unlink "$case_dir/calls.log" 2>/dev/null || true
        unlink "$case_dir/sleep.log" 2>/dev/null || true
        unlink "$case_dir/rekey.witness" 2>/dev/null || true
        rmdir "$case_dir/out" "$case_dir" 2>/dev/null || true
    done
    rmdir "$FIXTURE_DIR" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

run_case() {
    local case_name="$1" expected_success="$2" case_dir="$FIXTURE_DIR/$1"
    mkdir "$case_dir"
    if ! CASE_NAME="$case_name" EXPECTED_SUCCESS="$expected_success" \
        CASE_DIR="$case_dir" CAPTURE_SOURCE="$CAPTURE_SOURCE" \
        WAIT_SOURCE="$WAIT_SOURCE" VERIFY_SOURCE="$VERIFY_SOURCE" \
        EXTRACT_TOKEN_SOURCE="$EXTRACT_TOKEN_SOURCE" \
        EXTRACT_U32_SOURCE="$EXTRACT_U32_SOURCE" \
        FILE_HAS_TOKEN_SOURCE="$FILE_HAS_TOKEN_SOURCE" \
        "$BASH" -c '
            set -euo pipefail
            eval "$EXTRACT_TOKEN_SOURCE"
            eval "$EXTRACT_U32_SOURCE"
            eval "$FILE_HAS_TOKEN_SOURCE"
            eval "$CAPTURE_SOURCE"
            eval "$WAIT_SOURCE"
            eval "$VERIFY_SOURCE"

            OUT_DIR="$CASE_DIR/out"
            CALL_LOG="$CASE_DIR/calls.log"
            SLEEP_LOG="$CASE_DIR/sleep.log"
            REKEY_WITNESS="$CASE_DIR/rekey.witness"
            mkdir "$OUT_DIR"

            write_snapshot() {
                local generation="$1" episode="$2" target_bound="$3" path="$4"
                printf "capture_generation=%s backend=IWX enabled=1 target_bound=%s active_episode=%s episode_count=1 entry_count=12 dropped=0\\n" \
                    "$generation" "$target_bound" "$episode" >"$path"
            }

            write_progress() {
                local generation="$1" episode="$2" integrity="$3" progress="$4" stage="$5" path="$6"
                printf "capture_generation=%s backend=IWX entries=12 integrity=%s episode_count=1 active_episode=%s\\n" \
                    "$generation" "$integrity" "$episode" >"$path"
                printf "pmf_bip_progress=%s first_missing_stage=%s\\n" "$progress" "$stage" >>"$path"
            }

            capture_trace_client() {
                local label="$1" command="$2" subject="$3" generation=17 episode=9 target_bound=1
                printf "%s %s %s\\n" "$label" "$command" "$subject" >>"$CALL_LOG"
                : >"$OUT_DIR/$label.stderr"
                case "$CASE_PHASE:$subject" in
                    initial:snapshot)
                        write_snapshot 17 9 1 "$OUT_DIR/$label.stdout"
                        ;;
                    initial:pmf-bip-progress)
                        write_progress 17 9 ok INITIAL_PMF_BIP_READY none "$OUT_DIR/$label.stdout"
                        ;;
                    pre-rekey:snapshot)
                        case "$CASE_NAME" in
                            changed-episode) episode=10 ;;
                            generation-mismatch) generation=18 ;;
                            target-unbound) target_bound=0 ;;
                        esac
                        write_snapshot "$generation" "$episode" "$target_bound" "$OUT_DIR/$label.stdout"
                        ;;
                    pre-rekey:pmf-bip-progress)
                        case "$CASE_NAME" in
                            changed-episode)
                                write_progress 17 10 ok INITIAL_PMF_BIP_READY none "$OUT_DIR/$label.stdout"
                                ;;
                            integrity-inconclusive)
                                write_progress 17 9 inconclusive INTEGRITY_INCONCLUSIVE cross-slot-rekey "$OUT_DIR/$label.stdout"
                                ;;
                            *)
                                write_progress 17 9 ok INITIAL_PMF_BIP_READY none "$OUT_DIR/$label.stdout"
                                ;;
                        esac
                        ;;
                    *)
                        exit 97
                        ;;
                esac
            }

            sleep() {
                printf "sleep-called\\n" >>"$SLEEP_LOG"
                return 96
            }

            CAPTURE_GENERATION=17
            INITIAL_ATTEMPTS=1
            INITIAL_PMF_PROGRESS=0
            INITIAL_PMF_EPISODE=0
            PRE_REKEY_TRACE_FRESH=0
            CASE_PHASE=initial
            wait_for_initial_pmf_progress || exit 40
            [ "$INITIAL_PMF_PROGRESS" -eq 1 ] || exit 41
            [ "$INITIAL_PMF_EPISODE" -eq 9 ] || exit 42

            CASE_PHASE=pre-rekey
            if verify_pre_rekey_trace_freshness; then
                verifier_rc=0
            else
                verifier_rc=$?
            fi
            if [ "$EXPECTED_SUCCESS" = 1 ]; then
                [ "$verifier_rc" -eq 0 ] || exit 43
                [ "$PRE_REKEY_TRACE_FRESH" -eq 1 ] || exit 44
                printf "fake-rekey-requested\\n" >"$REKEY_WITNESS"
            else
                [ "$verifier_rc" -ne 0 ] || exit 45
                [ "$PRE_REKEY_TRACE_FRESH" -eq 0 ] || exit 46
                [ ! -e "$REKEY_WITNESS" ] || exit 47
            fi
            [ "$(grep -Fc "initial-progress-1-snapshot get snapshot" "$CALL_LOG" || true)" -eq 1 ] || exit 48
            [ "$(grep -Fc "initial-progress-1-report get pmf-bip-progress" "$CALL_LOG" || true)" -eq 1 ] || exit 49
            [ "$(grep -Fc "pre-rekey-fresh-snapshot get snapshot" "$CALL_LOG" || true)" -eq 1 ] || exit 50
            [ "$(grep -Fc "pre-rekey-fresh-report get pmf-bip-progress" "$CALL_LOG" || true)" -eq 1 ] || exit 51
            [ ! -e "$SLEEP_LOG" ] || exit 52
        '; then
        fail "synthetic case failed: $case_name"
    fi
    if [ "$expected_success" = 1 ]; then
        [ -f "$case_dir/rekey.witness" ] || fail "successful case withheld fake rekey: $case_name"
    else
        [ ! -e "$case_dir/rekey.witness" ] || fail "failed case reached fake rekey: $case_name"
    fi
}

run_case same-episode 1
run_case changed-episode 0
run_case integrity-inconclusive 0
run_case generation-mismatch 0
run_case target-unbound 0

printf 'PASS: IWX pre-rekey trace-freshness local fixture\n'
