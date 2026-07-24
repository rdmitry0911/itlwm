#!/usr/bin/env bash
# Run one provenance-bound, credential-free direct-IWN-SAE observation.
#
# This is deliberately a thin, lab-only companion to the bounded post-PLTI
# runner.  The latter owns the one radio OFF/ON transition, forces the fresh
# net80211 SCAN epoch by arming/resetting while the radio is Off, seals the
# trace, and restores the radio on failure.  Keeping that owner singular
# prevents this wrapper from accidentally adding a second association trigger.
#
# The only connection stimulus is the guest's already-authorized saved-profile
# autojoin path.  No wireless name, address, hardware identifier, password,
# keychain item, or raw frame is accepted as an argument or copied into the
# aggregate attestation.  This script never installs, activates, loads, or
# unloads a kext, and never reboots a guest or host.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
POST_PLTI_RUNNER="$ROOT/scripts/run_tahoe_post_plti_trace_runtime.sh"
IDENTITY_CAPTURE="$ROOT/scripts/capture_tahoe_iwn_lab_loaded_identity.py"

PINNED_GUEST="devops@127.0.0.1"
PINNED_PORT=3322
PINNED_GUEST_BUILD="25C56"
PINNED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
PINNED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"

TRACE_TOOL=""
CANDIDATE_RECEIPT=""
OUT_DIR=""
TRACE_CLIENT_SHA256=""
SETTLE_SECONDS=15
ACK_ATTEMPTS=20
RADIO_ATTEMPTS=30
STABLE_READ_DELAY_SECONDS=2

KNOWN_HOSTS=""
declare -a SSH

SOURCE_COMMIT=""
SOURCE_IDENTITY_SHA256=""
SOURCE_IDENTITY_PATHS_COUNT=0
LAB_PROFILE=""
LAB_STAGED_KEXT_REPO_PATH=""
ARCHIVE_SHA256=""
INFO_PLIST_SHA256=""
BUNDLE_TREE_SHA256=""
BINARY_SHA256=""
MACHO_UUID=""
BUNDLE_ID=""

IDENTITY_BEFORE_BOUND=0
IDENTITY_AFTER_BOUND=0
TRACE_CLIENT_PRE_BOUND=0
TRACE_CLIENT_POST_BOUND=0
GENERIC_RUNNER_EXIT=255
GENERIC_RESULT="INCONCLUSIVE"
GENERIC_FAILURE_PHASE="not-run"
GENERIC_RESET_SEQUENCE=0
GENERIC_CAPTURE_GENERATION=0
GENERIC_BACKEND="unknown"
GENERIC_INTEGRITY="inconclusive"
GENERIC_ENTRY_COUNT=0
GENERIC_EPISODE_COUNT=0
GENERIC_DROPPED_ENTRIES=0
GENERIC_VERDICT="INTEGRITY_INCONCLUSIVE"
GENERIC_FIRST_MISSING_STAGE="unknown"
GENERIC_RADIO_OFF=0
GENERIC_RADIO_ON=0
GENERIC_RESET_SYNC=0
GENERIC_INITIAL_SYNC=0
GENERIC_SEAL_ACK=0
GENERIC_FINAL_DISABLED=0
GENERIC_DOUBLE_READ=0
GENERIC_ARMED_WHILE_RADIO_OFF=0
GENERIC_BACKEND_PREFLIGHT_IWN=0

DIRECT_REPORT_ONE_READ=0
DIRECT_REPORT_TWO_READ=0
DIRECT_DOUBLE_READ_STABLE=0
DIRECT_CAPTURE_GENERATION=0
DIRECT_BACKEND="unknown"
DIRECT_ENTRY_COUNT=0
DIRECT_INTEGRITY="inconclusive"
DIRECT_EPISODE_COUNT=0
DIRECT_ACTIVE_EPISODE=0
DIRECT_VERDICT="INTEGRITY_INCONCLUSIVE"
DIRECT_FIRST_MISSING_STAGE="unknown"

RESULT="INCONCLUSIVE"
FAILURE_PHASE="preflight"
FINAL_EXIT=1
ATTESTATION_WRITTEN=0

usage() {
    cat >&2 <<'EOF'
usage: run_tahoe_iwn_direct_sae_runtime.sh \
  --trace-tool /private/tmp/aiam-post-plti-trace-CANDIDATE/airport_itlwm_post_plti_trace \
  --candidate-receipt /local/safe/iwn-lab-candidate-receipt-v2.json \
  --out /fresh/local/evidence/dir \
  [--settle-seconds 1..120] [--ack-attempts 1..60] \
  [--radio-attempts 1..60] [--stable-read-delay-seconds 1..10]

Preconditions deliberately outside this runner:
  * the exact lab candidate passed private admission, transactional activation,
    and the one authorized guest-only reboot;
  * --candidate-receipt is a local v2 receipt for that exact lab artifact and
    its exact executable trace client; the runner derives the digest itself;
  * the trace client named by that receipt was copied beforehand to the
    restricted guest-local path supplied above;
  * the guest already has an authorized saved profile for the laboratory AP.

The runner supplies no wireless identifier or secret.  It delegates exactly
one radio OFF/ON cycle to the bounded post-PLTI runner, which resets the safe
trace while Off before saved-profile autojoin can request a fresh SCAN epoch.
It then requires two identical sealed IWN direct-SAE aggregate reports with
DIRECT_SAE_4WAY_PORT_VALID.  A PASS is limited to that one local driver trace;
it is not a data-plane, rekey, reconnect, roaming, multi-AP, or physical-host
claim.
EOF
}

