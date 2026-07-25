#!/usr/bin/env bash
# Run one receipt-bound, identity-free WCL physical-scan observation.
#
# The sole stimulus is the trace client's fixed `scan-wcl-physical` command.
# It accepts no wireless identifier or credential, does not join/disassociate,
# does not change radio power/profile/routes/addresses, and does not install,
# load, unload, or reboot anything.  A PASS proves only the bounded one-or-two
# sealed IWN WCL physical-scan lifecycles caused by one public scan stimulus;
# it is explicitly not an association, SAE, roaming, reconnect, multi-AP, or
# data-plane result.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
IDENTITY_CAPTURE="$ROOT/scripts/capture_tahoe_iwn_lab_loaded_identity.py"

PINNED_GUEST="devops@127.0.0.1"
PINNED_PORT=3322
PINNED_GUEST_BUILD="25C56"
PINNED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
PINNED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"

TRACE_TOOL=""
CANDIDATE_RECEIPT=""
OUT_DIR=""
SETTLE_SECONDS=3
ACK_ATTEMPTS=15
STABLE_READ_DELAY_SECONDS=1

KNOWN_HOSTS=""
declare -a SSH

SOURCE_COMMIT=""
SOURCE_IDENTITY_SHA256=""
SOURCE_IDENTITY_PATHS_COUNT=0
LAB_PROFILE=""
LAB_STAGED_KEXT_REPO_PATH=""
ARCHIVE_SHA256=""
INFO_PLIST_SHA256=""
BINARY_SHA256=""
BUNDLE_TREE_SHA256=""
MACHO_UUID=""
BUNDLE_ID=""
TRACE_CLIENT_SHA256=""
TRACE_BACKEND="UNKNOWN"

IDENTITY_BEFORE_BOUND=0
IDENTITY_AFTER_BOUND=0
TRACE_CLIENT_PRE_BOUND=0
TRACE_CLIENT_POST_BOUND=0
TRACE_MAY_BE_ARMED=0
RESET_ACKNOWLEDGED=0
INITIAL_SNAPSHOT_SYNCHRONIZED=0
SEAL_ACKNOWLEDGED=0
FINAL_CONTROL_DISABLED=0
DOUBLE_READ_STABLE=0
SCAN_INVOCATION_COUNT=0
SCAN_EXIT=255
SCAN_OUTCOME="not-run"
SCAN_ENDPOINT_BINDING="unresolved"
SCAN_TOTAL=0
SCAN_BAND_2GHZ=0
SCAN_BAND_5GHZ=0
SCAN_BAND_6GHZ=0
SCAN_BAND_OTHER=0
SCAN_AGGREGATE_VALID=0
CAPTURE_GENERATION=0
WCL_ENTRIES=0
WCL_INTEGRITY="inconclusive"
WCL_EPISODE_COUNT=0
WCL_ACTIVE_EPISODE=0
WCL_VERDICT="INTEGRITY_INCONCLUSIVE"
WCL_FIRST_MISSING_STAGE="unknown"
WCL_RESULT_PUBLICATION=0

RESULT="INCONCLUSIVE"
FAILURE_PHASE="preflight"
FINAL_EXIT=1
ATTESTATION_WRITTEN=0

usage() {
    cat >&2 <<'EOF'
usage: run_tahoe_wcl_physical_scan_runtime.sh \
  --trace-tool /private/tmp/aiam-post-plti-trace-CANDIDATE/airport_itlwm_post_plti_trace \
  --candidate-receipt /local/safe/iwn-lab-candidate-receipt-v2.json \
  --out /fresh/local/evidence/dir \
  [--settle-seconds 2..30] [--ack-attempts 1..60] \
  [--stable-read-delay-seconds 1..10]

The runner requests exactly one fixed, undirected WCL physical scan.  That
single public stimulus may produce one or two sequential driver-owned WCL
physical-scan episodes; every observed episode must independently reach its
sealed categorical terminal.  The runner does not accept or emit a network
name, hardware address, signal, security value, information element, address,
route, profile, credential, or frame.  It only records safe aggregate band
counts and the sealed categorical WCL trace.
EOF
}

fail_phase() {
    FAILURE_PHASE="$1"
    RESULT="INCONCLUSIVE"
    FINAL_EXIT=1
    printf 'INCONCLUSIVE: phase=%s\n' "$FAILURE_PHASE" >&2
    exit 1
}

is_decimal_in_range() {
    local value="$1" min="$2" max="$3"
    case "$value" in ''|*[!0-9]*) return 1;; esac
    [ "$value" -ge "$min" ] && [ "$value" -le "$max" ]
}

is_u32() {
    case "$1" in ''|*[!0-9]*) return 1;; esac
    [ "$1" -le 4294967295 ]
}

valid_trace_tool_path() {
    [[ "$1" =~ ^/private/tmp/aiam-post-plti-trace(-[A-Za-z0-9._-]+)?/airport_itlwm_post_plti_trace$ ]]
}

valid_trace_client_sha256() {
    [[ "$1" =~ ^[0-9a-f]{64}$ ]]
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --trace-tool|--candidate-receipt|--out|--settle-seconds|--ack-attempts|--stable-read-delay-seconds)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --trace-tool) TRACE_TOOL="$2" ;;
                --candidate-receipt) CANDIDATE_RECEIPT="$2" ;;
                --out) OUT_DIR="$2" ;;
                --settle-seconds) SETTLE_SECONDS="$2" ;;
                --ack-attempts) ACK_ATTEMPTS="$2" ;;
                --stable-read-delay-seconds) STABLE_READ_DELAY_SECONDS="$2" ;;
            esac
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done

