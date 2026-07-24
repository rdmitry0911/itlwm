#!/usr/bin/env bash
# Run one narrowly bounded, release-bound post-PLTI trace experiment.
#
# This is deliberately not a generic Wi-Fi helper.  It can contact only the
# pinned disposable Tahoe QEMU guest, and an admitted experiment performs
# exactly one public radio OFF/ON transition on its fixed Wi-Fi interface.
# A strict backend preflight can stop before that transition. It never asks for a
# credential, scans, joins by name, enumerates wireless identities, changes a
# route/address/DHCP state, installs/loads a kext, or reboots anything.
#
# The caller must arrange the saved profile, exact candidate activation, and
# guest-only reboot beforehand.  A read-only exact-candidate identity
# attestation is mandatory so this script cannot credit an arbitrary loaded
# kext with its categorical trace result.  Client output remains local-only;
# the emitted JSON contains safe aggregates only and is intended to be checked
# by scripts/test_tahoe_post_plti_trace_runtime_evidence_contract.sh before
# any later documentation/commit decision.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

PINNED_GUEST="devops@127.0.0.1"
PINNED_PORT=3322
PINNED_GUEST_BUILD="25C56"
PINNED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
PINNED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"
PINNED_WIFI_INTERFACE="en1"

TRACE_TOOL=""
OUT_DIR=""
IDENTITY_EVIDENCE=""
TRACE_CLIENT_SHA256=""
ARM_WHILE_RADIO_OFF=0
SOURCE_COMMIT=""
SOURCE_IDENTITY_SHA256=""
SETTLE_SECONDS=15
ACK_ATTEMPTS=20
RADIO_ATTEMPTS=30
STABLE_READ_DELAY_SECONDS=2
KNOWN_HOSTS=""

RELEASE_TAG=""
ARCHIVE_SHA256=""
BINARY_SHA256=""
MACHO_UUID=""
RESET_SEQUENCE=0
CAPTURE_GENERATION=0
TRACE_BACKEND="unknown"
TRACE_INTEGRITY="inconclusive"
TRACE_ENTRY_COUNT=0
TRACE_EPISODE_COUNT=0
TRACE_DROPPED_ENTRIES=0
TRACE_VERDICT="INTEGRITY_INCONCLUSIVE"
TRACE_FIRST_MISSING_STAGE="unknown"
RESET_ACK_SYNC=0
INITIAL_SNAPSHOT_BUFFER_SYNC=0
DOUBLE_READ_STABLE=0
TRACE_MAY_BE_ARMED=0
TRACE_SEAL_ACKNOWLEDGED=0
FINAL_CONTROL_DISABLED=0
RADIO_OFF_PENDING=0
RADIO_OFF_OBSERVED=0
RADIO_ON_OBSERVED=0
RADIO_RECOVERY_ATTEMPTED=0
TRACE_ARMED_WHILE_RADIO_OFF=0
BACKEND_PREFLIGHT_IWN=0
FAILURE_PHASE="preflight"
RUN_COMPLETE=0

declare -a SSH

usage() {
    cat >&2 <<'EOF'
usage: run_tahoe_post_plti_trace_runtime.sh \
  --trace-tool /private/tmp/aiam-post-plti-trace-CANDIDATE/airport_itlwm_post_plti_trace \
  --identity-evidence /local/path/tahoe_lab_kext_identity.json \
  --out /fresh/local/evidence/dir \
  [--trace-client-sha256 lowercase-hex-digest] \
  [--arm-while-radio-off] \
  [--settle-seconds 1..120] [--ack-attempts 1..60] \
  [--radio-attempts 1..60] [--stable-read-delay-seconds 1..10]

Preconditions, deliberately outside this runner:
  * the exact candidate has passed private AuxKC admission, transactional
    guest-only activation, and a guest-only reboot;
  * --identity-evidence is the read-only pinned-guest v2 identity capture for
    that exact archive, source commit, and source identity, and reports every
    candidate-binding check true;
  * the trace client was built for Tahoe and copied beforehand to the exact
    /private/tmp/aiam-post-plti-trace-CANDIDATE/ path supplied above;
  * the QEMU guest already has an authorized saved profile and its radio is On.

The runner fails closed if any precondition, control generation, IWN backend,
snapshot/buffer synchronization, radio transition, stable double-read, or
trace classifier result is unavailable.  It never accepts credentials or
wireless names as arguments.

`--arm-while-radio-off` is a stricter causal mode for a caller that needs the
capture reset to happen after the one radio-OFF observation and before the
only radio-ON trigger. It does not add another radio cycle. A cold trace has
no read-only backend probe, so strict mode first binds and disables a
diagnostic-only preflight capture while the radio remains On; an unsupported
backend stops before any radio transition. Its final evidence generation is
then reset only after the controlled radio-OFF observation.
EOF
}