fail_phase() {
    FAILURE_PHASE="$1"
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

is_bool_token() {
    [ "$1" = 0 ] || [ "$1" = 1 ]
}

valid_trace_tool_path() {
    [[ "$1" =~ ^/private/tmp/aiam-post-plti-trace(-[A-Za-z0-9._-]+)?/airport_itlwm_post_plti_trace$ ]]
}

valid_trace_client_sha256() {
    [[ "$1" =~ ^[0-9a-f]{64}$ ]]
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --trace-tool|--candidate-receipt|--out|--settle-seconds|--ack-attempts|--radio-attempts|--stable-read-delay-seconds)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --trace-tool) TRACE_TOOL="$2" ;;
                --candidate-receipt) CANDIDATE_RECEIPT="$2" ;;
                --out) OUT_DIR="$2" ;;
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

[ -n "$TRACE_TOOL" ] && [ -n "$CANDIDATE_RECEIPT" ] && [ -n "$OUT_DIR" ] || {
    usage
    exit 2
}
valid_trace_tool_path "$TRACE_TOOL" || {
    printf 'ERROR: --trace-tool must use the restricted guest-local private path\n' >&2
    exit 2
}
[ -f "$CANDIDATE_RECEIPT" ] && [ ! -L "$CANDIDATE_RECEIPT" ] || {
    printf 'ERROR: --candidate-receipt must be a regular local file\n' >&2
    exit 2
}
[ -x "$POST_PLTI_RUNNER" ] && [ -x "$IDENTITY_CAPTURE" ] || {
    printf 'ERROR: required runtime or read-only identity helper is unavailable\n' >&2
    exit 2
}
for value_range in \
    "$SETTLE_SECONDS:1:120" "$ACK_ATTEMPTS:1:60" \
    "$RADIO_ATTEMPTS:1:60" "$STABLE_READ_DELAY_SECONDS:1:10"; do
    IFS=: read -r value min max <<<"$value_range"
    is_decimal_in_range "$value" "$min" "$max" || { usage; exit 2; }
done
[ ! -e "$OUT_DIR" ] && [ ! -L "$OUT_DIR" ] || {
    printf 'ERROR: --out must name a fresh path; refusing to overwrite evidence\n' >&2
    exit 2
}