[ -n "$TRACE_TOOL" ] && [ -n "$CANDIDATE_RECEIPT" ] && [ -n "$OUT_DIR" ] || {
    usage
    exit 2
}
valid_trace_tool_path "$TRACE_TOOL" || {
    printf 'ERROR: --trace-tool must be a restricted guest-local private path\n' >&2
    exit 2
}
case "$OUT_DIR" in /*) ;; *)
    printf 'ERROR: --out must be an absolute fresh local path\n' >&2
    exit 2
    ;;
esac
[ ! -e "$OUT_DIR" ] && [ ! -L "$OUT_DIR" ] || {
    printf 'ERROR: --out must name a fresh path; refusing to overwrite evidence\n' >&2
    exit 2
}
[ -f "$CANDIDATE_RECEIPT" ] && [ ! -L "$CANDIDATE_RECEIPT" ] || {
    printf 'ERROR: --candidate-receipt must be a regular local file\n' >&2
    exit 2
}
[ -x "$IDENTITY_CAPTURE" ] || {
    printf 'ERROR: required read-only identity helper is unavailable\n' >&2
    exit 2
}
for value_range in \
    "$SETTLE_SECONDS:2:30" "$ACK_ATTEMPTS:1:60" \
    "$STABLE_READ_DELAY_SECONDS:1:10"; do
    IFS=: read -r value min max <<<"$value_range"
    is_decimal_in_range "$value" "$min" "$max" || { usage; exit 2; }
done

read_candidate_receipt() {
    local -a fields
    mapfile -t fields < <(python3 - "$ROOT/scripts" "$CANDIDATE_RECEIPT" <<'PY'
import re
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from capture_tahoe_iwn_lab_candidate_receipt import load_direct_runtime_candidate_receipt

try:
    candidate = load_direct_runtime_candidate_receipt(Path(sys.argv[2]))
    keys = (
        "source_commit", "source_identity_sha256", "source_identity_paths_count",
        "profile", "staged_kext_repo_path", "archive_sha256",
        "info_plist_sha256", "binary_sha256", "bundle_tree_sha256",
        "macho_uuid", "bundle_id", "trace_client_sha256",
    )
    if not isinstance(candidate, dict) or not set(keys).issubset(candidate):
        raise ValueError("candidate shape")
    if re.fullmatch(r"[0-9a-f]{40}", str(candidate["source_commit"])) is None:
        raise ValueError("source commit")
    for key in ("source_identity_sha256", "archive_sha256", "info_plist_sha256",
                "binary_sha256", "bundle_tree_sha256", "trace_client_sha256"):
        if re.fullmatch(r"[0-9a-f]{64}", str(candidate[key])) is None:
            raise ValueError(key)
    if type(candidate["source_identity_paths_count"]) is not int or candidate["source_identity_paths_count"] < 1:
        raise ValueError("source identity path count")
    if candidate["profile"] != "iwn-software-pmf-lab":
        raise ValueError("profile")
    if candidate["staged_kext_repo_path"] != "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext":
        raise ValueError("staged kext path")
    if candidate["bundle_id"] != "com.zxystd.AirportItlwm":
        raise ValueError("bundle identifier")
    if re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                    str(candidate["macho_uuid"])) is None:
        raise ValueError("Mach-O UUID")
except Exception:
    raise SystemExit(1)

for key in keys:
    print(candidate[key])
PY
)
    [ "${#fields[@]}" -eq 12 ] || return 1
    SOURCE_COMMIT="${fields[0]}"
    SOURCE_IDENTITY_SHA256="${fields[1]}"
    SOURCE_IDENTITY_PATHS_COUNT="${fields[2]}"
    LAB_PROFILE="${fields[3]}"
    LAB_STAGED_KEXT_REPO_PATH="${fields[4]}"
    ARCHIVE_SHA256="${fields[5]}"
    INFO_PLIST_SHA256="${fields[6]}"
    BINARY_SHA256="${fields[7]}"
    BUNDLE_TREE_SHA256="${fields[8]}"
    MACHO_UUID="${fields[9]}"
    BUNDLE_ID="${fields[10]}"
    TRACE_CLIENT_SHA256="${fields[11]}"
    valid_trace_client_sha256 "$TRACE_CLIENT_SHA256"
}

capture_identity() {
    local label="$1"
    python3 "$IDENTITY_CAPTURE" --candidate-receipt "$CANDIDATE_RECEIPT" \
        --output "$OUT_DIR/identity-$label.json" \
        >"$OUT_DIR/identity-$label.stdout" \
        2>"$OUT_DIR/identity-$label.stderr" || return 1
    python3 - "$OUT_DIR/identity-$label.json" \
        "$SOURCE_COMMIT" "$SOURCE_IDENTITY_SHA256" \
        "$SOURCE_IDENTITY_PATHS_COUNT" "$LAB_PROFILE" \
        "$LAB_STAGED_KEXT_REPO_PATH" "$ARCHIVE_SHA256" \
        "$INFO_PLIST_SHA256" "$BINARY_SHA256" "$BUNDLE_TREE_SHA256" \
        "$MACHO_UUID" "$BUNDLE_ID" "$TRACE_CLIENT_SHA256" <<'PY'
import json
import sys
from pathlib import Path

keys = (
    "source_commit", "source_identity_sha256", "source_identity_paths_count",
    "profile", "staged_kext_repo_path", "archive_sha256", "info_plist_sha256",
    "binary_sha256", "bundle_tree_sha256", "macho_uuid", "bundle_id",
    "trace_client_sha256",
)

try:
    evidence = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    candidate = evidence.get("expected_local_lab_candidate")
    checks = evidence.get("candidate_binding", {}).get("checks")
    if (evidence.get("schema_version") != "itlwm-tahoe-iwn-lab-loaded-identity/v1" or
            not isinstance(candidate, dict) or set(candidate) != set(keys) or
            any(candidate[key] != value for key, value in zip(keys, sys.argv[2:])) or
            evidence.get("candidate_binding", {}).get("candidate_kext_bound") is not True or
            not isinstance(checks, dict) or not checks or not all(value is True for value in checks.values()) or
            evidence.get("verdict", {}).get("ready_for_exact_local_lab_candidate_runtime_experiment") is not True):
        raise ValueError("loaded identity binding")
except Exception:
    raise SystemExit(1)
PY
    case "$label" in
        before) IDENTITY_BEFORE_BOUND=1 ;;
        after) IDENTITY_AFTER_BOUND=1 ;;
        *) return 1 ;;
    esac
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
    is_u32 "$value" || return 1
    printf '%s\n' "$value"
}

file_has_token() {
    local observed
    observed="$(extract_token "$1" "$2")" || return 1
    [ "$observed" = "$3" ]
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
parent="${tool%/airport_itlwm_post_plti_trace}"
test -d "$parent" && test ! -L "$parent"
physical_parent="$(CDPATH= cd -P -- "$parent" && pwd -P)"
case "$physical_parent" in
    /private/tmp/aiam-post-plti-trace|/private/tmp/aiam-post-plti-trace-*) ;;
    *) exit 65 ;;
esac
[ "$physical_parent" = "$parent" ]
test -f "$tool" && test ! -L "$tool" && test -x "$tool"
observed="$(LC_ALL=C PATH=/usr/bin:/bin /usr/bin/shasum -a 256 "$tool" |
    /usr/bin/awk -v path="$tool" '
        function is_lower_hex64(value) { return length(value) == 64 && value !~ /[^0-9a-f]/ }
        NR == 1 && NF == 2 && is_lower_hex64($1) && $2 == path { value = $1; next }
        { invalid = 1 }
        END { if (NR != 1 || invalid || value == "") exit 1; print value }
    ')"
[ "$observed" = "$expected_sha256" ]
exec "$tool" "$@"
REMOTE
}

remote_trace_client_exists() {
    # The control acknowledgement is intentionally not a precondition here:
    # it is first published by the reset below.  This only binds the local
    # interpreter bytes before the trace-control state machine is touched.
    "${SSH[@]}" /bin/bash -s -- "$TRACE_TOOL" "$TRACE_CLIENT_SHA256" <<'REMOTE'
set -euo pipefail
tool="$1"
expected_sha256="$2"
case "$tool" in
    /private/tmp/aiam-post-plti-trace/airport_itlwm_post_plti_trace|/private/tmp/aiam-post-plti-trace-*/airport_itlwm_post_plti_trace) ;;
    *) exit 64 ;;