fail_phase() {
    FAILURE_PHASE="$1"
    printf 'INCONCLUSIVE: phase=%s\n' "$FAILURE_PHASE" >&2
    exit 1
}

is_decimal_in_range() {
    local value="$1" min="$2" max="$3"
    case "$value" in ''|*[!0-9]*) return 1;; esac
    [ "$value" -ge "$min" ] && [ "$value" -le "$max" ]
}

valid_trace_tool_path() {
    [[ "$1" =~ ^/private/tmp/aiam-post-plti-trace(-[A-Za-z0-9._-]+)?/airport_itlwm_post_plti_trace$ ]]
}

valid_trace_client_sha256() {
    [[ "$1" =~ ^[0-9a-f]{64}$ ]]
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --arm-while-radio-off)
            ARM_WHILE_RADIO_OFF=1
            shift
            ;;
        --trace-tool|--identity-evidence|--out|--trace-client-sha256|--settle-seconds|--ack-attempts|--radio-attempts|--stable-read-delay-seconds)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --trace-tool) TRACE_TOOL="$2" ;;
                --identity-evidence) IDENTITY_EVIDENCE="$2" ;;
                --out) OUT_DIR="$2" ;;
                --trace-client-sha256) TRACE_CLIENT_SHA256="$2" ;;
                --settle-seconds) SETTLE_SECONDS="$2" ;;
                --ack-attempts) ACK_ATTEMPTS="$2" ;;
                --radio-attempts) RADIO_ATTEMPTS="$2" ;;
                --stable-read-delay-seconds) STABLE_READ_DELAY_SECONDS="$2" ;;
            esac
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

[ -n "$TRACE_TOOL" ] && [ -n "$IDENTITY_EVIDENCE" ] && [ -n "$OUT_DIR" ] || {
    usage
    exit 2
}
valid_trace_tool_path "$TRACE_TOOL" || {
    printf 'ERROR: --trace-tool must be the restricted private temporary client path\n' >&2
    exit 2
}
[ -z "$TRACE_CLIENT_SHA256" ] || valid_trace_client_sha256 "$TRACE_CLIENT_SHA256" || {
    printf 'ERROR: --trace-client-sha256 must be one lowercase SHA-256 digest\n' >&2
    exit 2
}
[ -f "$IDENTITY_EVIDENCE" ] || {
    printf 'ERROR: --identity-evidence is not a regular local file\n' >&2
    exit 2
}
is_decimal_in_range "$SETTLE_SECONDS" 1 120 || { usage; exit 2; }
is_decimal_in_range "$ACK_ATTEMPTS" 1 60 || { usage; exit 2; }
is_decimal_in_range "$RADIO_ATTEMPTS" 1 60 || { usage; exit 2; }
is_decimal_in_range "$STABLE_READ_DELAY_SECONDS" 1 10 || { usage; exit 2; }
[ ! -e "$OUT_DIR" ] && [ ! -L "$OUT_DIR" ] || {
    printf 'ERROR: --out must name a fresh path; refusing to overwrite evidence\n' >&2
    exit 2
}

read_identity_attestation() {
    local -a fields
    mapfile -t fields < <(python3 - "$IDENTITY_EVIDENCE" <<'PY'
import json
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
try:
    evidence = json.loads(path.read_text(encoding="utf-8"))
    if evidence.get("schema_version") != "itlwm-tahoe-lab-kext-identity-binding/v2":
        raise ValueError("identity schema")
    binding = evidence.get("candidate_binding")
    if not isinstance(binding, dict) or binding.get("candidate_kext_bound") is not True:
        raise ValueError("candidate binding")
    checks = binding.get("checks")
    if not isinstance(checks, dict) or not checks or not all(value is True for value in checks.values()):
        raise ValueError("candidate binding checks")
    verdict = evidence.get("verdict")
    if not isinstance(verdict, dict) or verdict.get(
        "ready_for_exact_candidate_runtime_experiment"
    ) is not True:
        raise ValueError("identity readiness verdict")
    release = evidence.get("expected_release")
    if not isinstance(release, dict):
        raise ValueError("expected release")
    source_commit = release.get("source_commit")
    source_identity = release.get("source_identity_sha256")
    tag = release.get("release_tag")
    archive = release.get("archive_sha256")
    binary = release.get("binary_sha256")
    uuid = release.get("macho_uuid")
    if not isinstance(source_commit, str) or re.fullmatch(r"[0-9a-f]{40}", source_commit) is None:
        raise ValueError("identity-bound source commit")
    if not isinstance(source_identity, str) or re.fullmatch(r"[0-9a-f]{64}", source_identity) is None:
        raise ValueError("identity-bound source identity")
    if not isinstance(tag, str) or re.fullmatch(r"[A-Za-z0-9._-]+", tag) is None:
        raise ValueError("release tag")
    if re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?", tag) is None:
        raise ValueError("semantic release tag")
    for value, label in ((archive, "archive digest"), (binary, "binary digest")):
        if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
            raise ValueError(label)
    if not isinstance(uuid, str) or re.fullmatch(
        r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", uuid
    ) is None:
        raise ValueError("Mach-O UUID")
except Exception as exc:
    raise SystemExit(f"identity attestation rejected: {exc}")

print(source_commit)
print(source_identity)
print(tag)
print(archive)
print(binary)
print(uuid)
PY
)
    [ "${#fields[@]}" -eq 6 ] || return 1
    SOURCE_COMMIT="${fields[0]}"
    SOURCE_IDENTITY_SHA256="${fields[1]}"
    RELEASE_TAG="${fields[2]}"
    ARCHIVE_SHA256="${fields[3]}"
    BINARY_SHA256="${fields[4]}"
    MACHO_UUID="${fields[5]}"
}