read_candidate_receipt() {
    local -a fields
    mapfile -t fields < <(python3 - "$ROOT/scripts" "$CANDIDATE_RECEIPT" <<'PY'
import re
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from capture_tahoe_iwn_lab_candidate_receipt import (
    load_direct_runtime_candidate_receipt,
)

try:
    candidate = load_direct_runtime_candidate_receipt(Path(sys.argv[2]))
    canonical = (
        "source_commit", "source_identity_sha256", "source_identity_paths_count",
        "profile", "staged_kext_repo_path", "archive_sha256",
        "info_plist_sha256", "binary_sha256", "bundle_tree_sha256",
        "macho_uuid", "bundle_id", "trace_client_sha256",
    )
    if (not isinstance(candidate, dict) or
            not set(canonical).issubset(candidate)):
        raise ValueError("direct-runtime receipt candidate fields")
    if re.fullmatch(r"[0-9a-f]{40}", str(candidate["source_commit"])) is None:
        raise ValueError("direct-runtime receipt source commit")
    for key in (
        "source_identity_sha256", "archive_sha256", "info_plist_sha256",
        "binary_sha256", "bundle_tree_sha256", "trace_client_sha256",
    ):
        if re.fullmatch(r"[0-9a-f]{64}", str(candidate[key])) is None:
            raise ValueError(f"direct-runtime receipt {key}")
    if (type(candidate["source_identity_paths_count"]) is not int or
            candidate["source_identity_paths_count"] < 1):
        raise ValueError("direct-runtime receipt source identity path count")
    if candidate["profile"] != "iwn-software-pmf-lab":
        raise ValueError("direct-runtime receipt profile")
    if candidate["staged_kext_repo_path"] != (
            "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext"):
        raise ValueError("direct-runtime receipt staged kext path")
    if candidate["bundle_id"] != "com.zxystd.AirportItlwm":
        raise ValueError("direct-runtime receipt bundle identifier")
    if re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                    str(candidate["macho_uuid"])) is None:
        raise ValueError("direct-runtime receipt Mach-O UUID")
except Exception as exc:
    raise SystemExit(f"candidate receipt rejected: {exc}")

for key in canonical:
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
    local label="$1" values
    python3 "$IDENTITY_CAPTURE" --candidate-receipt "$CANDIDATE_RECEIPT" \
        --output "$OUT_DIR/identity-$label.json" \
        >"$OUT_DIR/identity-$label.stdout" \
        2>"$OUT_DIR/identity-$label.stderr" || return 1
    values="$(python3 - "$OUT_DIR/identity-$label.json" <<'PY'
import json
import re
import sys
from pathlib import Path

EXPECTED_FIELDS = {
    "source_commit", "source_identity_sha256", "source_identity_paths_count",
    "profile", "staged_kext_repo_path", "archive_sha256", "info_plist_sha256",
    "binary_sha256", "bundle_tree_sha256", "macho_uuid", "bundle_id",
    "trace_client_sha256",
}


def reject_duplicate_keys(pairs):
    document = {}
    for key, value in pairs:
        if key in document:
            raise ValueError("duplicate JSON key")
        document[key] = value
    return document


try:
    evidence = json.loads(
        Path(sys.argv[1]).read_text(encoding="utf-8"),
        object_pairs_hook=reject_duplicate_keys,
    )
    if (not isinstance(evidence, dict) or evidence.get("schema_version") !=
            "itlwm-tahoe-iwn-lab-loaded-identity/v1"):
        raise ValueError("loaded identity schema")
    candidate = evidence.get("expected_local_lab_candidate")
    if not isinstance(candidate, dict) or set(candidate) != EXPECTED_FIELDS:
        raise ValueError("loaded identity candidate fields")
    binding = evidence.get("candidate_binding")
    if (not isinstance(binding, dict) or
            binding.get("candidate_kext_bound") is not True):
        raise ValueError("loaded identity candidate binding")
    checks = binding.get("checks")
    if (not isinstance(checks, dict) or not checks or
            not all(value is True for value in checks.values())):
        raise ValueError("loaded identity candidate binding checks")
    verdict = evidence.get("verdict")
    if (not isinstance(verdict, dict) or verdict.get(
            "ready_for_exact_local_lab_candidate_runtime_experiment") is not True):
        raise ValueError("loaded identity readiness verdict")
    if re.fullmatch(r"[0-9a-f]{40}", str(candidate["source_commit"])) is None:
        raise ValueError("loaded identity source commit")
    for key in (
        "source_identity_sha256", "archive_sha256", "info_plist_sha256",
        "binary_sha256", "bundle_tree_sha256", "trace_client_sha256",
    ):
        if re.fullmatch(r"[0-9a-f]{64}", str(candidate[key])) is None:
            raise ValueError(f"loaded identity {key}")
    if (type(candidate["source_identity_paths_count"]) is not int or
            candidate["source_identity_paths_count"] < 1):
        raise ValueError("loaded identity source identity path count")
    if candidate["profile"] != "iwn-software-pmf-lab":
        raise ValueError("loaded identity profile")
    if candidate["staged_kext_repo_path"] != (
            "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext"):
        raise ValueError("loaded identity staged kext path")
    if candidate["bundle_id"] != "com.zxystd.AirportItlwm":
        raise ValueError("loaded identity bundle identifier")
    if re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}",
                    str(candidate["macho_uuid"])) is None:
        raise ValueError("loaded identity Mach-O UUID")
except Exception as exc:
    raise SystemExit(f"loaded identity rejected: {exc}")

for key in (
    "source_commit", "source_identity_sha256", "source_identity_paths_count",
    "profile", "staged_kext_repo_path", "archive_sha256", "info_plist_sha256",
    "binary_sha256", "bundle_tree_sha256", "macho_uuid", "bundle_id",
    "trace_client_sha256",
):
    print(candidate[key])
PY
)" || return 1
    local -a identity_values expected_values
    mapfile -t identity_values <<<"$values"
    [ "${#identity_values[@]}" -eq 12 ] || return 1
    expected_values=(
        "$SOURCE_COMMIT" "$SOURCE_IDENTITY_SHA256"
        "$SOURCE_IDENTITY_PATHS_COUNT" "$LAB_PROFILE"
        "$LAB_STAGED_KEXT_REPO_PATH" "$ARCHIVE_SHA256"
        "$INFO_PLIST_SHA256" "$BINARY_SHA256" "$BUNDLE_TREE_SHA256"
        "$MACHO_UUID" "$BUNDLE_ID" "$TRACE_CLIENT_SHA256"
    )
    local index
    for index in "${!expected_values[@]}"; do
        [ "${identity_values[$index]}" = "${expected_values[$index]}" ] || return 1
    done
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

