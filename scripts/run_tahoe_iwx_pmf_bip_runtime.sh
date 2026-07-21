#!/usr/bin/env bash
# Bounded credential-safe runtime gate for the IWX PMF/BIP ownership layer.
#
# This does not activate a kext, reboot a guest, accept a password, or run an
# arbitrary join command.  It requires an already activated exact candidate,
# a saved profile, and a fresh disposable QEMU guest.  The only host mutation
# is delegated to the separately bounded hostapd switchover helper, which
# restores the optional-PMF AP before this runner can report a result.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
IDENTITY_CAPTURE="$ROOT/scripts/capture_tahoe_lab_kext_identity.py"
AP_HELPER="$ROOT/scripts/tahoe_pmf_required_ap_switchover.sh"

PINNED_GUEST="devops@127.0.0.1"
PINNED_PORT=3322
PINNED_GUEST_BUILD="25C56"
PINNED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
PINNED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"
PINNED_WIFI_INTERFACE="en1"
PINNED_MANAGEMENT_INTERFACE="en0"
PINNED_PROFILE_SSID="AIAMlab6235"
PINNED_LAB_GATEWAY="10.77.0.1"
PINNED_LAB_PREFIX="10.77.0."

TRACE_TOOL=""
RELEASE_ZIP=""
CANDIDATE_PROVENANCE=""
OUT_DIR=""
INITIAL_ATTEMPTS=45
ASSOCIATION_ATTEMPTS=45
REKEY_SETTLE_SECONDS=20
STABLE_READ_DELAY_SECONDS=2
ACK_ATTEMPTS=20
RADIO_ATTEMPTS=30
AP_LEASE_SECONDS=300

KNOWN_HOSTS=""
AP_STATE_DIR=""
declare -a SSH

SOURCE_COMMIT=""
SOURCE_IDENTITY_SHA256=""
RELEASE_TAG=""
ARCHIVE_SHA256=""
BINARY_SHA256=""
MACHO_UUID=""

IDENTITY_BEFORE_BOUND=0
IDENTITY_AFTER_BOUND=0
SAVED_PROFILE_READY=0
AP_PREFLIGHT_PASSED=0
AP_REQUIRED_ACTIVE=0
AP_REQUIRED_WAS_ACTIVE=0
AP_ROLLBACK_VERIFIED=0
AP_ROLLBACK_ATTEMPTED=0
INITIAL_PMF_PROGRESS=0
TRAFFIC_SUCCESS=0
REKEY_REQUESTED=0
CROSS_SLOT_REKEY_OBSERVED=0
NETWORK_INVARIANTS_OK=0
RESET_ACK_SYNC=0
INITIAL_SNAPSHOT_BUFFER_SYNC=0
TRACE_SEAL_ACKNOWLEDGED=0
FINAL_CONTROL_DISABLED=0
DOUBLE_READ_STABLE=0
RADIO_OFF_PENDING=0
RADIO_OFF_OBSERVED=0
RADIO_ON_OBSERVED=0
RADIO_RECOVERY_ATTEMPTED=0
TRACE_MAY_BE_ARMED=0
RUN_COMPLETE=0
RESET_SEQUENCE=0
CAPTURE_GENERATION=0
TRACE_BACKEND="unknown"
TRACE_INTEGRITY="inconclusive"
TRACE_ENTRY_COUNT=0
TRACE_EPISODE_COUNT=0
TRACE_DROPPED_ENTRIES=0
TRACE_VERDICT="INTEGRITY_INCONCLUSIVE"
TRACE_FIRST_MISSING_STAGE="unknown"
FAILURE_PHASE="preflight"

DEFAULT_ROUTE_BASELINE=""
MANAGEMENT_IPV4_BASELINE=""
LAB_IPV4_BASELINE=""
LAB_ROUTE_BASELINE=""
ACTIVE_CLIENT_MAC=""

usage() {
    cat >&2 <<'EOF'
usage: run_tahoe_iwx_pmf_bip_runtime.sh \
  --trace-tool /private/tmp/aiam-post-plti-trace-CANDIDATE/airport_itlwm_post_plti_trace \
  --release-zip /local/safe/AirportItlwm-Tahoe.kext.zip \
  --candidate-provenance /local/safe/tahoe_candidate_provenance.json \
  --out /fresh/local/evidence/dir \
  [--initial-attempts 1..60] [--association-attempts 1..60] \
  [--rekey-settle-seconds 1..60] [--stable-read-delay-seconds 1..10]

Preconditions outside this runner:
  * the exact clean candidate passed private AuxKC admission, transactional
    activation, and a guest-only reboot on a fresh disposable overlay;
  * the Tahoe trace client has been copied to the restricted private path;
  * the guest has a pre-existing Keychain/saved profile for the pinned lab AP.

The runner captures exact loaded-candidate identity before and after the test,
arms only the safe IWX trace, uses saved-profile autojoin after one radio
OFF/ON, requires an initial PMF/BIP progress classifier before it requests one
bounded hostapd group rekey, then requires a sealed cross-slot rekey verdict
and bounded local traffic.  It does not accept a password or wireless name.
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

while [ "$#" -gt 0 ]; do
    case "$1" in
        --trace-tool|--release-zip|--candidate-provenance|--out|--initial-attempts|--association-attempts|--rekey-settle-seconds|--stable-read-delay-seconds|--ack-attempts|--radio-attempts|--ap-lease-seconds)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --trace-tool) TRACE_TOOL="$2" ;;
                --release-zip) RELEASE_ZIP="$2" ;;
                --candidate-provenance) CANDIDATE_PROVENANCE="$2" ;;
                --out) OUT_DIR="$2" ;;
                --initial-attempts) INITIAL_ATTEMPTS="$2" ;;
                --association-attempts) ASSOCIATION_ATTEMPTS="$2" ;;
                --rekey-settle-seconds) REKEY_SETTLE_SECONDS="$2" ;;
                --stable-read-delay-seconds) STABLE_READ_DELAY_SECONDS="$2" ;;
                --ack-attempts) ACK_ATTEMPTS="$2" ;;
                --radio-attempts) RADIO_ATTEMPTS="$2" ;;
                --ap-lease-seconds) AP_LEASE_SECONDS="$2" ;;
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