write_safe_attestation() {
    [ -n "$OUT_DIR" ] && [ -d "$OUT_DIR" ] || return 0
    python3 - "$OUT_DIR/runtime-attestation.json" \
        "$SOURCE_COMMIT" "$SOURCE_IDENTITY_SHA256" "$RELEASE_TAG" "$ARCHIVE_SHA256" "$BINARY_SHA256" "$MACHO_UUID" \
        "$RESET_SEQUENCE" "$CAPTURE_GENERATION" "$TRACE_BACKEND" "$TRACE_INTEGRITY" \
        "$TRACE_ENTRY_COUNT" "$TRACE_EPISODE_COUNT" "$TRACE_DROPPED_ENTRIES" "$TRACE_VERDICT" \
        "$TRACE_FIRST_MISSING_STAGE" "$RESET_ACK_SYNC" "$INITIAL_SNAPSHOT_BUFFER_SYNC" "$DOUBLE_READ_STABLE" \
        "$TRACE_SEAL_ACKNOWLEDGED" "$FINAL_CONTROL_DISABLED" "$RADIO_OFF_OBSERVED" "$RADIO_ON_OBSERVED" \
        "$RADIO_RECOVERY_ATTEMPTED" "$TRACE_ARMED_WHILE_RADIO_OFF" "$BACKEND_PREFLIGHT_IWN" \
        "$FAILURE_PHASE" "$RUN_COMPLETE" <<'PY'
import json
import sys
from pathlib import Path

(
    output, source_commit, source_identity_sha256, release_tag, archive_sha256, binary_sha256, macho_uuid,
    reset_sequence, capture_generation, backend, integrity, entry_count,
    episode_count, dropped_entries, verdict, first_missing_stage, reset_sync,
    initial_sync, double_stable, seal_acknowledged, final_disabled, radio_off, radio_on, recovery_attempted,
    armed_while_radio_off, backend_preflight_iwn, failure_phase, run_complete,
) = sys.argv[1:]

def as_bool(value: str) -> bool:
    return value == "1"

trace_pass = (
    backend == "iwn"
    and verdict == "KERNEL_CHAIN_OBSERVED"
    and integrity == "ok"
    and as_bool(reset_sync)
    and as_bool(initial_sync)
    and as_bool(double_stable)
    and as_bool(seal_acknowledged)
    and as_bool(final_disabled)
    and as_bool(radio_off)
    and as_bool(radio_on)
    and as_bool(run_complete)
)
document = {
    "schema": "itlwm-tahoe-post-plti-trace-runtime/v3",
    "candidate": {
        "source_commit": source_commit,
        "source_identity_sha256": source_identity_sha256,
        "release_tag": release_tag,
        "release_publication_model": "single_mutable_release_per_semantic_version",
        "archive_sha256": archive_sha256,
        "binary_sha256": binary_sha256,
        "macho_uuid": macho_uuid,
        "identity_binding_precondition": "PASS",
    },
    "scope": {
        "environment": "pinned_disposable_qemu_guest",
        "physical_host_touched": False,
        "physical_host_rebooted": False,
        "guest_rebooted_by_runner": False,
        "guest_network_identity_collected": False,
    },
    "radio_cycle": {
        "requested_cycles": 1,
        "connection_trigger": "saved_profile_autojoin_only",
        "saved_profile_precondition": "external",
        "radio_off_observed": as_bool(radio_off),
        "radio_on_observed": as_bool(radio_on),
        "radio_recovery_attempted": as_bool(recovery_attempted),
        "trace_armed_while_radio_off": as_bool(armed_while_radio_off),
        "explicit_join_command": False,
        "explicit_route_command": False,
        "explicit_address_command": False,
        "explicit_dhcp_state_mutating_command": False,
    },
    "trace": {
        "reset_control_sequence": int(reset_sequence),
        "reset_ack_generation": int(capture_generation),
        "backend_preflight_iwn": as_bool(backend_preflight_iwn),
        "backend": backend,
        "reset_ack_generation_synchronized": as_bool(reset_sync),
        "initial_snapshot_buffer_generation_synchronized": as_bool(initial_sync),
        "double_read_stable": as_bool(double_stable),
        "capture_generation": int(capture_generation),
        "integrity": integrity,
        "entry_count": int(entry_count),
        "episode_count": int(episode_count),
        "dropped_entries": int(dropped_entries),
        "verdict": verdict,
        "first_missing_stage": first_missing_stage,
        "seal_control_acknowledged": as_bool(seal_acknowledged),
        "final_control_disabled": as_bool(final_disabled),
    },
    "result": "PASS" if trace_pass else "INCONCLUSIVE",
    "failure_phase": "none" if trace_pass else failure_phase,
    "local_only_raw_artifacts": {
        "client_output_retained_local_only": True,
        "raw_output_committed": False,
    },
    "commit_safety": {
        "wireless_identity_committed": False,
        "ip_or_route_committed": False,
        "secret_material_committed": False,
        "raw_capture_committed": False,
    },
    "non_claims": [
        "pure SAE or PMF functionality",
        "generic Internet reachability",
        "physical-host validation",
        "proof beyond the categorical post-PLTI trace verdict",
    ],
}
Path(output).write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
PY
}