read_generic_attestation() {
    local path="$OUT_DIR/post-plti/runtime-attestation.json" values
    [ -f "$path" ] || return 1
    values="$(python3 - "$path" "$SOURCE_COMMIT" "$SOURCE_IDENTITY_SHA256" \
        "$SOURCE_IDENTITY_PATHS_COUNT" "$LAB_PROFILE" \
        "$LAB_STAGED_KEXT_REPO_PATH" "$ARCHIVE_SHA256" \
        "$INFO_PLIST_SHA256" "$BINARY_SHA256" "$BUNDLE_TREE_SHA256" \
        "$MACHO_UUID" "$BUNDLE_ID" "$TRACE_CLIENT_SHA256" <<'PY'
import json
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
expected = list(sys.argv[2:])


def reject_duplicate_keys(pairs):
    document = {}
    for key, value in pairs:
        if key in document:
            raise ValueError("duplicate JSON key")
        document[key] = value
    return document


try:
    if (len(expected) != 12 or not expected[2].isdecimal() or
            int(expected[2]) < 1):
        raise ValueError("candidate source identity path count")
    # The receipt reader crosses this value through Bash, while the delegated
    # v4 JSON attestation intentionally carries it as a JSON number. Normalize
    # before the exact candidate comparison so a valid trace is not rejected
    # solely for a string-versus-integer representation difference.
    expected[2] = int(expected[2])
    data = json.loads(
        path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
    )
    if data.get("schema") != "itlwm-tahoe-post-plti-trace-runtime/v4":
        raise ValueError("schema")
    candidate = data.get("candidate")
    expected_keys = (
        "source_commit", "source_identity_sha256", "source_identity_paths_count",
        "profile", "staged_kext_repo_path", "archive_sha256",
        "info_plist_sha256", "binary_sha256", "bundle_tree_sha256",
        "macho_uuid", "bundle_id", "trace_client_sha256",
    )
    if (not isinstance(candidate, dict) or candidate.get("kind") !=
            "local-unpublished-iwn-lab-candidate"):
        raise ValueError("candidate kind")
    if set(candidate) != set(expected_keys) | {
        "kind", "identity_binding_precondition",
        "trace_client_receipt_binding_precondition",
    }:
        raise ValueError("candidate fields")
    for key, value in zip(expected_keys, expected):
        if candidate.get(key) != value:
            raise ValueError("candidate identity")
    if candidate.get("identity_binding_precondition") != "PASS":
        raise ValueError("candidate binding")
    if candidate.get("trace_client_receipt_binding_precondition") != "PASS":
        raise ValueError("trace-client receipt binding")
    radio = data.get("radio_cycle")
    trace = data.get("trace")
    if not isinstance(radio, dict) or not isinstance(trace, dict):
        raise ValueError("radio/trace")
    scalars = ("reset_control_sequence", "capture_generation", "entry_count", "episode_count", "dropped_entries")
    for key in scalars:
        if type(trace.get(key)) is not int or not 0 <= trace[key] <= 4294967295:
            raise ValueError(key)
    for key in ("radio_off_observed", "radio_on_observed", "trace_armed_while_radio_off"):
        if not isinstance(radio.get(key), bool):
            raise ValueError(key)
    for key in ("reset_ack_generation_synchronized", "initial_snapshot_buffer_generation_synchronized", "seal_control_acknowledged", "final_control_disabled", "double_read_stable", "backend_preflight_iwn"):
        if not isinstance(trace.get(key), bool):
            raise ValueError(key)
    if trace.get("backend") not in {"iwn", "iwx", "unsupported", "unknown"}:
        raise ValueError("backend")
    if trace.get("integrity") not in {"ok", "inconclusive"}:
        raise ValueError("integrity")
    if not isinstance(trace.get("verdict"), str) or not isinstance(trace.get("first_missing_stage"), str):
        raise ValueError("trace verdict")
    if data.get("result") not in {"PASS", "INCONCLUSIVE"}:
        raise ValueError("result")
    if not isinstance(data.get("failure_phase"), str):
        raise ValueError("failure phase")
except Exception:
    raise SystemExit(1)

print(data["result"])
print(data["failure_phase"])
print(trace["reset_control_sequence"])
print(trace["capture_generation"])
print(trace["backend"])
print(trace["integrity"])
print(trace["entry_count"])
print(trace["episode_count"])
print(trace["dropped_entries"])
print(trace["verdict"])
print(trace["first_missing_stage"])
print(int(radio["radio_off_observed"]))
print(int(radio["radio_on_observed"]))
print(int(trace["reset_ack_generation_synchronized"]))
print(int(trace["initial_snapshot_buffer_generation_synchronized"]))
print(int(trace["seal_control_acknowledged"]))
print(int(trace["final_control_disabled"]))
print(int(trace["double_read_stable"]))
print(int(radio["trace_armed_while_radio_off"]))
print(int(trace["backend_preflight_iwn"]))
PY
)" || return 1
    local -a fields
    mapfile -t fields <<<"$values"
    [ "${#fields[@]}" -eq 20 ] || return 1
    GENERIC_RESULT="${fields[0]}"
    GENERIC_FAILURE_PHASE="${fields[1]}"
    GENERIC_RESET_SEQUENCE="${fields[2]}"
    GENERIC_CAPTURE_GENERATION="${fields[3]}"
    GENERIC_BACKEND="${fields[4]}"
    GENERIC_INTEGRITY="${fields[5]}"
    GENERIC_ENTRY_COUNT="${fields[6]}"
    GENERIC_EPISODE_COUNT="${fields[7]}"
    GENERIC_DROPPED_ENTRIES="${fields[8]}"
    GENERIC_VERDICT="${fields[9]}"
    GENERIC_FIRST_MISSING_STAGE="${fields[10]}"
    GENERIC_RADIO_OFF="${fields[11]}"
    GENERIC_RADIO_ON="${fields[12]}"
    GENERIC_RESET_SYNC="${fields[13]}"
    GENERIC_INITIAL_SYNC="${fields[14]}"
    GENERIC_SEAL_ACK="${fields[15]}"
    GENERIC_FINAL_DISABLED="${fields[16]}"
    GENERIC_DOUBLE_READ="${fields[17]}"
    GENERIC_ARMED_WHILE_RADIO_OFF="${fields[18]}"
    GENERIC_BACKEND_PREFLIGHT_IWN="${fields[19]}"
    for value in "$GENERIC_RESET_SEQUENCE" "$GENERIC_CAPTURE_GENERATION" \
        "$GENERIC_ENTRY_COUNT" "$GENERIC_EPISODE_COUNT" "$GENERIC_DROPPED_ENTRIES"; do
        is_u32 "$value" || return 1
    done
    for value in "$GENERIC_RADIO_OFF" "$GENERIC_RADIO_ON" "$GENERIC_RESET_SYNC" \
        "$GENERIC_INITIAL_SYNC" "$GENERIC_SEAL_ACK" "$GENERIC_FINAL_DISABLED" \
        "$GENERIC_DOUBLE_READ" "$GENERIC_ARMED_WHILE_RADIO_OFF" \
        "$GENERIC_BACKEND_PREFLIGHT_IWN"; do
        is_bool_token "$value" || return 1
    done
}