[ -n "$TRACE_TOOL" ] && [ -n "$RELEASE_ZIP" ] && \
    [ -n "$CANDIDATE_PROVENANCE" ] && [ -n "$OUT_DIR" ] || {
    usage
    exit 2
}
valid_trace_tool_path "$TRACE_TOOL" || {
    printf 'ERROR: --trace-tool must use the restricted guest-local private path\n' >&2
    exit 2
}
[ -f "$RELEASE_ZIP" ] && [ ! -L "$RELEASE_ZIP" ] || {
    printf 'ERROR: --release-zip must be a regular local file\n' >&2
    exit 2
}
[ -f "$CANDIDATE_PROVENANCE" ] && [ ! -L "$CANDIDATE_PROVENANCE" ] || {
    printf 'ERROR: --candidate-provenance must be a regular local file\n' >&2
    exit 2
}
[ -x "$IDENTITY_CAPTURE" ] && [ -x "$AP_HELPER" ] || {
    printf 'ERROR: required identity/AP helper is unavailable\n' >&2
    exit 2
}
for value_range in \
    "$INITIAL_ATTEMPTS:1:60" "$ASSOCIATION_ATTEMPTS:1:60" \
    "$REKEY_SETTLE_SECONDS:1:60" "$STABLE_READ_DELAY_SECONDS:1:10" \
    "$ACK_ATTEMPTS:1:60" "$RADIO_ATTEMPTS:1:60" "$AP_LEASE_SECONDS:60:300"; do
    IFS=: read -r value min max <<<"$value_range"
    is_decimal_in_range "$value" "$min" "$max" || { usage; exit 2; }
done
[ ! -e "$OUT_DIR" ] && [ ! -L "$OUT_DIR" ] || {
    printf 'ERROR: --out must name a fresh path; refusing to overwrite evidence\n' >&2
    exit 2
}

remote_trace() {
    "${SSH[@]}" /bin/bash -s -- "$TRACE_TOOL" "$@" <<'REMOTE'
set -euo pipefail
tool="$1"
shift
case "$tool" in
    /private/tmp/aiam-post-plti-trace/airport_itlwm_post_plti_trace|/private/tmp/aiam-post-plti-trace-*/airport_itlwm_post_plti_trace) ;;
    *) exit 64 ;;
esac
test -x "$tool"
exec "$tool" "$@"
REMOTE
}

remote_trace_client_exists() {
    "${SSH[@]}" /bin/bash -s -- "$TRACE_TOOL" <<'REMOTE'
set -euo pipefail
tool="$1"
case "$tool" in
    /private/tmp/aiam-post-plti-trace/airport_itlwm_post_plti_trace|/private/tmp/aiam-post-plti-trace-*/airport_itlwm_post_plti_trace) ;;
    *) exit 64 ;;
esac
test -x "$tool"
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
        END { if (count == 1 && value != "") print value; else exit 1 }
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

capture_identity() {
    local label="$1" values
    python3 "$IDENTITY_CAPTURE" --expected-release-zip "$RELEASE_ZIP" \
        --candidate-provenance "$CANDIDATE_PROVENANCE" \
        --output "$OUT_DIR/identity-$label.json" \
        >"$OUT_DIR/identity-$label.stdout" 2>"$OUT_DIR/identity-$label.stderr" || return 1
    values="$(python3 - "$OUT_DIR/identity-$label.json" <<'PY'
import json
import re
import sys
from pathlib import Path

data = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if data.get("schema_version") != "itlwm-tahoe-lab-kext-identity-binding/v2":
    raise SystemExit(1)
binding = data.get("candidate_binding")
verdict = data.get("verdict")
release = data.get("expected_release")
if not isinstance(binding, dict) or binding.get("candidate_kext_bound") is not True:
    raise SystemExit(1)
checks = binding.get("checks")
if not isinstance(checks, dict) or not checks or not all(v is True for v in checks.values()):
    raise SystemExit(1)
if not isinstance(verdict, dict) or verdict.get("ready_for_exact_candidate_runtime_experiment") is not True:
    raise SystemExit(1)
if not isinstance(release, dict):
    raise SystemExit(1)
fields = ("source_commit", "source_identity_sha256", "release_tag", "archive_sha256", "binary_sha256", "macho_uuid")
for key in fields:
    value = release.get(key)
    if not isinstance(value, str) or not value:
        raise SystemExit(1)
if re.fullmatch(r"[0-9a-f]{40}", release["source_commit"]) is None:
    raise SystemExit(1)
if re.fullmatch(r"[0-9a-f]{64}", release["source_identity_sha256"]) is None:
    raise SystemExit(1)
if re.fullmatch(r"[0-9a-f]{64}", release["archive_sha256"]) is None:
    raise SystemExit(1)
if re.fullmatch(r"[0-9a-f]{64}", release["binary_sha256"]) is None:
    raise SystemExit(1)
if re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", release["macho_uuid"]) is None:
    raise SystemExit(1)
print("\n".join(release[key] for key in fields))
PY
)" || return 1
    mapfile -t identity_values <<<"$values"
    [ "${#identity_values[@]}" -eq 6 ] || return 1
    if [ "$label" = before ]; then
        SOURCE_COMMIT="${identity_values[0]}"
        SOURCE_IDENTITY_SHA256="${identity_values[1]}"
        RELEASE_TAG="${identity_values[2]}"
        ARCHIVE_SHA256="${identity_values[3]}"
        BINARY_SHA256="${identity_values[4]}"
        MACHO_UUID="${identity_values[5]}"
        IDENTITY_BEFORE_BOUND=1
    else
        [ "${identity_values[0]}" = "$SOURCE_COMMIT" ] &&
            [ "${identity_values[1]}" = "$SOURCE_IDENTITY_SHA256" ] &&
            [ "${identity_values[2]}" = "$RELEASE_TAG" ] &&
            [ "${identity_values[3]}" = "$ARCHIVE_SHA256" ] &&
            [ "${identity_values[4]}" = "$BINARY_SHA256" ] &&
            [ "${identity_values[5]}" = "$MACHO_UUID" ] || return 1
        IDENTITY_AFTER_BOUND=1
    fi
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