remote_trace() {
    "${SSH[@]}" /bin/bash -s -- "$TRACE_TOOL" "$TRACE_CLIENT_SHA256" "$@" <<'REMOTE'
set -euo pipefail
tool="$1"
expected_sha256="$2"
shift 2
case "$tool" in
    /private/tmp/aiam-post-plti-trace/airport_itlwm_post_plti_trace|/private/tmp/aiam-post-plti-trace-*/airport_itlwm_post_plti_trace) ;;
    *) exit 64 ;;
esac
test -f "$tool" && test ! -L "$tool" && test -x "$tool"
if [ -n "$expected_sha256" ]; then
    [[ "$expected_sha256" =~ ^[0-9a-f]{64}$ ]]
    parent="${tool%/airport_itlwm_post_plti_trace}"
    test -d "$parent" && test ! -L "$parent"
    physical_parent="$(CDPATH= cd -P -- "$parent" && pwd -P)"
    case "$physical_parent" in
        /private/tmp/aiam-post-plti-trace|/private/tmp/aiam-post-plti-trace-*) ;;
        *) exit 65 ;;
    esac
    [ "$physical_parent" = "$parent" ]
    observed="$(LC_ALL=C PATH=/usr/bin:/bin /usr/bin/shasum -a 256 "$tool" |
        /usr/bin/awk -v path="$tool" '
            function is_lower_hex64(value) { return length(value) == 64 && value !~ /[^0-9a-f]/ }
            NR == 1 && NF == 2 && is_lower_hex64($1) && $2 == path { value = $1; next }
            { invalid = 1 }
            END { if (NR != 1 || invalid || value == "") exit 1; print value }
        ')"
    [ "$observed" = "$expected_sha256" ]
fi
exec "$tool" "$@"
REMOTE
}

remote_trace_client_exists() {
    "${SSH[@]}" /bin/bash -s -- "$TRACE_TOOL" "$TRACE_CLIENT_SHA256" <<'REMOTE'
set -euo pipefail
tool="$1"
expected_sha256="$2"
case "$tool" in
    /private/tmp/aiam-post-plti-trace/airport_itlwm_post_plti_trace|/private/tmp/aiam-post-plti-trace-*/airport_itlwm_post_plti_trace) ;;
    *) exit 64 ;;
esac
test -f "$tool" && test ! -L "$tool" && test -x "$tool"
if [ -n "$expected_sha256" ]; then
    [[ "$expected_sha256" =~ ^[0-9a-f]{64}$ ]]
    parent="${tool%/airport_itlwm_post_plti_trace}"
    test -d "$parent" && test ! -L "$parent"
    physical_parent="$(CDPATH= cd -P -- "$parent" && pwd -P)"
    case "$physical_parent" in
        /private/tmp/aiam-post-plti-trace|/private/tmp/aiam-post-plti-trace-*) ;;
        *) exit 65 ;;
    esac
    [ "$physical_parent" = "$parent" ]
    observed="$(LC_ALL=C PATH=/usr/bin:/bin /usr/bin/shasum -a 256 "$tool" |
        /usr/bin/awk -v path="$tool" '
            function is_lower_hex64(value) { return length(value) == 64 && value !~ /[^0-9a-f]/ }
            NR == 1 && NF == 2 && is_lower_hex64($1) && $2 == path { value = $1; next }
            { invalid = 1 }
            END { if (NR != 1 || invalid || value == "") exit 1; print value }
        ')"
    [ "$observed" = "$expected_sha256" ]
fi
REMOTE
}