capture_direct_report() {
    local label="$1"
    remote_trace get iwn-direct-sae-report >"$OUT_DIR/$label.stdout" 2>"$OUT_DIR/$label.stderr"
}

read_direct_report() {
    local path="$1"
    DIRECT_CAPTURE_GENERATION="$(extract_token "$path" capture_generation)" || return 1
    DIRECT_BACKEND="$(extract_token "$path" backend)" || return 1
    DIRECT_ENTRY_COUNT="$(extract_token "$path" entries)" || return 1
    DIRECT_INTEGRITY="$(extract_token "$path" integrity)" || return 1
    DIRECT_EPISODE_COUNT="$(extract_token "$path" episode_count)" || return 1
    DIRECT_ACTIVE_EPISODE="$(extract_token "$path" active_episode)" || return 1
    DIRECT_VERDICT="$(extract_token "$path" iwn_direct_sae_verdict)" || return 1
    DIRECT_FIRST_MISSING_STAGE="$(extract_token "$path" first_missing_stage)" || return 1
    for value in "$DIRECT_CAPTURE_GENERATION" "$DIRECT_ENTRY_COUNT" \
        "$DIRECT_EPISODE_COUNT" "$DIRECT_ACTIVE_EPISODE"; do
        is_u32 "$value" || return 1
    done
    [ "$DIRECT_BACKEND" = IWN ] || return 1
    case "$DIRECT_INTEGRITY" in ok|inconclusive) ;; *) return 1;; esac
    case "$DIRECT_VERDICT" in
        DIRECT_SAE_4WAY_PORT_VALID|BRANCH_NOT_OBSERVED|FRESH_SCAN_NOT_OBSERVED|REQUEST_NO_BSS_SELECTION|JOIN_BSS_NOT_OBSERVED|NODE_MFP_NOT_NEGOTIATED|AUTH_STATE_NOT_OBSERVED|COMMIT_TX_NOT_COMPLETE|PEER_COMMIT_NOT_ACCEPTED|CONFIRM_TX_NOT_COMPLETE|PEER_CONFIRM_NOT_VALIDATED|PMK_NOT_CLAIMED|ASSOC_DESCRIPTOR_NOT_ACCEPTED|ASSOC_EXCHANGE_NOT_COMPLETE|FOUR_WAY_NOT_COMPLETE|PMF_PTK_SOFTWARE_CCMP_NOT_OBSERVED|PMF_GTK_SOFTWARE_CCMP_NOT_OBSERVED|PMF_IGTK_STAGE_NOT_OBSERVED|PMF_IGTK_PUBLICATION_NOT_OBSERVED|PMF_KEYSET_PUBLICATION_NOT_OBSERVED|BACKEND_UNSUPPORTED|INTEGRITY_INCONCLUSIVE) ;;
        *) return 1 ;;
    esac
    [[ "$DIRECT_FIRST_MISSING_STAGE" =~ ^[a-z0-9-]+$ ]] || return 1
}

delegated_fresh_scan_lifecycle_is_complete() {
    [ "$GENERIC_RUNNER_EXIT" = 0 ] &&
        {
            { [ "$GENERIC_RESULT" = PASS ] && [ "$GENERIC_FAILURE_PHASE" = none ]; } ||
            { [ "$GENERIC_RESULT" = INCONCLUSIVE ] && [ "$GENERIC_FAILURE_PHASE" = trace-verdict-diagnostic ]; }
        } &&
        [ "$GENERIC_BACKEND" = iwn ] &&
        [ "$GENERIC_INTEGRITY" = ok ] &&
        [ "$GENERIC_RESET_SEQUENCE" -gt 0 ] &&
        [ "$GENERIC_CAPTURE_GENERATION" -gt 0 ] &&
        [ "$GENERIC_ENTRY_COUNT" -gt 0 ] &&
        [ "$GENERIC_EPISODE_COUNT" = 1 ] &&
        [ "$GENERIC_DROPPED_ENTRIES" = 0 ] &&
        [ "$GENERIC_RADIO_OFF" = 1 ] && [ "$GENERIC_RADIO_ON" = 1 ] &&
        [ "$GENERIC_RESET_SYNC" = 1 ] && [ "$GENERIC_INITIAL_SYNC" = 1 ] &&
        [ "$GENERIC_SEAL_ACK" = 1 ] && [ "$GENERIC_FINAL_DISABLED" = 1 ] &&
        [ "$GENERIC_DOUBLE_READ" = 1 ] &&
        [ "$GENERIC_ARMED_WHILE_RADIO_OFF" = 1 ] &&
        [ "$GENERIC_BACKEND_PREFLIGHT_IWN" = 1 ]
}