wait_for_radio_state() {
    local expected="$1" attempt observed
    for attempt in $(seq 1 "$RADIO_ATTEMPTS"); do
        observed="$(remote_radio_state 2>/dev/null || true)"
        [ "$observed" = "$expected" ] && return 0
        sleep 1
    done
    return 1
}

guest_default_route_signature() {
    "${SSH[@]}" "route -n get default 2>/dev/null | awk '/gateway:/{gateway=\$2} /interface:/{iface=\$2} END { if (iface != \"\") printf \"gateway=%s interface=%s\\n\", gateway, iface; else exit 1 }'"
}

guest_management_ipv4() {
    "${SSH[@]}" "ifconfig $PINNED_MANAGEMENT_INTERFACE 2>/dev/null | awk '\$1 == \"inet\" { print \$2; exit }'"
}

guest_lab_ipv4() {
    "${SSH[@]}" "ifconfig $PINNED_WIFI_INTERFACE 2>/dev/null | awk '\$1 == \"inet\" && \$2 ~ /^10\\.77\\.0\\./ { print \$2; exit }'"
}

guest_lab_route_signature() {
    "${SSH[@]}" "route -n get $PINNED_LAB_GATEWAY 2>/dev/null | awk -v expected='$PINNED_LAB_GATEWAY' '
        /^[[:space:]]*destination:/ { destination=\$2 }
        /^[[:space:]]*gateway:/ { gateway=\$2 }
        /^[[:space:]]*interface:/ { iface=\$2 }
        END { if (destination != expected || iface == \"\") exit 1; if (gateway == \"\") gateway = \"direct\"; printf \"destination=%s nexthop=%s interface=%s\\n\", destination, gateway, iface }
    '"
}

guest_saved_profile_ready() {
    "${SSH[@]}" "networksetup -listpreferredwirelessnetworks $PINNED_WIFI_INTERFACE 2>/dev/null | awk 'NR > 1 { line=\$0; sub(/^[[:space:]]+/, \"\", line); sub(/[[:space:]]+$/, \"\", line); if (line == \"$PINNED_PROFILE_SSID\") found=1 } END { exit !found }'"
}

guest_wifi_mac() {
    "${SSH[@]}" "ifconfig $PINNED_WIFI_INTERFACE 2>/dev/null | awk '\$1 == \"ether\" { print \$2; exit }'"
}

station_authorized() {
    local mac="$1"
    [ -n "$mac" ] || return 1
    sudo -n /usr/sbin/iw dev wlp0s20f3 station dump 2>/dev/null | awk -v wanted="$mac" '
        tolower($1) == "station" { active = (tolower($2) == tolower(wanted)); next }
        active && $1 == "authorized:" && $2 == "yes" { authorized = 1 }
        END { exit !authorized }
    '
}

wait_authorized_stable() {
    local attempt mac streak=0
    for attempt in $(seq 1 "$ASSOCIATION_ATTEMPTS"); do
        mac="$(guest_wifi_mac 2>/dev/null || true)"
        if [[ "$mac" =~ ^([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}$ ]] &&
            station_authorized "$mac"; then
            ACTIVE_CLIENT_MAC="$mac"
            streak=$((streak + 1))
        else
            streak=0
        fi
        [ "$streak" -ge 3 ] && return 0
        sleep 1
    done
    return 1
}

capture_local_network_state() {
    local phase="$1"
    {
        printf '%s\n' '__DEFAULT_ROUTE_BEGIN__'
        "${SSH[@]}" 'route -n get default 2>&1 || true'
        printf '%s\n' '__LAB_ROUTE_BEGIN__'
        "${SSH[@]}" "route -n get $PINNED_LAB_GATEWAY 2>&1 || true"
        printf '%s\n' '__MANAGEMENT_IF_BEGIN__'
        "${SSH[@]}" "ifconfig $PINNED_MANAGEMENT_INTERFACE 2>&1 || true"
        printf '%s\n' '__WIFI_IF_BEGIN__'
        "${SSH[@]}" "ifconfig $PINNED_WIFI_INTERFACE 2>&1 || true"
    } >"$OUT_DIR/network-$phase.txt"
}

assert_network_invariants() {
    local phase="$1" default_route management_ipv4 lab_ipv4 lab_route
    default_route="$(guest_default_route_signature 2>/dev/null || true)"
    management_ipv4="$(guest_management_ipv4 2>/dev/null || true)"
    lab_ipv4="$(guest_lab_ipv4 2>/dev/null || true)"
    lab_route="$(guest_lab_route_signature 2>/dev/null || true)"
    if [ "$default_route" != "$DEFAULT_ROUTE_BASELINE" ] ||
       [ "$management_ipv4" != "$MANAGEMENT_IPV4_BASELINE" ] ||
       [ "$lab_ipv4" != "$LAB_IPV4_BASELINE" ] ||
       [ "$lab_route" != "$LAB_ROUTE_BASELINE" ]; then
        NETWORK_INVARIANTS_OK=0
        capture_local_network_state "$phase-failed"
        return 1
    fi
    capture_local_network_state "$phase"
    NETWORK_INVARIANTS_OK=1
}

wait_for_control_ack() {
    local label="$1" sequence="$2" expected_enable="$3" expected_reset="$4" expected_seal="$5"
    local attempt backend generation
    for attempt in $(seq 1 "$ACK_ATTEMPTS"); do
        if capture_trace_client "$label-$attempt" get control &&
            file_has_token "$OUT_DIR/$label-$attempt.stdout" seq "$sequence" &&
            file_has_token "$OUT_DIR/$label-$attempt.stdout" applied 1 &&
            file_has_token "$OUT_DIR/$label-$attempt.stdout" enable "$expected_enable" &&
            file_has_token "$OUT_DIR/$label-$attempt.stdout" reset "$expected_reset" &&
            file_has_token "$OUT_DIR/$label-$attempt.stdout" seal "$expected_seal"; then
            if [ "$expected_enable" = 1 ]; then
                file_has_token "$OUT_DIR/$label-$attempt.stdout" bound 1 || { sleep 1; continue; }
                backend="$(extract_u32 "$OUT_DIR/$label-$attempt.stdout" backend || true)"
                generation="$(extract_u32 "$OUT_DIR/$label-$attempt.stdout" generation || true)"
                if [ "$backend" = 3 ] && [ -n "$generation" ] && [ "$generation" -gt 0 ]; then
                    CAPTURE_GENERATION="$generation"
                    TRACE_BACKEND=iwx
                    return 0
                fi
                if [ -n "$generation" ] && [ "$generation" -gt 0 ]; then
                    TRACE_BACKEND=unsupported
                    return 2
                fi
            else
                return 0
            fi
        fi
        sleep 1
    done
    return 1
}

wait_for_reset_snapshot_buffer_sync() {
    local attempt snapshot trace snapshot_generation trace_generation
    for attempt in $(seq 1 "$ACK_ATTEMPTS"); do
        snapshot="reset-sync-$attempt-snapshot"
        trace="reset-sync-$attempt-trace"
        if capture_trace_client "$snapshot" get snapshot &&
            capture_trace_client "$trace" get trace; then
            snapshot_generation="$(extract_u32 "$OUT_DIR/$snapshot.stdout" capture_generation || true)"
            trace_generation="$(extract_u32 "$OUT_DIR/$trace.stdout" capture_generation || true)"
            if [ "$snapshot_generation" = "$CAPTURE_GENERATION" ] &&
                [ "$trace_generation" = "$CAPTURE_GENERATION" ] &&
                file_has_token "$OUT_DIR/$snapshot.stdout" backend IWX &&
                file_has_token "$OUT_DIR/$trace.stdout" backend IWX &&
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

wait_for_initial_pmf_progress() {
    local attempt snapshot progress snapshot_generation progress_generation
    local snapshot_episode progress_episode
    for attempt in $(seq 1 "$INITIAL_ATTEMPTS"); do
        snapshot="initial-progress-$attempt-snapshot"
        progress="initial-progress-$attempt-report"
        if capture_trace_client "$snapshot" get snapshot &&
            capture_trace_client "$progress" get pmf-bip-progress; then
            snapshot_generation="$(extract_u32 "$OUT_DIR/$snapshot.stdout" capture_generation || true)"
            progress_generation="$(extract_u32 "$OUT_DIR/$progress.stdout" capture_generation || true)"
            snapshot_episode="$(extract_u32 "$OUT_DIR/$snapshot.stdout" active_episode || true)"
            progress_episode="$(extract_u32 "$OUT_DIR/$progress.stdout" active_episode || true)"
            if [ "$snapshot_generation" = "$CAPTURE_GENERATION" ] &&
                [ "$progress_generation" = "$CAPTURE_GENERATION" ] &&
                [ -n "$snapshot_episode" ] && [ -n "$progress_episode" ] &&
                [ "$snapshot_episode" -gt 0 ] && [ "$progress_episode" -gt 0 ] &&
                [ "$snapshot_episode" = "$progress_episode" ] &&
                file_has_token "$OUT_DIR/$snapshot.stdout" backend IWX &&
                file_has_token "$OUT_DIR/$progress.stdout" backend IWX &&
                file_has_token "$OUT_DIR/$snapshot.stdout" enabled 1 &&
                file_has_token "$OUT_DIR/$snapshot.stdout" episode_count 1 &&
                file_has_token "$OUT_DIR/$progress.stdout" episode_count 1 &&
                file_has_token "$OUT_DIR/$snapshot.stdout" dropped 0 &&
                file_has_token "$OUT_DIR/$progress.stdout" integrity ok &&
                file_has_token "$OUT_DIR/$progress.stdout" pmf_bip_progress INITIAL_PMF_BIP_READY &&
                file_has_token "$OUT_DIR/$progress.stdout" first_missing_stage none; then
                INITIAL_PMF_PROGRESS=1
                return 0
            fi
        fi
        sleep 1
    done
    return 1
}

read_final_trace_once() {
    local label="$1" snapshot trace report
    local snapshot_generation trace_generation report_generation
    snapshot="$label-snapshot"
    trace="$label-trace"
    report="$label-report"
    capture_trace_client "$snapshot" get snapshot || return 1
    capture_trace_client "$trace" get trace || return 1
    capture_trace_client "$report" get pmf-bip-report || return 1
    snapshot_generation="$(extract_u32 "$OUT_DIR/$snapshot.stdout" capture_generation || true)"
    trace_generation="$(extract_u32 "$OUT_DIR/$trace.stdout" capture_generation || true)"
    report_generation="$(extract_u32 "$OUT_DIR/$report.stdout" capture_generation || true)"
    [ "$snapshot_generation" = "$CAPTURE_GENERATION" ] &&
        [ "$trace_generation" = "$CAPTURE_GENERATION" ] &&
        [ "$report_generation" = "$CAPTURE_GENERATION" ] || return 1
    file_has_token "$OUT_DIR/$snapshot.stdout" backend IWX || return 1
    file_has_token "$OUT_DIR/$trace.stdout" backend IWX || return 1
    file_has_token "$OUT_DIR/$report.stdout" backend IWX || return 1
    file_has_token "$OUT_DIR/$snapshot.stdout" enabled 0 || return 1
    file_has_token "$OUT_DIR/$snapshot.stdout" target_bound 1 || return 1
    file_has_token "$OUT_DIR/$snapshot.stdout" active_episode 0 || return 1
    file_has_token "$OUT_DIR/$trace.stdout" integrity ok || return 1
    file_has_token "$OUT_DIR/$report.stdout" integrity ok || return 1
    TRACE_ENTRY_COUNT="$(extract_u32 "$OUT_DIR/$report.stdout" entries || true)"
    TRACE_EPISODE_COUNT="$(extract_u32 "$OUT_DIR/$report.stdout" episode_count || true)"
    TRACE_DROPPED_ENTRIES="$(extract_u32 "$OUT_DIR/$snapshot.stdout" dropped || true)"
    TRACE_VERDICT="$(extract_token "$OUT_DIR/$report.stdout" pmf_bip_verdict || true)"
    TRACE_FIRST_MISSING_STAGE="$(extract_token "$OUT_DIR/$report.stdout" first_missing_stage || true)"
    case "$TRACE_VERDICT" in
        INITIAL_PMF_BIP_OBSERVED|CROSS_SLOT_REKEY_OBSERVED|PMF_RX_NOT_OBSERVED|Q0_DOORBELL_NOT_OBSERVED|Q0_COMPLETION_NOT_OBSERVED|IGTK_PUBLICATION_NOT_OBSERVED|ACTIVE_SLOT_NOT_OBSERVED|PORT_VALID_NOT_OBSERVED|BACKEND_UNSUPPORTED|BRANCH_NOT_OBSERVED|INTEGRITY_INCONCLUSIVE) ;;
        *) return 1 ;;
    esac
    case "$TRACE_FIRST_MISSING_STAGE" in
        none|capture-seal|pmf-rx|q0-doorbell|q0-completion|igtk-publication|active-slot|port-valid|cross-slot-rekey|unknown) ;;
        *) return 1 ;;
    esac
    [ -n "$TRACE_ENTRY_COUNT" ] && [ -n "$TRACE_EPISODE_COUNT" ] &&
        [ -n "$TRACE_DROPPED_ENTRIES" ] && [ "$TRACE_DROPPED_ENTRIES" = 0 ] || return 1
    TRACE_INTEGRITY=ok
    return 0
}