remote_radio_state() {
    "${SSH[@]}" /bin/bash -s <<'REMOTE'
set -euo pipefail
state="$(/usr/sbin/networksetup -getairportpower en1 2>/dev/null || true)"
case "$state" in
    *": On") printf 'on\n' ;;
    *": Off") printf 'off\n' ;;
    *) exit 1 ;;
esac
REMOTE
}

remote_radio_power() {
    local state="$1"
    case "$state" in on|off) ;; *) return 2;; esac
    "${SSH[@]}" /bin/bash -s -- "$state" <<'REMOTE'
set -euo pipefail
state="$1"
case "$state" in on|off) ;; *) exit 64;; esac
exec sudo -n /usr/sbin/networksetup -setairportpower en1 "$state"
REMOTE
}

capture_trace_client() {
    local label="$1"
    shift
    remote_trace "$@" >"$OUT_DIR/$label.stdout" 2>"$OUT_DIR/$label.stderr"
}

extract_token() {
    local path="$1" key="$2"
    tr ';' ' ' <"$path" | awk -v key="$key" '
        {
            for (i = 1; i <= NF; i++) {
                prefix = key "="
                if (index($i, prefix) == 1) {
                    value = substr($i, length(prefix) + 1)
                    count++
                }
            }
        }
        END {
            if (count == 1 && value != "") {
                print value
                exit 0
            }
            exit 1
        }
    '
}

extract_u32() {
    local value
    value="$(extract_token "$1" "$2")" || return 1
    case "$value" in ''|*[!0-9]*) return 1;; esac
    [ "$value" -le 4294967295 ] || return 1
    printf '%s\n' "$value"
}

file_has_token() {
    local path="$1" key="$2" expected="$3" observed
    observed="$(extract_token "$path" "$key")" || return 1
    [ "$observed" = "$expected" ]
}

wait_for_control_ack() {
    local label="$1" sequence="$2" expected_enable="$3" expected_reset="$4" expected_seal="$5"
    local attempt generation backend
    for attempt in $(seq 1 "$ACK_ATTEMPTS"); do
        if capture_trace_client "$label-$attempt" get control; then
            if file_has_token "$OUT_DIR/$label-$attempt.stdout" seq "$sequence" &&
                file_has_token "$OUT_DIR/$label-$attempt.stdout" applied 1 &&
                file_has_token "$OUT_DIR/$label-$attempt.stdout" enable "$expected_enable" &&
                file_has_token "$OUT_DIR/$label-$attempt.stdout" reset "$expected_reset"; then
                file_has_token "$OUT_DIR/$label-$attempt.stdout" seal "$expected_seal" || { sleep 1; continue; }
                if [ "$expected_enable" = 1 ]; then
                    file_has_token "$OUT_DIR/$label-$attempt.stdout" bound 1 || {
                        sleep 1
                        continue
                    }
                    backend="$(extract_u32 "$OUT_DIR/$label-$attempt.stdout" backend || true)"
                    generation="$(extract_u32 "$OUT_DIR/$label-$attempt.stdout" generation || true)"
                    if [ "$backend" = 1 ] && [ -n "$generation" ] && [ "$generation" -gt 0 ]; then
                        CAPTURE_GENERATION="$generation"
                        TRACE_BACKEND="iwn"
                        return 0
                    fi
                    if [ "$backend" = 2 ] && [ -n "$generation" ] && [ "$generation" -gt 0 ]; then
                        CAPTURE_GENERATION="$generation"
                        TRACE_BACKEND="unsupported"
                        TRACE_VERDICT="BACKEND_UNSUPPORTED"
                        TRACE_FIRST_MISSING_STAGE="unknown"
                        return 2
                    fi
                    if [ "$backend" = 3 ] && [ -n "$generation" ] && [ "$generation" -gt 0 ]; then
                        CAPTURE_GENERATION="$generation"
                        TRACE_BACKEND="iwx"
                        TRACE_VERDICT="BACKEND_UNSUPPORTED"
                        TRACE_FIRST_MISSING_STAGE="unknown"
                        return 2
                    fi
                else
                    return 0
                fi
            fi
        fi
        sleep 1
    done
    return 1
}