direct_chain_is_positive() {
    [ "$DIRECT_REPORT_ONE_READ" = 1 ] &&
        [ "$DIRECT_REPORT_TWO_READ" = 1 ] &&
        [ "$DIRECT_DOUBLE_READ_STABLE" = 1 ] &&
        [ "$DIRECT_CAPTURE_GENERATION" = "$GENERIC_CAPTURE_GENERATION" ] &&
        [ "$DIRECT_BACKEND" = IWN ] &&
        [ "$DIRECT_ENTRY_COUNT" = "$GENERIC_ENTRY_COUNT" ] &&
        [ "$DIRECT_ENTRY_COUNT" -gt 0 ] &&
        [ "$DIRECT_INTEGRITY" = ok ] &&
        [ "$DIRECT_EPISODE_COUNT" = 1 ] && [ "$DIRECT_ACTIVE_EPISODE" = 0 ] &&
        [ "$DIRECT_VERDICT" = DIRECT_SAE_4WAY_PORT_VALID ] &&
        [ "$DIRECT_FIRST_MISSING_STAGE" = none ]
}

write_safe_attestation() {
    [ "$ATTESTATION_WRITTEN" -eq 0 ] || return 0
    [ -n "$OUT_DIR" ] && [ -d "$OUT_DIR" ] || return 0
    python3 - "$OUT_DIR/runtime-attestation.json" \
        "$SOURCE_COMMIT" "$SOURCE_IDENTITY_SHA256" \
        "$SOURCE_IDENTITY_PATHS_COUNT" "$LAB_PROFILE" \
        "$LAB_STAGED_KEXT_REPO_PATH" "$ARCHIVE_SHA256" \
        "$INFO_PLIST_SHA256" "$BUNDLE_TREE_SHA256" "$BINARY_SHA256" \
        "$MACHO_UUID" "$BUNDLE_ID" "$TRACE_CLIENT_SHA256" \
        "$IDENTITY_BEFORE_BOUND" "$IDENTITY_AFTER_BOUND" \
        "$TRACE_CLIENT_PRE_BOUND" "$TRACE_CLIENT_POST_BOUND" \
        "$GENERIC_RUNNER_EXIT" "$GENERIC_RESULT" "$GENERIC_FAILURE_PHASE" \
        "$GENERIC_RESET_SEQUENCE" "$GENERIC_CAPTURE_GENERATION" "$GENERIC_BACKEND" \
        "$GENERIC_INTEGRITY" "$GENERIC_ENTRY_COUNT" "$GENERIC_EPISODE_COUNT" \
        "$GENERIC_DROPPED_ENTRIES" "$GENERIC_VERDICT" "$GENERIC_FIRST_MISSING_STAGE" \
        "$GENERIC_RADIO_OFF" "$GENERIC_RADIO_ON" "$GENERIC_RESET_SYNC" \
        "$GENERIC_INITIAL_SYNC" "$GENERIC_SEAL_ACK" "$GENERIC_FINAL_DISABLED" \
        "$GENERIC_DOUBLE_READ" "$GENERIC_ARMED_WHILE_RADIO_OFF" \
        "$GENERIC_BACKEND_PREFLIGHT_IWN" "$DIRECT_REPORT_ONE_READ" \
        "$DIRECT_REPORT_TWO_READ" "$DIRECT_DOUBLE_READ_STABLE" \
        "$DIRECT_CAPTURE_GENERATION" "$DIRECT_BACKEND" "$DIRECT_ENTRY_COUNT" \
        "$DIRECT_INTEGRITY" "$DIRECT_EPISODE_COUNT" "$DIRECT_ACTIVE_EPISODE" \
        "$DIRECT_VERDICT" "$DIRECT_FIRST_MISSING_STAGE" "$RESULT" "$FAILURE_PHASE" <<'PY'
import json
import re
import sys
from pathlib import Path

(
    output, source_commit, source_identity, source_identity_paths_count,
    profile, staged_kext_repo_path, archive_sha256, info_plist_sha256,
    bundle_tree_sha256, binary_sha256, macho_uuid, bundle_id,
    trace_client_sha256, identity_before, identity_after, client_pre,
    client_post, generic_exit, generic_result, generic_failure, reset_sequence,
    generic_generation, generic_backend, generic_integrity, generic_entries,
    generic_episodes, generic_dropped, generic_verdict, generic_missing,
    radio_off, radio_on, reset_sync, initial_sync, seal_ack, final_disabled,
    generic_double_read, armed_while_off, backend_preflight_iwn,
    direct_read_one, direct_read_two, direct_double_read, direct_generation,
    direct_backend, direct_entries, direct_integrity, direct_episodes,
    direct_active, direct_verdict, direct_missing, result, failure_phase,
) = sys.argv[1:]

def b(value: str) -> bool:
    return value == "1"

def integer(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError:
        return 0
    return parsed if 0 <= parsed <= 4294967295 else 0

def digest(value: str, width: int) -> str:
    return value if re.fullmatch(rf"[0-9a-f]{{{width}}}", value) else ""

candidate = {
    "kind": "local-unpublished-iwn-lab-candidate",
    "source_commit": digest(source_commit, 40),
    "source_identity_sha256": digest(source_identity, 64),
    "source_identity_paths_count": integer(source_identity_paths_count),
    "profile": profile if profile == "iwn-software-pmf-lab" else "",
    "staged_kext_repo_path": staged_kext_repo_path if staged_kext_repo_path ==
        "Build/Debug/Tahoe-IwnSoftwarePmfLab/AirportItlwm.kext" else "",
    "archive_sha256": digest(archive_sha256, 64),
    "info_plist_sha256": digest(info_plist_sha256, 64),
    "bundle_tree_sha256": digest(bundle_tree_sha256, 64),
    "binary_sha256": digest(binary_sha256, 64),
    "macho_uuid": macho_uuid if re.fullmatch(
        r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", macho_uuid
    ) else "",
    "bundle_id": bundle_id if bundle_id == "com.zxystd.AirportItlwm" else "",
    "trace_client_sha256": digest(trace_client_sha256, 64),
    "identity_before_bound": b(identity_before),
    "identity_after_bound": b(identity_after),
    "trace_client_pre_bound": b(client_pre),
    "trace_client_post_bound": b(client_post),
}
generic = {
    "delegated_runner_exit": integer(generic_exit),
    "result": generic_result,
    "failure_phase": generic_failure,
    "reset_control_sequence": integer(reset_sequence),
    "capture_generation": integer(generic_generation),
    "backend": generic_backend,
    "integrity": generic_integrity,
    "entry_count": integer(generic_entries),
    "episode_count": integer(generic_episodes),
    "dropped_entries": integer(generic_dropped),
    "verdict": generic_verdict,
    "first_missing_stage": generic_missing,
    "radio_off_observed": b(radio_off),
    "radio_on_observed": b(radio_on),
    "reset_ack_generation_synchronized": b(reset_sync),
    "initial_snapshot_buffer_generation_synchronized": b(initial_sync),
    "seal_control_acknowledged": b(seal_ack),
    "final_control_disabled": b(final_disabled),
    "double_read_stable": b(generic_double_read),
    "trace_armed_while_radio_off": b(armed_while_off),
    "backend_preflight_iwn": b(backend_preflight_iwn),
}
direct = {
    "report_one_read": b(direct_read_one),
    "report_two_read": b(direct_read_two),
    "double_read_stable": b(direct_double_read),
    "capture_generation": integer(direct_generation),
    "backend": direct_backend,
    "entry_count": integer(direct_entries),
    "integrity": direct_integrity,
    "episode_count": integer(direct_episodes),
    "active_episode": integer(direct_active),
    "verdict": direct_verdict,
    "first_missing_stage": direct_missing,
}
positive = (
    result == "PASS"
    and (
        (generic["result"] == "PASS" and generic["failure_phase"] == "none")
        or (generic["result"] == "INCONCLUSIVE"
            and generic["failure_phase"] == "trace-verdict-diagnostic")
    )
    and generic["backend"] == "iwn"
    and generic["integrity"] == "ok"
    and generic["capture_generation"] > 0
    and generic["entry_count"] > 0
    and generic["episode_count"] == 1
    and generic["dropped_entries"] == 0
    and all(generic[key] is True for key in (
        "radio_off_observed", "radio_on_observed",
        "reset_ack_generation_synchronized",
        "initial_snapshot_buffer_generation_synchronized",
        "seal_control_acknowledged", "final_control_disabled", "double_read_stable",
        "trace_armed_while_radio_off", "backend_preflight_iwn",
    ))
    and all(direct[key] is True for key in (
        "report_one_read", "report_two_read", "double_read_stable",
    ))
    and direct["capture_generation"] == generic["capture_generation"]
    and direct["backend"] == "IWN"
    and direct["entry_count"] == generic["entry_count"]
    and direct["integrity"] == "ok"
    and direct["episode_count"] == 1
    and direct["active_episode"] == 0
    and direct["verdict"] == "DIRECT_SAE_4WAY_PORT_VALID"
    and direct["first_missing_stage"] == "none"
    and all(candidate[key] is True for key in (
        "identity_before_bound", "identity_after_bound",
        "trace_client_pre_bound", "trace_client_post_bound",
    ))
)
document = {
    "schema": "itlwm-tahoe-iwn-direct-sae-runtime/v2",
    "candidate": candidate,
    "scope": {
        "environment": "pinned_disposable_qemu_guest",
        "physical_host_touched": False,
        "physical_host_rebooted": False,
        "guest_rebooted_by_runner": False,
        "wireless_identity_collected": False,
        "network_secret_collected": False,
    },
    "wcl_trigger": {
        "requested_cycles": 1,
        "connection_trigger": "saved_profile_autojoin_only",
        "secret_argument": "none",
        "fresh_scan_state": "delegated_radio_off_on_trace_reset_while_off",
        "explicit_join_command": False,
        "explicit_scan_command": False,
        "explicit_profile_command": False,
        "explicit_route_command": False,
        "explicit_address_command": False,
        "explicit_dhcp_state_mutating_command": False,
    },
    "generic_trace": generic,
    "iwn_direct_sae_trace": direct,
    "result": "PASS" if positive else "INCONCLUSIVE",
    "failure_phase": "none" if positive else failure_phase,
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
        "application or data-plane traffic verification",
        "group rekey, reconnect, roaming, or multi-AP replacement",
        "physical-host validation",
        "proof beyond one sealed direct-SAE four-way port-valid trace",
    ],
}
Path(output).write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
PY
    ATTESTATION_WRITTEN=1
}