seal_trace() {
    local sequence
    capture_trace_client seal seal || return 1
    sequence="$(extract_u32 "$OUT_DIR/seal.stdout" seq || true)"
    [ -n "$sequence" ] || return 1
    if wait_for_control_ack seal-ack "$sequence" 0 0 1; then
        TRACE_MAY_BE_ARMED=0
        TRACE_SEAL_ACKNOWLEDGED=1
        return 0
    fi
    return 1
}

disable_trace() {
    local sequence
    capture_trace_client final-off off || return 1
    sequence="$(extract_u32 "$OUT_DIR/final-off.stdout" seq || true)"
    [ -n "$sequence" ] || return 1
    if wait_for_control_ack final-off-ack "$sequence" 0 0 0; then
        FINAL_CONTROL_DISABLED=1
        TRACE_MAY_BE_ARMED=0
        return 0
    fi
    return 1
}

run_bounded_traffic_probe() {
    "${SSH[@]}" "ping -S $LAB_IPV4_BASELINE -c 5 -W 1000 $PINNED_LAB_GATEWAY" \
        >"$OUT_DIR/traffic.stdout" 2>"$OUT_DIR/traffic.stderr" || return 1
    grep -Eq '5 packets transmitted, 5 packets received, 0\.0% packet loss' \
        "$OUT_DIR/traffic.stdout"
}