esac
parent="${tool%/airport_itlwm_post_plti_trace}"
test -d "$parent" && test ! -L "$parent"
physical_parent="$(CDPATH= cd -P -- "$parent" && pwd -P)"
case "$physical_parent" in
    /private/tmp/aiam-post-plti-trace|/private/tmp/aiam-post-plti-trace-*) ;;
    *) exit 65 ;;
esac
[ "$physical_parent" = "$parent" ]
test -f "$tool" && test ! -L "$tool" && test -x "$tool"
observed="$(LC_ALL=C PATH=/usr/bin:/bin /usr/bin/shasum -a 256 "$tool" |
    /usr/bin/awk -v path="$tool" '
        function is_lower_hex64(value) { return length(value) == 64 && value !~ /[^0-9a-f]/ }
        NR == 1 && NF == 2 && is_lower_hex64($1) && $2 == path { value = $1; next }
        { invalid = 1 }
        END { if (NR != 1 || invalid || value == "") exit 1; print value }
    ')"
[ "$observed" = "$expected_sha256" ]
REMOTE
}

capture_trace_client() {
    local label="$1"
    shift
    remote_trace "$@" >"$OUT_DIR/$label.stdout" 2>"$OUT_DIR/$label.stderr"
}

wait_for_control_ack() {
    local label="$1" sequence="$2" expected_enable="$3" expected_reset="$4" expected_seal="$5"
    local attempt generation backend
    for attempt in $(seq 1 "$ACK_ATTEMPTS"); do
        if capture_trace_client "$label-$attempt" get control; then
            if file_has_token "$OUT_DIR/$label-$attempt.stdout" seq "$sequence" &&
                file_has_token "$OUT_DIR/$label-$attempt.stdout" applied 1 &&
                file_has_token "$OUT_DIR/$label-$attempt.stdout" enable "$expected_enable" &&
                file_has_token "$OUT_DIR/$label-$attempt.stdout" reset "$expected_reset" &&
                file_has_token "$OUT_DIR/$label-$attempt.stdout" seal "$expected_seal" &&
                file_has_token "$OUT_DIR/$label-$attempt.stdout" bound 1; then
                backend="$(extract_u32 "$OUT_DIR/$label-$attempt.stdout" backend || true)"
                generation="$(extract_u32 "$OUT_DIR/$label-$attempt.stdout" generation || true)"
                [ "$backend" = 1 ] && [ -n "$generation" ] && [ "$generation" -gt 0 ] || {
                    sleep 1
                    continue
                }
                TRACE_BACKEND="IWN"
                if [ "$expected_enable" = 1 ]; then
                    CAPTURE_GENERATION="$generation"
                    return 0
                fi
                # A reset write can have taken effect even if its ACK was lost.
                # In that case cleanup must still be able to prove a later off.
                [ "$CAPTURE_GENERATION" -eq 0 ] && return 0
                [ "$generation" = "$CAPTURE_GENERATION" ] && return 0
            fi
        fi
        sleep 1
    done
    return 1
}