wait_for_reset_snapshot_buffer_sync() {
    local attempt snapshot trace snap_generation trace_generation
    for attempt in $(seq 1 "$ACK_ATTEMPTS"); do
        snapshot="reset-sync-$attempt-snapshot"
        trace="reset-sync-$attempt-trace"
        if capture_trace_client "$snapshot" get snapshot &&
            capture_trace_client "$trace" get trace; then
            snap_generation="$(extract_u32 "$OUT_DIR/$snapshot.stdout" capture_generation || true)"
            trace_generation="$(extract_u32 "$OUT_DIR/$trace.stdout" capture_generation || true)"
            if [ "$snap_generation" = "$CAPTURE_GENERATION" ] &&
                [ "$trace_generation" = "$CAPTURE_GENERATION" ] &&
                file_has_token "$OUT_DIR/$snapshot.stdout" backend IWN &&
                file_has_token "$OUT_DIR/$trace.stdout" backend IWN &&
                file_has_token "$OUT_DIR/$snapshot.stdout" enabled 1 &&
                file_has_token "$OUT_DIR/$snapshot.stdout" target_bound 1 &&
                file_has_token "$OUT_DIR/$snapshot.stdout" active_episode 0 &&
                file_has_token "$OUT_DIR/$snapshot.stdout" episode_count 0 &&
                file_has_token "$OUT_DIR/$snapshot.stdout" entry_count 0 &&
                file_has_token "$OUT_DIR/$snapshot.stdout" dropped 0 &&
                file_has_token "$OUT_DIR/$trace.stdout" integrity ok &&
                file_has_token "$OUT_DIR/$trace.stdout" entries 0 &&
                file_has_token "$OUT_DIR/$trace.stdout" dropped 0; then
                INITIAL_SNAPSHOT_BUFFER_SYNC=1
                return 0
            fi
        fi
        sleep 1
    done
    return 1
}

wait_for_radio_state() {
    local expected="$1" attempt observed
    for attempt in $(seq 1 "$RADIO_ATTEMPTS"); do
        observed="$(remote_radio_state 2>/dev/null || true)"
        if [ "$observed" = "$expected" ]; then
            return 0
        fi
        sleep 1
    done
    return 1
}

read_trace_once() {
    local label="$1" expected_enabled="$2" snapshot trace report
    local snapshot_generation trace_generation report_generation
    snapshot="$label-snapshot"
    trace="$label-trace"
    report="$label-report"
    capture_trace_client "$snapshot" get snapshot || return 1
    capture_trace_client "$trace" get trace || return 1
    capture_trace_client "$report" get report || return 1

    snapshot_generation="$(extract_u32 "$OUT_DIR/$snapshot.stdout" capture_generation || true)"
    trace_generation="$(extract_u32 "$OUT_DIR/$trace.stdout" capture_generation || true)"
    report_generation="$(extract_u32 "$OUT_DIR/$report.stdout" capture_generation || true)"
    [ "$snapshot_generation" = "$CAPTURE_GENERATION" ] &&
        [ "$trace_generation" = "$CAPTURE_GENERATION" ] &&
        [ "$report_generation" = "$CAPTURE_GENERATION" ] || return 1
    file_has_token "$OUT_DIR/$snapshot.stdout" backend IWN || return 1
    file_has_token "$OUT_DIR/$trace.stdout" backend IWN || return 1
    file_has_token "$OUT_DIR/$report.stdout" backend IWN || return 1
    file_has_token "$OUT_DIR/$snapshot.stdout" enabled "$expected_enabled" || return 1
    file_has_token "$OUT_DIR/$snapshot.stdout" target_bound 1 || return 1
    file_has_token "$OUT_DIR/$trace.stdout" integrity ok || return 1
    file_has_token "$OUT_DIR/$report.stdout" integrity ok || return 1

    TRACE_ENTRY_COUNT="$(extract_u32 "$OUT_DIR/$report.stdout" entries || true)"
    TRACE_EPISODE_COUNT="$(extract_u32 "$OUT_DIR/$report.stdout" episode_count || true)"
    TRACE_DROPPED_ENTRIES="$(extract_u32 "$OUT_DIR/$snapshot.stdout" dropped || true)"
    TRACE_VERDICT="$(extract_token "$OUT_DIR/$report.stdout" verdict || true)"
    TRACE_FIRST_MISSING_STAGE="$(extract_token "$OUT_DIR/$report.stdout" first_missing_stage || true)"
    case "$TRACE_VERDICT" in
        KERNEL_CHAIN_OBSERVED|BRANCH_NOT_OBSERVED|RESUME_NO_STATE_REQUEST|RESUME_NO_IWN_DISPATCH|SCAN_COMMAND_REJECTED|SCAN_INCOMPLETE|SCAN_NO_CANDIDATE|RESUME_NO_SELECTION|AUTH_NOT_DRAINED|TX_NO_COMPLETION|NO_EAPOL|BACKEND_UNSUPPORTED|INTEGRITY_INCONCLUSIVE) ;;
        *) return 1 ;;
    esac
    case "$TRACE_FIRST_MISSING_STAGE" in
        none|state-scan-self-request|iwn-scan-state|iwn-scan-command|scan-completion|bss-selection|join-bss|auth-state|auth-enqueue|auth-dequeue|auth-firmware-submit|auth-exchange|assoc-state|assoc-enqueue|assoc-dequeue|assoc-firmware-submit|assoc-exchange|run-state|eapol-decapped|eapol-kernel-pae|eapol-enqueue|port-valid|unknown) ;;
        *) return 1 ;;
    esac
    [ -n "$TRACE_ENTRY_COUNT" ] && [ -n "$TRACE_EPISODE_COUNT" ] &&
        [ -n "$TRACE_DROPPED_ENTRIES" ] || return 1
    [ "$TRACE_DROPPED_ENTRIES" = 0 ] || return 1
    TRACE_INTEGRITY="ok"
    return 0
}