write_safe_attestation() {
    [ -n "$OUT_DIR" ] && [ -d "$OUT_DIR" ] || return 0
    python3 - "$OUT_DIR/runtime-attestation.json" \
        "$SOURCE_COMMIT" "$SOURCE_IDENTITY_SHA256" "$RELEASE_TAG" "$ARCHIVE_SHA256" "$BINARY_SHA256" "$MACHO_UUID" \
        "$IDENTITY_BEFORE_BOUND" "$IDENTITY_AFTER_BOUND" "$SAVED_PROFILE_READY" \
        "$AP_PREFLIGHT_PASSED" "$AP_REQUIRED_WAS_ACTIVE" "$AP_ROLLBACK_ATTEMPTED" "$AP_ROLLBACK_VERIFIED" \
        "$INITIAL_PMF_PROGRESS" "$TRAFFIC_SUCCESS" "$REKEY_REQUESTED" "$CROSS_SLOT_REKEY_OBSERVED" "$NETWORK_INVARIANTS_OK" \
        "$RESET_SEQUENCE" "$CAPTURE_GENERATION" "$TRACE_BACKEND" "$TRACE_INTEGRITY" "$TRACE_ENTRY_COUNT" "$TRACE_EPISODE_COUNT" "$TRACE_DROPPED_ENTRIES" "$TRACE_VERDICT" "$TRACE_FIRST_MISSING_STAGE" \
        "$RESET_ACK_SYNC" "$INITIAL_SNAPSHOT_BUFFER_SYNC" "$TRACE_SEAL_ACKNOWLEDGED" "$FINAL_CONTROL_DISABLED" "$DOUBLE_READ_STABLE" \
        "$RADIO_OFF_OBSERVED" "$RADIO_ON_OBSERVED" "$RADIO_RECOVERY_ATTEMPTED" "$FAILURE_PHASE" "$RUN_COMPLETE" <<'PY'
import json
import sys
from pathlib import Path

(
    output, source_commit, source_identity, release_tag, archive_sha, binary_sha, macho_uuid,
    identity_before, identity_after, saved_profile, ap_preflight, ap_required, ap_rollback_attempted, ap_rollback,
    initial_progress, traffic, rekey_requested, cross_slot, network_ok,
    reset_sequence, generation, backend, integrity, entry_count, episode_count, dropped,
    verdict, first_missing, reset_sync, initial_sync, sealed, disabled, double_read,
    radio_off, radio_on, radio_recovery, failure_phase, run_complete,
) = sys.argv[1:]

def b(value: str) -> bool:
    return value == "1"

trace_pass = (
    backend == "iwx" and integrity == "ok" and dropped == "0" and
    verdict == "CROSS_SLOT_REKEY_OBSERVED" and first_missing == "none" and
    int(episode_count) == 1 and int(entry_count) > 0 and
    b(reset_sync) and b(initial_sync) and b(sealed) and b(disabled) and b(double_read)
)
result = (
    b(identity_before) and b(identity_after) and b(saved_profile) and
    b(ap_preflight) and b(ap_required) and b(ap_rollback_attempted) and b(ap_rollback) and
    b(initial_progress) and b(traffic) and b(rekey_requested) and b(cross_slot) and
    b(network_ok) and b(radio_off) and b(radio_on) and b(run_complete) and trace_pass
)
document = {
    "schema": "itlwm-tahoe-iwx-pmf-bip-runtime/v1",
    "candidate": {
        "source_commit": source_commit,
        "source_identity_sha256": source_identity,
        "release_tag": release_tag,
        "release_publication_model": "single_mutable_release_per_semantic_version",
        "archive_sha256": archive_sha,
        "binary_sha256": binary_sha,
        "macho_uuid": macho_uuid,
        "identity_before_bound": b(identity_before),
        "identity_after_bound": b(identity_after),
    },
    "scope": {
        "environment": "pinned_disposable_qemu_guest_with_pinned_lab_ap",
        "guest_rebooted_by_runner": False,
        "physical_validation_host_touched": False,
        "host_ap_process_touched": b(ap_required),
        "host_ip_nat_forwarding_route_mutated": False,
    },
    "saved_profile": {
        "preexisting_keychain_or_known_network_required": True,
        "preflight_observed": b(saved_profile),
        "explicit_join_command": False,
        "password_carrier": "none",
    },
    "network_invariants": {
        "default_route_management_interface_preserved": b(network_ok),
        "direct_lab_route_preserved": b(network_ok),
        "preexisting_lab_address_preserved": b(network_ok),
        "explicit_route_command": False,
        "explicit_address_command": False,
        "explicit_dhcp_state_mutating_command": False,
    },
    "ap_switchover": {
        "optional_pmf_preflight_passed": b(ap_preflight),
        "required_pmf_was_active": b(ap_required),
        "optional_pmf_rollback_attempted": b(ap_rollback_attempted),
        "optional_pmf_rollback_verified": b(ap_rollback),
        "host_ip_nat_forwarding_route_mutated": False,
    },
    "pmf_bip": {
        "initial_active_prefix_observed_before_rekey": b(initial_progress),
        "bounded_traffic_probe_succeeded_before_rekey": b(traffic),
        "bounded_group_rekey_requested": b(rekey_requested),
        "sealed_cross_slot_rekey_observed": b(cross_slot),
    },
    "trace": {
        "backend": backend,
        "reset_control_sequence": int(reset_sequence),
        "capture_generation": int(generation),
        "reset_ack_generation_synchronized": b(reset_sync),
        "initial_snapshot_buffer_generation_synchronized": b(initial_sync),
        "seal_control_acknowledged": b(sealed),
        "final_control_disabled": b(disabled),
        "double_read_stable": b(double_read),
        "integrity": integrity,
        "entry_count": int(entry_count),
        "episode_count": int(episode_count),
        "dropped_entries": int(dropped),
        "verdict": verdict,
        "first_missing_stage": first_missing,
    },
    "radio_cycle": {
        "requested_cycles": 1,
        "connection_trigger": "saved_profile_autojoin_only",
        "radio_off_observed": b(radio_off),
        "radio_on_observed": b(radio_on),
        "radio_recovery_attempted": b(radio_recovery),
    },
    "local_only_raw_artifacts": {
        "trace_interface_route_and_hostapd_output_retained_local_only": True,
        "raw_output_committed": False,
    },
    "commit_safety": {
        "wireless_identity_committed": False,
        "ip_or_route_committed": False,
        "secret_material_committed": False,
        "raw_capture_committed": False,
    },
    "result": "PASS" if result else "INCONCLUSIVE",
    "failure_phase": "none" if result else failure_phase,
    "non_claims": [
        "pure SAE functionality",
        "generic Internet reachability",
        "physical-host validation",
        "proof beyond the bounded PMF/BIP and traffic gate",
    ],
}
Path(output).write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
PY
}