cleanup() {
    local rc=$?
    trap - EXIT HUP INT TERM
    set +e
    # The delegated runner owns both radio recovery and trace disarm.  This
    # wrapper only queries its frozen aggregate after that runner returned.
    # A retry here is deliberately read-only: first re-bind the exact loaded
    # candidate, then re-check the already receipt-bound trace client.
    if [ "$IDENTITY_BEFORE_BOUND" -eq 1 ] && [ "$IDENTITY_AFTER_BOUND" -eq 0 ]; then
        capture_identity after || true
    fi
    if [ "$TRACE_CLIENT_PRE_BOUND" -eq 1 ] && [ "$TRACE_CLIENT_POST_BOUND" -eq 0 ] && \
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
    printf 'ERROR: --candidate-receipt is not a valid direct-runtime v2 receipt\n' >&2
    exit 2
}
umask 077
mkdir -p "$OUT_DIR"
chmod 700 "$OUT_DIR"

KNOWN_HOSTS="$(mktemp /tmp/aiam-iwn-direct-sae-known-hosts.XXXXXX)"
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

# The delegated runner is the only network-control owner.  In strict mode it
# proves IWN first, then resets the final trace while the radio is Off, so the
# sole saved-profile autojoin is causally after a fresh SCAN state.
set +e
"$POST_PLTI_RUNNER" --trace-tool "$TRACE_TOOL" \
    --lab-identity-evidence "$OUT_DIR/identity-before.json" \
    --trace-client-sha256 "$TRACE_CLIENT_SHA256" \
    --out "$OUT_DIR/post-plti" --arm-while-radio-off \
    --settle-seconds "$SETTLE_SECONDS" --ack-attempts "$ACK_ATTEMPTS" \
    --radio-attempts "$RADIO_ATTEMPTS" \
    --stable-read-delay-seconds "$STABLE_READ_DELAY_SECONDS" \
    >"$OUT_DIR/delegated-runner.stdout" 2>"$OUT_DIR/delegated-runner.stderr"