parse_initial_state() {
    python3 - "$1" "$2" "$CAPTURE_GENERATION" <<'PY'
import sys
from pathlib import Path


def fields(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        for item in line.split():
            if "=" not in item:
                raise ValueError("non key-value output")
            key, value = item.split("=", 1)
            if not key or not value or key in values:
                raise ValueError("duplicate or empty key")
            values[key] = value
    return values


def u32(value: str) -> int:
    if not value.isdecimal() or int(value) > 0xFFFFFFFF:
        raise ValueError("u32")
    return int(value)


try:
    snapshot = fields(Path(sys.argv[1]))
    report = fields(Path(sys.argv[2]))
    generation = u32(sys.argv[3])
    if set(snapshot) != {"version", "capture_generation", "backend", "enabled", "target_bound", "active_episode", "episode_count", "entry_count", "dropped", "first_sequence", "latest_sequence"}:
        raise ValueError("snapshot shape")
    if set(report) != {"capture_generation", "backend", "entries", "integrity", "episode_count", "active_episode", "iwn_wcl_physical_scan_verdict", "first_missing_stage", "result_publication_issued"}:
        raise ValueError("report shape")
    for key in ("version", "capture_generation", "enabled", "target_bound", "active_episode", "episode_count", "entry_count", "dropped", "first_sequence", "latest_sequence"):
        u32(snapshot[key])
    for key in ("capture_generation", "entries", "episode_count", "active_episode", "result_publication_issued"):
        u32(report[key])
    if (u32(snapshot["version"]) != 7 or u32(snapshot["capture_generation"]) != generation or
            u32(report["capture_generation"]) != generation or snapshot["backend"] != "IWN" or
            report["backend"] != "IWN" or u32(snapshot["enabled"]) != 1 or
            u32(snapshot["target_bound"]) != 1 or u32(snapshot["active_episode"]) != 0 or
            u32(snapshot["episode_count"]) != 0 or u32(snapshot["entry_count"]) != 0 or
            u32(snapshot["dropped"]) != 0 or u32(report["entries"]) != 0 or
            u32(report["episode_count"]) != 0 or u32(report["active_episode"]) != 0 or
            report["integrity"] != "ok" or
            report["iwn_wcl_physical_scan_verdict"] != "BRANCH_NOT_OBSERVED" or
            report["first_missing_stage"] != "request" or
            report["result_publication_issued"] != "0"):
        raise ValueError("initial state")
except Exception:
    raise SystemExit(1)
PY
}

wait_for_initial_snapshot() {
    local attempt snapshot report
    for attempt in $(seq 1 "$ACK_ATTEMPTS"); do
        snapshot="reset-sync-$attempt-snapshot"
        report="reset-sync-$attempt-report"
        if capture_trace_client "$snapshot" get snapshot &&
            capture_trace_client "$report" get iwn-wcl-physical-scan-report &&
            parse_initial_state "$OUT_DIR/$snapshot.stdout" "$OUT_DIR/$report.stdout"; then
            INITIAL_SNAPSHOT_SYNCHRONIZED=1
            return 0
        fi
        sleep 1
    done
    return 1
}

preseal_episode_is_closed() {
    python3 - "$1" "$2" "$CAPTURE_GENERATION" <<'PY'
import sys
from pathlib import Path

VERDICTS = {
    "INTEGRITY_INCONCLUSIVE", "BACKEND_UNSUPPORTED", "BRANCH_NOT_OBSERVED",
    "LOWER_LEASE_NOT_OBSERVED", "TERMINAL_NOT_OBSERVED", "TERMINAL_ABORTED",
    "DONE_PUBLICATION_NOT_OBSERVED", "IWN_WCL_PHYSICAL_SCAN_OBSERVED",
}
STAGES = {"none", "request", "lower-lease", "terminal", "done-publication", "unknown"}


def fields(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        for item in line.split():
            if "=" not in item:
                raise ValueError("non key-value output")
            key, value = item.split("=", 1)
            if not key or not value or key in values:
                raise ValueError("duplicate or empty key")
            values[key] = value
    return values


def u32(value: str) -> int:
    if not value.isdecimal() or int(value) > 0xFFFFFFFF:
        raise ValueError("u32")
    return int(value)


try:
    snapshot = fields(Path(sys.argv[1]))
    report = fields(Path(sys.argv[2]))
    generation = u32(sys.argv[3])
    if set(snapshot) != {"version", "capture_generation", "backend", "enabled", "target_bound", "active_episode", "episode_count", "entry_count", "dropped", "first_sequence", "latest_sequence"}:
        raise ValueError("snapshot shape")
    if set(report) != {"capture_generation", "backend", "entries", "integrity", "episode_count", "active_episode", "iwn_wcl_physical_scan_verdict", "first_missing_stage", "result_publication_issued"}:
        raise ValueError("report shape")
    for key in ("version", "capture_generation", "enabled", "target_bound", "active_episode", "episode_count", "entry_count", "dropped", "first_sequence", "latest_sequence"):
        u32(snapshot[key])
    for key in ("capture_generation", "entries", "episode_count", "active_episode", "result_publication_issued"):
        u32(report[key])
    if (u32(snapshot["version"]) != 7 or u32(snapshot["capture_generation"]) != generation or
            u32(report["capture_generation"]) != generation or snapshot["backend"] != "IWN" or
            report["backend"] != "IWN" or u32(snapshot["enabled"]) != 1 or
            u32(snapshot["target_bound"]) != 1 or u32(snapshot["active_episode"]) != 0 or
            u32(report["active_episode"]) != 0 or
            u32(snapshot["episode_count"]) not in {1, 2} or
            u32(report["episode_count"]) not in {1, 2} or
            u32(snapshot["entry_count"]) < 3 * u32(snapshot["episode_count"]) or
            u32(snapshot["entry_count"]) != u32(report["entries"]) or
            u32(snapshot["dropped"]) != 0 or report["integrity"] not in {"ok", "inconclusive"} or
            report["iwn_wcl_physical_scan_verdict"] not in VERDICTS or
            report["first_missing_stage"] not in STAGES or
            report["result_publication_issued"] not in {"0", "1"}):
        raise ValueError("preseal closed state")
except Exception:
    raise SystemExit(1)
PY
}

wait_for_preseal_episode_close() {
    local attempt snapshot report closed=0
    for attempt in $(seq 1 "$SETTLE_SECONDS"); do
        snapshot="preseal-close-$attempt-snapshot"
        report="preseal-close-$attempt-report"
        if capture_trace_client "$snapshot" get snapshot &&
            capture_trace_client "$report" get iwn-wcl-physical-scan-report &&
            preseal_episode_is_closed "$OUT_DIR/$snapshot.stdout" "$OUT_DIR/$report.stdout"; then
            # Do not seal immediately after a first closed pass: one public
            # CoreWLAN scan may enqueue its bounded second physical pass just
            # behind it.  Retain the full settle window without a second
            # stimulus, then seal the final aggregate.
            closed=1
        fi
        [ "$attempt" -lt "$SETTLE_SECONDS" ] && sleep 1
    done
    [ "$closed" = 1 ]
}

parse_scan_stimulus() {
    local -a values
    mapfile -t values < <(python3 - "$1" <<'PY'
import re
import sys
from pathlib import Path

pattern = re.compile(
    r"wcl_physical_scan_stimulus=(ok|client-unavailable|interface-unavailable|scan-failed|count-overflow|airport-itlwm-bsd-unresolved) "
    r"endpoint_binding=(airport-itlwm-bsd|unresolved) total=([0-9]+) "
    r"band_2ghz=([0-9]+) band_5ghz=([0-9]+) "
    r"band_6ghz=([0-9]+) band_other=([0-9]+)"
)
try:
    lines = Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
    if len(lines) != 1:
        raise ValueError("line count")
    match = pattern.fullmatch(lines[0])
    if match is None:
        raise ValueError("shape")
    values = match.groups()
    if any(int(value) > 0xFFFFFFFF for value in values[2:]):
        raise ValueError("u32")
    if ((values[0] == "airport-itlwm-bsd-unresolved") !=
            (values[1] == "unresolved")):
        raise ValueError("endpoint binding")
except Exception:
    raise SystemExit(1)
print("\n".join(values))
PY
)
    [ "${#values[@]}" -eq 7 ] || return 1
    SCAN_OUTCOME="${values[0]}"
    SCAN_ENDPOINT_BINDING="${values[1]}"
    SCAN_TOTAL="${values[2]}"
    SCAN_BAND_2GHZ="${values[3]}"
    SCAN_BAND_5GHZ="${values[4]}"
    SCAN_BAND_6GHZ="${values[5]}"
    SCAN_BAND_OTHER="${values[6]}"
    local sum=$((SCAN_BAND_2GHZ + SCAN_BAND_5GHZ + SCAN_BAND_6GHZ + SCAN_BAND_OTHER))
    [ "$sum" = "$SCAN_TOTAL" ] || return 1
    SCAN_AGGREGATE_VALID=1
}

parse_final_state() {
    local -a values
    mapfile -t values < <(python3 - "$1" "$2" "$CAPTURE_GENERATION" <<'PY'
import sys
from pathlib import Path

VERDICTS = {
    "INTEGRITY_INCONCLUSIVE", "BACKEND_UNSUPPORTED", "BRANCH_NOT_OBSERVED",
    "LOWER_LEASE_NOT_OBSERVED", "TERMINAL_NOT_OBSERVED", "TERMINAL_ABORTED",
    "DONE_PUBLICATION_NOT_OBSERVED", "IWN_WCL_PHYSICAL_SCAN_OBSERVED",
}
STAGES = {"none", "request", "lower-lease", "terminal", "done-publication", "unknown"}


def fields(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        for item in line.split():
            if "=" not in item:
                raise ValueError("non key-value output")
            key, value = item.split("=", 1)
            if not key or not value or key in values:
                raise ValueError("duplicate or empty key")
            values[key] = value
    return values


def u32(value: str) -> int:
    if not value.isdecimal() or int(value) > 0xFFFFFFFF:
        raise ValueError("u32")
    return int(value)


try:
    snapshot = fields(Path(sys.argv[1]))
    report = fields(Path(sys.argv[2]))
    generation = u32(sys.argv[3])
    if set(snapshot) != {"version", "capture_generation", "backend", "enabled", "target_bound", "active_episode", "episode_count", "entry_count", "dropped", "first_sequence", "latest_sequence"}:
        raise ValueError("snapshot shape")
    if set(report) != {"capture_generation", "backend", "entries", "integrity", "episode_count", "active_episode", "iwn_wcl_physical_scan_verdict", "first_missing_stage", "result_publication_issued"}:
        raise ValueError("report shape")
    for key in ("version", "capture_generation", "enabled", "target_bound", "active_episode", "episode_count", "entry_count", "dropped", "first_sequence", "latest_sequence"):
        u32(snapshot[key])
    for key in ("capture_generation", "entries", "episode_count", "active_episode", "result_publication_issued"):
        u32(report[key])
    if (u32(snapshot["version"]) != 7 or u32(snapshot["capture_generation"]) != generation or
            u32(report["capture_generation"]) != generation or snapshot["backend"] != "IWN" or
            report["backend"] != "IWN" or u32(snapshot["enabled"]) != 0 or
            u32(snapshot["target_bound"]) != 1 or u32(snapshot["active_episode"]) != 0 or
            u32(report["active_episode"]) != 0 or u32(snapshot["episode_count"]) != u32(report["episode_count"]) or
            u32(snapshot["entry_count"]) != u32(report["entries"]) or u32(snapshot["dropped"]) != 0 or
            report["integrity"] not in {"ok", "inconclusive"} or
            report["iwn_wcl_physical_scan_verdict"] not in VERDICTS or
            report["first_missing_stage"] not in STAGES or
            report["result_publication_issued"] not in {"0", "1"}):
        raise ValueError("final state")
except Exception:
    raise SystemExit(1)

for key in ("entries", "integrity", "episode_count", "active_episode",
            "iwn_wcl_physical_scan_verdict", "first_missing_stage",
            "result_publication_issued"):
    print(report[key])
PY
)
    [ "${#values[@]}" -eq 7 ] || return 1
    WCL_ENTRIES="${values[0]}"
    WCL_INTEGRITY="${values[1]}"
    WCL_EPISODE_COUNT="${values[2]}"
    WCL_ACTIVE_EPISODE="${values[3]}"
    WCL_VERDICT="${values[4]}"
    WCL_FIRST_MISSING_STAGE="${values[5]}"
    WCL_RESULT_PUBLICATION="${values[6]}"
}

capture_final_state() {
    local label="$1"
    capture_trace_client "$label-snapshot" get snapshot || return 1
    capture_trace_client "$label-report" get iwn-wcl-physical-scan-report || return 1
    parse_final_state "$OUT_DIR/$label-snapshot.stdout" "$OUT_DIR/$label-report.stdout" || return 1
    printf '%s %s %s %s %s %s %s\n' \
        "$WCL_ENTRIES" "$WCL_INTEGRITY" "$WCL_EPISODE_COUNT" \
        "$WCL_ACTIVE_EPISODE" "$WCL_VERDICT" "$WCL_FIRST_MISSING_STAGE" \
        "$WCL_RESULT_PUBLICATION" >"$OUT_DIR/$label.normalized"
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

write_safe_attestation() {
    [ "$ATTESTATION_WRITTEN" -eq 0 ] || return 0
    [ -n "$SOURCE_COMMIT" ] && [ -d "$OUT_DIR" ] || return 0
    AIAM_WCL_SOURCE_COMMIT="$SOURCE_COMMIT" \
    AIAM_WCL_SOURCE_IDENTITY_SHA256="$SOURCE_IDENTITY_SHA256" \
    AIAM_WCL_SOURCE_IDENTITY_PATHS_COUNT="$SOURCE_IDENTITY_PATHS_COUNT" \
    AIAM_WCL_PROFILE="$LAB_PROFILE" \
    AIAM_WCL_STAGED_KEXT_PATH="$LAB_STAGED_KEXT_REPO_PATH" \
    AIAM_WCL_ARCHIVE_SHA256="$ARCHIVE_SHA256" \
    AIAM_WCL_INFO_SHA256="$INFO_PLIST_SHA256" \
    AIAM_WCL_BINARY_SHA256="$BINARY_SHA256" \
    AIAM_WCL_TREE_SHA256="$BUNDLE_TREE_SHA256" \
    AIAM_WCL_UUID="$MACHO_UUID" \
    AIAM_WCL_BUNDLE_ID="$BUNDLE_ID" \
    AIAM_WCL_TRACE_SHA256="$TRACE_CLIENT_SHA256" \
    AIAM_WCL_TRACE_BACKEND="$TRACE_BACKEND" \
    AIAM_WCL_ID_BEFORE="$IDENTITY_BEFORE_BOUND" \
    AIAM_WCL_ID_AFTER="$IDENTITY_AFTER_BOUND" \
    AIAM_WCL_TRACE_PRE="$TRACE_CLIENT_PRE_BOUND" \
    AIAM_WCL_TRACE_POST="$TRACE_CLIENT_POST_BOUND" \
    AIAM_WCL_RESET_ACK="$RESET_ACKNOWLEDGED" \
    AIAM_WCL_INITIAL_SYNC="$INITIAL_SNAPSHOT_SYNCHRONIZED" \
    AIAM_WCL_SEAL_ACK="$SEAL_ACKNOWLEDGED" \
    AIAM_WCL_FINAL_OFF="$FINAL_CONTROL_DISABLED" \
    AIAM_WCL_DOUBLE_READ="$DOUBLE_READ_STABLE" \
    AIAM_WCL_SCAN_COUNT="$SCAN_INVOCATION_COUNT" \
    AIAM_WCL_SCAN_EXIT="$SCAN_EXIT" \
    AIAM_WCL_SCAN_OUTCOME="$SCAN_OUTCOME" \
    AIAM_WCL_SCAN_ENDPOINT_BINDING="$SCAN_ENDPOINT_BINDING" \
    AIAM_WCL_SCAN_TOTAL="$SCAN_TOTAL" \
    AIAM_WCL_SCAN_2G="$SCAN_BAND_2GHZ" \
    AIAM_WCL_SCAN_5G="$SCAN_BAND_5GHZ" \
    AIAM_WCL_SCAN_6G="$SCAN_BAND_6GHZ" \
    AIAM_WCL_SCAN_OTHER="$SCAN_BAND_OTHER" \
    AIAM_WCL_SCAN_VALID="$SCAN_AGGREGATE_VALID" \
    AIAM_WCL_GENERATION="$CAPTURE_GENERATION" \
    AIAM_WCL_ENTRIES="$WCL_ENTRIES" \
    AIAM_WCL_INTEGRITY="$WCL_INTEGRITY" \
    AIAM_WCL_EPISODES="$WCL_EPISODE_COUNT" \
    AIAM_WCL_ACTIVE="$WCL_ACTIVE_EPISODE" \
    AIAM_WCL_VERDICT="$WCL_VERDICT" \
    AIAM_WCL_MISSING="$WCL_FIRST_MISSING_STAGE" \
    AIAM_WCL_RESULT_PUBLICATION="$WCL_RESULT_PUBLICATION" \
    AIAM_WCL_RESULT="$RESULT" \
    AIAM_WCL_FAILURE="$FAILURE_PHASE" \
    python3 - "$OUT_DIR/runtime-attestation.json" <<'PY'
import json
import os
import re
import sys
from pathlib import Path


def value(key: str) -> str:
    return os.environ["AIAM_WCL_" + key]


def b(key: str) -> bool:
    return value(key) == "1"


def u32(key: str) -> int:
    raw = value(key)
    if not raw.isdecimal() or int(raw) > 0xFFFFFFFF:
        return 0
    return int(raw)


candidate = {
    "kind": "local-unpublished-iwn-lab-candidate",
    "source_commit": value("SOURCE_COMMIT"),
    "source_identity_sha256": value("SOURCE_IDENTITY_SHA256"),
    "source_identity_paths_count": u32("SOURCE_IDENTITY_PATHS_COUNT"),
    "profile": value("PROFILE"),
    "staged_kext_repo_path": value("STAGED_KEXT_PATH"),
    "archive_sha256": value("ARCHIVE_SHA256"),
    "info_plist_sha256": value("INFO_SHA256"),
    "binary_sha256": value("BINARY_SHA256"),
    "bundle_tree_sha256": value("TREE_SHA256"),
    "macho_uuid": value("UUID"),
    "bundle_id": value("BUNDLE_ID"),
    "trace_client_sha256": value("TRACE_SHA256"),
    "identity_binding_precondition": "PASS" if b("ID_BEFORE") and b("ID_AFTER") else "INCONCLUSIVE",
    "trace_client_receipt_binding_precondition": "PASS" if b("TRACE_PRE") and b("TRACE_POST") else "INCONCLUSIVE",
}
document = {
    "schema": "itlwm-tahoe-iwn-wcl-physical-scan-runtime/v3",
    "candidate": candidate,
    "scope": {
        "environment": "pinned_disposable_qemu_guest",
        "physical_host_touched": False,
        "guest_rebooted_by_runner": False,
        "radio_power_changed": False,
        "association_or_profile_changed": False,
        "guest_network_identity_collected": False,
        "fixed_undirected_scan_requested": b("SCAN_COUNT"),
    },
    "physical_scan_stimulus": {
        "command": "scan-wcl-physical",
        "invocation_count": u32("SCAN_COUNT"),
        "client_exit_zero": value("SCAN_EXIT") == "0",
        "outcome": value("SCAN_OUTCOME"),
        "endpoint_binding": value("SCAN_ENDPOINT_BINDING"),
        "total": u32("SCAN_TOTAL"),
        "band_2ghz": u32("SCAN_2G"),
        "band_5ghz": u32("SCAN_5G"),
        "band_6ghz": u32("SCAN_6G"),
        "band_other": u32("SCAN_OTHER"),
        "aggregate_sum_valid": b("SCAN_VALID"),
    },
    "trace_lifecycle": {
        "capture_generation": u32("GENERATION"),
        "backend": value("TRACE_BACKEND"),
        "reset_control_acknowledged": b("RESET_ACK"),
        "initial_snapshot_synchronized": b("INITIAL_SYNC"),
        "seal_control_acknowledged": b("SEAL_ACK"),
        "final_control_disabled": b("FINAL_OFF"),
        "double_read_stable": b("DOUBLE_READ"),
    },
    "iwn_wcl_physical_scan_trace": {
        "entry_count": u32("ENTRIES"),
        "integrity": value("INTEGRITY"),
        "episode_count": u32("EPISODES"),
        "active_episode": u32("ACTIVE"),
        "verdict": value("VERDICT"),
        "first_missing_stage": value("MISSING"),
        "result_publication_issued": b("RESULT_PUBLICATION"),
    },
    "result": value("RESULT"),
    "failure_phase": value("FAILURE"),
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
        "association, authentication, or SAE functionality",
        "roaming, reconnect, or multi-AP behavior",
        "data-plane or Internet reachability",
        "physical-host validation",
        "proof beyond one public scan invocation and its bounded IWN WCL physical-scan lifecycles",
    ],
}
Path(sys.argv[1]).write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                              encoding="utf-8")
PY
    ATTESTATION_WRITTEN=1
}

cleanup() {
    local rc=$?
    trap - EXIT HUP INT TERM
    set +e
    if [ "$TRACE_MAY_BE_ARMED" -eq 1 ]; then
        disable_trace >/dev/null 2>&1 || true
    fi
    if [ "$IDENTITY_BEFORE_BOUND" -eq 1 ] && [ "$IDENTITY_AFTER_BOUND" -eq 0 ]; then
        capture_identity after || true
    fi
    if [ "$TRACE_CLIENT_PRE_BOUND" -eq 1 ] && [ "$TRACE_CLIENT_POST_BOUND" -eq 0 ] &&
            [ "${#SSH[@]}" -gt 0 ]; then
        remote_trace_client_exists && TRACE_CLIENT_POST_BOUND=1
    fi
    write_safe_attestation
    [ -z "$KNOWN_HOSTS" ] || rm -f "$KNOWN_HOSTS"
    exit "$rc"
}

trap cleanup EXIT
trap 'exit 130' HUP INT
trap 'exit 143' TERM

read_candidate_receipt || {
    printf 'ERROR: --candidate-receipt is not a valid typed local-lab receipt\n' >&2
    exit 2
}
umask 077
mkdir -p "$OUT_DIR"
chmod 700 "$OUT_DIR"

KNOWN_HOSTS="$(mktemp /tmp/aiam-wcl-physical-scan-known-hosts.XXXXXX)"
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
capture_identity before || fail_phase candidate-identity-before
remote_trace_client_exists || fail_phase trace-client-preflight
TRACE_CLIENT_PRE_BOUND=1

TRACE_MAY_BE_ARMED=1
capture_trace_client reset reset || fail_phase trace-reset-request
reset_sequence="$(extract_u32 "$OUT_DIR/reset.stdout" seq || true)"
[ -n "$reset_sequence" ] || fail_phase trace-reset-sequence
wait_for_control_ack reset-ack "$reset_sequence" 1 1 0 || fail_phase trace-reset-ack
RESET_ACKNOWLEDGED=1
wait_for_initial_snapshot || fail_phase trace-reset-snapshot-sync

SCAN_INVOCATION_COUNT=1
set +e
remote_trace scan-wcl-physical >"$OUT_DIR/scan-stimulus.stdout" 2>"$OUT_DIR/scan-stimulus.stderr"
SCAN_EXIT=$?
set -e
parse_scan_stimulus "$OUT_DIR/scan-stimulus.stdout" || fail_phase scan-stimulus-parse
if [ "$SCAN_OUTCOME" = ok ]; then
    [ "$SCAN_EXIT" = 0 ] || fail_phase scan-stimulus-exit
else
    [ "$SCAN_EXIT" -ne 0 ] || fail_phase scan-stimulus-exit
fi

# CoreWLAN returns after the request but the driver terminal callback can be
# queued behind it.  Observe a closed bounded aggregate for the full settle
# window; on timeout seal anyway and retain an honest categorical diagnostic.
wait_for_preseal_episode_close || true
capture_trace_client seal seal || fail_phase trace-seal
seal_sequence="$(extract_u32 "$OUT_DIR/seal.stdout" seq || true)"
[ -n "$seal_sequence" ] || fail_phase trace-seal-sequence
wait_for_control_ack seal-ack "$seal_sequence" 0 0 1 || fail_phase trace-seal-ack
SEAL_ACKNOWLEDGED=1
TRACE_MAY_BE_ARMED=0
FINAL_CONTROL_DISABLED=1

capture_final_state state-read-1 || fail_phase wcl-state-first-read
sleep "$STABLE_READ_DELAY_SECONDS"
capture_final_state state-read-2 || fail_phase wcl-state-second-read
cmp -s "$OUT_DIR/state-read-1-snapshot.stdout" "$OUT_DIR/state-read-2-snapshot.stdout" &&
    cmp -s "$OUT_DIR/state-read-1-report.stdout" "$OUT_DIR/state-read-2-report.stdout" &&
    cmp -s "$OUT_DIR/state-read-1.normalized" "$OUT_DIR/state-read-2.normalized" ||
    fail_phase wcl-state-double-read-unstable
DOUBLE_READ_STABLE=1

capture_identity after || fail_phase candidate-identity-after
remote_trace_client_exists || fail_phase trace-client-postflight
TRACE_CLIENT_POST_BOUND=1

if [ "$SCAN_OUTCOME" = ok ] && [ "$SCAN_ENDPOINT_BINDING" = airport-itlwm-bsd ] &&
    [ "$SCAN_EXIT" = 0 ] &&
    [ "$SCAN_AGGREGATE_VALID" = 1 ] && [ "$RESET_ACKNOWLEDGED" = 1 ] &&
    [ "$INITIAL_SNAPSHOT_SYNCHRONIZED" = 1 ] && [ "$SEAL_ACKNOWLEDGED" = 1 ] &&
    [ "$FINAL_CONTROL_DISABLED" = 1 ] && [ "$DOUBLE_READ_STABLE" = 1 ] &&
    [ "$IDENTITY_BEFORE_BOUND" = 1 ] && [ "$IDENTITY_AFTER_BOUND" = 1 ] &&
    [ "$TRACE_CLIENT_PRE_BOUND" = 1 ] && [ "$TRACE_CLIENT_POST_BOUND" = 1 ] &&
    [ "$CAPTURE_GENERATION" -gt 0 ] && [ "$WCL_EPISODE_COUNT" -ge 1 ] &&
    [ "$WCL_EPISODE_COUNT" -le 2 ] &&
    [ "$WCL_ENTRIES" -ge $((WCL_EPISODE_COUNT * 4)) ] &&
    [ "$WCL_ENTRIES" -le 128 ] && [ "$WCL_INTEGRITY" = ok ] &&
    [ "$WCL_ACTIVE_EPISODE" = 0 ] &&
    [ "$WCL_VERDICT" = IWN_WCL_PHYSICAL_SCAN_OBSERVED ] &&
    [ "$WCL_FIRST_MISSING_STAGE" = none ] &&
    { [ "$WCL_RESULT_PUBLICATION" = 0 ] || [ "$WCL_RESULT_PUBLICATION" = 1 ]; }; then
    RESULT="PASS"
    FAILURE_PHASE="none"
    FINAL_EXIT=0
    printf 'PASS: one public WCL scan stimulus observed with bounded sealed IWN physical-scan lifecycles\n'
else
    RESULT="INCONCLUSIVE"
    FAILURE_PHASE="trace-verdict-diagnostic"
    FINAL_EXIT=0
    printf 'INCONCLUSIVE: sealed WCL physical-scan aggregate retained as local-only diagnostic evidence\n'
fi
exit "$FINAL_EXIT"