cleanup() {
    local rc=$?
    trap - EXIT HUP INT TERM
    set +e
    if [ "$RADIO_OFF_PENDING" -eq 1 ]; then
        RADIO_RECOVERY_ATTEMPTED=1
        remote_radio_power on >/dev/null 2>&1
        if wait_for_radio_state on; then
            RADIO_ON_OBSERVED=1
            RADIO_OFF_PENDING=0
        fi
    fi
    # The external activation can return immediately before this shell records
    # AP_REQUIRED_ACTIVE. A fresh state directory is therefore the ownership
    # boundary, not that advisory local flag. A valid watchdog-written witness
    # is accepted first; otherwise ask the helper for an immediate rollback.
    if [ -n "$AP_STATE_DIR" ] && [ "$AP_ROLLBACK_VERIFIED" -eq 0 ]; then
        if grep -Fxq 'rollback_verified=true' "$AP_STATE_DIR/rollback.status" 2>/dev/null; then
            AP_ROLLBACK_VERIFIED=1
            AP_REQUIRED_ACTIVE=0
        else
            AP_ROLLBACK_ATTEMPTED=1
            if "$AP_HELPER" --rollback --state-dir "$AP_STATE_DIR" \
                >"$OUT_DIR/ap-rollback-cleanup.stdout" 2>"$OUT_DIR/ap-rollback-cleanup.stderr"; then
                if grep -Fxq 'rollback_verified=true' "$AP_STATE_DIR/rollback.status"; then
                    AP_ROLLBACK_VERIFIED=1
                    AP_REQUIRED_ACTIVE=0
                fi
            fi
        fi
    fi
    if [ "$TRACE_MAY_BE_ARMED" -eq 1 ]; then
        disable_trace >/dev/null 2>&1 || true
    fi
    if [ "$IDENTITY_BEFORE_BOUND" -eq 1 ] && [ "$IDENTITY_AFTER_BOUND" -eq 0 ]; then
        capture_identity after || true
    fi
    write_safe_attestation
    [ -z "$KNOWN_HOSTS" ] || /bin/rm -f "$KNOWN_HOSTS"
    exit "$rc"
}