disable_trace() {
    local label="${1:-final-off}" off_sequence
    if ! capture_trace_client "$label" off; then
        return 1
    fi
    off_sequence="$(extract_u32 "$OUT_DIR/$label.stdout" seq || true)"
    [ -n "$off_sequence" ] || return 1
    if wait_for_control_ack "$label-ack" "$off_sequence" 0 0 0; then
        FINAL_CONTROL_DISABLED=1
        TRACE_MAY_BE_ARMED=0
        return 0
    fi
    return 1
}

seal_trace() {
    local seal_sequence
    if ! capture_trace_client seal seal; then
        return 1
    fi
    seal_sequence="$(extract_u32 "$OUT_DIR/seal.stdout" seq || true)"
    [ -n "$seal_sequence" ] || return 1
    if wait_for_control_ack seal-ack "$seal_sequence" 0 0 1; then
        TRACE_MAY_BE_ARMED=0
        TRACE_SEAL_ACKNOWLEDGED=1
        return 0
    fi
    return 1
}

observe_trace_before_seal() {
    local attempt
    for attempt in $(seq 1 5); do
        capture_trace_client "pre-seal-$attempt" get report || return 1
        file_has_token "$OUT_DIR/pre-seal-$attempt.stdout" integrity ok || return 1
        case "$(extract_token "$OUT_DIR/pre-seal-$attempt.stdout" verdict || true)" in
            KERNEL_CHAIN_OBSERVED) return 0 ;;
        esac
        sleep 1
    done
    return 0
}

cleanup() {
    local rc=$?
    trap - EXIT HUP INT TERM
    set +e
    # Quiesce a possibly armed capture before restoring the radio: otherwise a
    # saved profile could add post-failure events during recovery.
    if [ "$TRACE_MAY_BE_ARMED" -eq 1 ]; then
        disable_trace >/dev/null 2>&1
    fi
    if [ "$RADIO_OFF_PENDING" -eq 1 ]; then
        RADIO_RECOVERY_ATTEMPTED=1
        remote_radio_power on >/dev/null 2>&1
        if wait_for_radio_state on; then
            RADIO_ON_OBSERVED=1
            RADIO_OFF_PENDING=0
        fi
    fi
    write_safe_attestation
    [ -z "$KNOWN_HOSTS" ] || rm -f "$KNOWN_HOSTS"
    exit "$rc"
}

trap cleanup EXIT
trap 'exit 130' HUP INT
trap 'exit 143' TERM

read_identity_attestation || fail_phase identity-attestation
umask 077
mkdir -p "$OUT_DIR"
chmod 700 "$OUT_DIR"

KNOWN_HOSTS="$(mktemp /tmp/aiam-post-plti-trace-known-hosts.XXXXXX)"
chmod 600 "$KNOWN_HOSTS"
printf '%s\n' "$PINNED_GUEST_HOSTKEY_LINE" >"$KNOWN_HOSTS"
observed_fingerprint="$(ssh-keygen -lf "$KNOWN_HOSTS" -E sha256 2>/dev/null | awk 'NR == 1 { print $2; exit }')"
[ "$observed_fingerprint" = "$PINNED_GUEST_HOSTKEY_SHA256" ] || fail_phase hostkey-pin
SSH=(
    ssh -F /dev/null -p "$PINNED_PORT" -o BatchMode=yes -o ConnectTimeout=8
    -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$KNOWN_HOSTS"
    -o GlobalKnownHostsFile=/dev/null -o UpdateHostKeys=no -o LogLevel=ERROR
    "$PINNED_GUEST"
)
guest_build="$("${SSH[@]}" 'sw_vers -buildVersion' 2>/dev/null || true)"
[ "$guest_build" = "$PINNED_GUEST_BUILD" ] || fail_phase guest-build-pin
remote_trace_client_exists || fail_phase trace-client-preflight