GENERIC_RUNNER_EXIT=$?
set -e
read_generic_attestation || fail_phase delegated-runner-attestation
[ "$GENERIC_RUNNER_EXIT" = 0 ] || fail_phase delegated-runner-failed

capture_direct_report direct-sae-report-read-1 || fail_phase iwn-direct-sae-report-first-read
read_direct_report "$OUT_DIR/direct-sae-report-read-1.stdout" ||
    fail_phase iwn-direct-sae-report-first-parse
DIRECT_REPORT_ONE_READ=1
sleep "$STABLE_READ_DELAY_SECONDS"
capture_direct_report direct-sae-report-read-2 || fail_phase iwn-direct-sae-report-second-read
read_direct_report "$OUT_DIR/direct-sae-report-read-2.stdout" ||
    fail_phase iwn-direct-sae-report-second-parse
DIRECT_REPORT_TWO_READ=1
cmp -s "$OUT_DIR/direct-sae-report-read-1.stdout" \
    "$OUT_DIR/direct-sae-report-read-2.stdout" ||
    fail_phase iwn-direct-sae-report-double-read-unstable
DIRECT_DOUBLE_READ_STABLE=1

capture_identity after || fail_phase candidate-identity-after
remote_trace_client_exists || fail_phase trace-client-postflight
TRACE_CLIENT_POST_BOUND=1

if delegated_fresh_scan_lifecycle_is_complete && direct_chain_is_positive; then
    RESULT="PASS"
    FAILURE_PHASE="none"
    FINAL_EXIT=0
    printf 'PASS: one sealed direct IWN SAE four-way port-valid trace observed\n'
else
    RESULT="INCONCLUSIVE"
    FAILURE_PHASE="trace-verdict-diagnostic"
    FINAL_EXIT=0
    printf 'INCONCLUSIVE: sealed direct IWN SAE aggregate retained as local-only diagnostic evidence\n'
fi
exit "$FINAL_EXIT"