trap cleanup EXIT
trap 'exit 130' HUP INT
trap 'exit 143' TERM

umask 077
mkdir "$OUT_DIR"
chmod 700 "$OUT_DIR"

KNOWN_HOSTS="$(mktemp /tmp/aiam-iwx-pmf-bip-known-hosts.XXXXXX)"
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
capture_identity before || fail_phase candidate-identity-before

DEFAULT_ROUTE_BASELINE="$(guest_default_route_signature 2>/dev/null || true)"
MANAGEMENT_IPV4_BASELINE="$(guest_management_ipv4 2>/dev/null || true)"
LAB_IPV4_BASELINE="$(guest_lab_ipv4 2>/dev/null || true)"
LAB_ROUTE_BASELINE="$(guest_lab_route_signature 2>/dev/null || true)"
[ -n "$DEFAULT_ROUTE_BASELINE" ] && [ -n "$MANAGEMENT_IPV4_BASELINE" ] &&
    [ -n "$LAB_IPV4_BASELINE" ] && [ -n "$LAB_ROUTE_BASELINE" ] ||
    fail_phase guest-network-baseline
case "$DEFAULT_ROUTE_BASELINE" in *"interface=$PINNED_MANAGEMENT_INTERFACE") ;; *) fail_phase default-route-not-management;; esac
case "$LAB_IPV4_BASELINE" in "$PINNED_LAB_PREFIX"*) ;; *) fail_phase lab-address-not-pinned;; esac
case "$LAB_ROUTE_BASELINE" in "destination=$PINNED_LAB_GATEWAY nexthop=direct interface=$PINNED_WIFI_INTERFACE") ;; *) fail_phase lab-route-not-direct;; esac
guest_saved_profile_ready || fail_phase saved-profile-preflight
SAVED_PROFILE_READY=1
wait_for_radio_state on || fail_phase radio-precondition-on
wait_authorized_stable || fail_phase preflight-ap-authorization
assert_network_invariants preflight || fail_phase preflight-network-invariants
"$AP_HELPER" --preflight >"$OUT_DIR/ap-preflight.stdout" 2>"$OUT_DIR/ap-preflight.stderr" ||
    fail_phase ap-preflight
grep -Fxq 'PMF_AP_PREFLIGHT=PASS' "$OUT_DIR/ap-preflight.stdout" || fail_phase ap-preflight-output
AP_PREFLIGHT_PASSED=1