# A normal generic trace preserves its historical arm-then-radio ordering.
# The IWN-only PMF wrapper selects the stricter branch: it first binds and
# immediately disables a diagnostic-only preflight capture while the radio is
# still on. That is the only existing way to discover a cold backend without
# an association or radio transition. An IWX/unsupported result stops there.
# Only a confirmed IWN then gets one OFF/ON, followed by the second reset
# while Off; that second generation is the sole experiment evidence.
wait_for_radio_state on || fail_phase radio-precondition-on
if [ "$ARM_WHILE_RADIO_OFF" -eq 1 ]; then
    TRACE_MAY_BE_ARMED=1
    if ! capture_trace_client preflight-reset reset; then
        fail_phase trace-preflight-reset-request
    fi
    preflight_sequence="$(extract_u32 "$OUT_DIR/preflight-reset.stdout" seq || true)"
    [ "$preflight_sequence" -gt 0 ] 2>/dev/null ||
        fail_phase trace-preflight-reset-sequence
    if wait_for_control_ack preflight-reset-ack "$preflight_sequence" 1 1 0; then
        BACKEND_PREFLIGHT_IWN=1
    else
        case "$TRACE_BACKEND" in
            iwx) fail_phase trace-backend-iwx-ordered-unsupported ;;
            unsupported) fail_phase trace-backend-unsupported ;;
            *) fail_phase trace-preflight-reset-ack ;;
        esac
    fi
    disable_trace preflight-off || fail_phase trace-preflight-final-off
    # The preflight is not the experiment's final control lifecycle.
    FINAL_CONTROL_DISABLED=0
    wait_for_radio_state on || fail_phase radio-precondition-on
    RADIO_OFF_PENDING=1
    remote_radio_power off >/dev/null 2>&1 || fail_phase radio-off-request
    wait_for_radio_state off || fail_phase radio-off-observation
    RADIO_OFF_OBSERVED=1
    wait_for_radio_state off || fail_phase radio-off-before-final-reset
fi

TRACE_MAY_BE_ARMED=1
if ! capture_trace_client reset reset; then
    fail_phase trace-reset-request
fi
RESET_SEQUENCE="$(extract_u32 "$OUT_DIR/reset.stdout" seq || true)"
[ "$RESET_SEQUENCE" -gt 0 ] 2>/dev/null || fail_phase trace-reset-sequence
if wait_for_control_ack reset-ack "$RESET_SEQUENCE" 1 1 0; then
    :
else
    case "$TRACE_BACKEND" in
        iwx) fail_phase trace-backend-iwx-ordered-unsupported ;;
        unsupported) fail_phase trace-backend-unsupported ;;
        *) fail_phase trace-reset-ack ;;
    esac
fi
RESET_ACK_SYNC=1
if [ "$ARM_WHILE_RADIO_OFF" -eq 1 ]; then
    wait_for_radio_state off || fail_phase radio-off-after-final-reset-ack
fi
wait_for_reset_snapshot_buffer_sync || fail_phase trace-reset-snapshot-buffer-sync
if [ "$ARM_WHILE_RADIO_OFF" -eq 1 ]; then
    wait_for_radio_state off || fail_phase radio-off-after-final-reset-sync
    TRACE_ARMED_WHILE_RADIO_OFF=1
fi

if [ "$ARM_WHILE_RADIO_OFF" -eq 0 ]; then
    wait_for_radio_state on || fail_phase radio-precondition-on
    RADIO_OFF_PENDING=1
    remote_radio_power off >/dev/null 2>&1 || fail_phase radio-off-request
    wait_for_radio_state off || fail_phase radio-off-observation
    RADIO_OFF_OBSERVED=1
fi
remote_radio_power on >/dev/null 2>&1 || fail_phase radio-on-request
wait_for_radio_state on || fail_phase radio-on-observation
RADIO_ON_OBSERVED=1
RADIO_OFF_PENDING=0

sleep "$SETTLE_SECONDS"
observe_trace_before_seal || fail_phase trace-pre-seal-observation
seal_trace || fail_phase trace-seal
read_trace_once read-1 0 || fail_phase trace-first-read
sleep "$STABLE_READ_DELAY_SECONDS"
read_trace_once read-2 0 || fail_phase trace-second-read
cmp -s "$OUT_DIR/read-1-snapshot.stdout" "$OUT_DIR/read-2-snapshot.stdout" &&
    cmp -s "$OUT_DIR/read-1-trace.stdout" "$OUT_DIR/read-2-trace.stdout" &&
    cmp -s "$OUT_DIR/read-1-report.stdout" "$OUT_DIR/read-2-report.stdout" ||
    fail_phase trace-double-read-unstable
DOUBLE_READ_STABLE=1

disable_trace || fail_phase trace-final-off
RUN_COMPLETE=1
if [ "$TRACE_VERDICT" = KERNEL_CHAIN_OBSERVED ]; then
    [ "$TRACE_EPISODE_COUNT" = 1 ] && [ "$TRACE_ENTRY_COUNT" -gt 0 ] &&
        [ "$RESET_SEQUENCE" -gt 0 ] && [ "$CAPTURE_GENERATION" -gt 0 ] ||
        fail_phase trace-success-invariants
    FAILURE_PHASE="none"
    printf 'PASS: post-PLTI categorical trace complete; safe aggregate is local-only\n'
    exit 0
fi
FAILURE_PHASE="trace-verdict-diagnostic"
printf 'INCONCLUSIVE: sealed post-PLTI categorical trace retained as local-only diagnostic evidence\n'
exit 0