# Arm cleanup before requesting reset: the remote control can enable capture
# before this shell regains control to record the returned sequence.
TRACE_MAY_BE_ARMED=1
capture_trace_client reset reset || fail_phase trace-reset-request
RESET_SEQUENCE="$(extract_u32 "$OUT_DIR/reset.stdout" seq || true)"
[ "$RESET_SEQUENCE" -gt 0 ] 2>/dev/null || fail_phase trace-reset-sequence
if ! wait_for_control_ack reset-ack "$RESET_SEQUENCE" 1 1 0; then
    fail_phase trace-backend-not-iwx
fi
RESET_ACK_SYNC=1
wait_for_reset_snapshot_buffer_sync || fail_phase trace-reset-snapshot-buffer-sync

RADIO_OFF_PENDING=1
remote_radio_power off >/dev/null 2>&1 || fail_phase radio-off-request
wait_for_radio_state off || fail_phase radio-off-observation
RADIO_OFF_OBSERVED=1
assert_network_invariants radio-off || fail_phase radio-off-network-invariants
AP_STATE_DIR="$(mktemp -d /tmp/aiam-pmf-required-switch.XXXXXX)"
chmod 700 "$AP_STATE_DIR"
"$AP_HELPER" --activate --state-dir "$AP_STATE_DIR" --lease-seconds "$AP_LEASE_SECONDS" \
    >"$OUT_DIR/ap-activate.stdout" 2>"$OUT_DIR/ap-activate.stderr" || fail_phase ap-required-activation
AP_REQUIRED_ACTIVE=1
AP_REQUIRED_WAS_ACTIVE=1
grep -Fxq 'PMF_AP_SWITCHOVER=REQUIRED_ACTIVE' "$OUT_DIR/ap-activate.stdout" ||
    fail_phase ap-required-activation-output
assert_network_invariants required-pmf-radio-off || fail_phase required-pmf-radio-off-invariants
remote_radio_power on >/dev/null 2>&1 || fail_phase radio-on-request
wait_for_radio_state on || fail_phase radio-on-observation
RADIO_ON_OBSERVED=1
RADIO_OFF_PENDING=0
wait_authorized_stable || fail_phase required-pmf-ap-authorization
assert_network_invariants required-pmf-autojoin || fail_phase required-pmf-network-invariants
wait_for_initial_pmf_progress || fail_phase initial-pmf-bip-progress
run_bounded_traffic_probe || fail_phase bounded-traffic-probe
TRAFFIC_SUCCESS=1
assert_network_invariants initial-pmf-traffic || fail_phase initial-pmf-traffic-invariants

[ "$INITIAL_PMF_PROGRESS" -eq 1 ] && [ "$TRAFFIC_SUCCESS" -eq 1 ] ||
    fail_phase rekey-without-proven-initial-pmf
"$AP_HELPER" --rekey --state-dir "$AP_STATE_DIR" \
    >"$OUT_DIR/ap-rekey.stdout" 2>"$OUT_DIR/ap-rekey.stderr" || fail_phase bounded-group-rekey
grep -Fxq 'PMF_AP_REKEY=REQUESTED' "$OUT_DIR/ap-rekey.stdout" || fail_phase bounded-group-rekey-output
REKEY_REQUESTED=1
sleep "$REKEY_SETTLE_SECONDS"
wait_authorized_stable || fail_phase post-rekey-ap-authorization
assert_network_invariants post-rekey || fail_phase post-rekey-network-invariants

seal_trace || fail_phase trace-seal
read_final_trace_once read-1 || fail_phase trace-first-read
sleep "$STABLE_READ_DELAY_SECONDS"
read_final_trace_once read-2 || fail_phase trace-second-read
cmp -s "$OUT_DIR/read-1-snapshot.stdout" "$OUT_DIR/read-2-snapshot.stdout" &&
    cmp -s "$OUT_DIR/read-1-trace.stdout" "$OUT_DIR/read-2-trace.stdout" &&
    cmp -s "$OUT_DIR/read-1-report.stdout" "$OUT_DIR/read-2-report.stdout" ||
    fail_phase trace-double-read-unstable
DOUBLE_READ_STABLE=1
disable_trace || fail_phase trace-final-off
assert_network_invariants sealed-trace || fail_phase sealed-trace-network-invariants
if [ "$TRACE_VERDICT" = CROSS_SLOT_REKEY_OBSERVED ] &&
    [ "$TRACE_FIRST_MISSING_STAGE" = none ] &&
    [ "$TRACE_EPISODE_COUNT" = 1 ] && [ "$TRACE_ENTRY_COUNT" -gt 0 ] &&
    [ "$TRACE_DROPPED_ENTRIES" = 0 ]; then
    CROSS_SLOT_REKEY_OBSERVED=1
else
    fail_phase trace-cross-slot-rekey-verdict
fi

AP_ROLLBACK_ATTEMPTED=1
"$AP_HELPER" --rollback --state-dir "$AP_STATE_DIR" \
    >"$OUT_DIR/ap-rollback.stdout" 2>"$OUT_DIR/ap-rollback.stderr" || fail_phase ap-rollback
grep -Fxq 'rollback_verified=true' "$AP_STATE_DIR/rollback.status" || fail_phase ap-rollback-verification
AP_ROLLBACK_VERIFIED=1
AP_REQUIRED_ACTIVE=0
wait_authorized_stable || fail_phase optional-pmf-recovery-authorization
assert_network_invariants optional-pmf-recovery || fail_phase optional-pmf-recovery-invariants
capture_identity after || fail_phase candidate-identity-after

RUN_COMPLETE=1
FAILURE_PHASE=none
printf 'PASS: bounded IWX PMF/BIP initial, traffic, cross-slot rekey, and AP rollback gate complete\n'
