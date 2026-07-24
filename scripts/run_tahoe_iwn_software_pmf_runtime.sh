#!/usr/bin/env bash
# Run one bounded, provenance-bound IWN software-PMF ownership experiment.
#
# This wrapper deliberately delegates the radio stimulus, trace lifecycle,
# and generic association-chain classification to the existing post-PLTI
# runner.  It adds the IWN-specific sealed report required to tell a real
# software CCMP+BIP publication from a merely successful association chain.
# It never activates a kext, reboots a guest, joins a network explicitly,
# enumerates profiles, or changes AP, route, address, or DHCP state.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
POST_PLTI_RUNNER="$ROOT/scripts/run_tahoe_post_plti_trace_runtime.sh"
IDENTITY_CAPTURE="$ROOT/scripts/capture_tahoe_lab_kext_identity.py"

PINNED_GUEST="devops@127.0.0.1"
PINNED_PORT=3322
PINNED_GUEST_BUILD="25C56"
PINNED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
PINNED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"

TRACE_TOOL=""
RELEASE_ZIP=""
CANDIDATE_PROVENANCE=""
OUT_DIR=""
SETTLE_SECONDS=15
ACK_ATTEMPTS=20
RADIO_ATTEMPTS=30
STABLE_READ_DELAY_SECONDS=2

KNOWN_HOSTS=""
declare -a SSH

SOURCE_COMMIT=""
SOURCE_IDENTITY_SHA256=""
RELEASE_TAG=""
ARCHIVE_SHA256=""
BINARY_SHA256=""
MACHO_UUID=""
TRACE_CLIENT_SHA256=""

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

PMF_REPORT_ONE_READ=0
PMF_REPORT_TWO_READ=0
PMF_DOUBLE_READ_STABLE=0
PMF_CAPTURE_GENERATION=0
PMF_BACKEND="unknown"
PMF_ENTRY_COUNT=0
PMF_INTEGRITY="inconclusive"
PMF_EPISODE_COUNT=0
PMF_ACTIVE_EPISODE=0
PMF_VERDICT="INTEGRITY_INCONCLUSIVE"
PMF_FIRST_MISSING_STAGE="unknown"

RESULT="INCONCLUSIVE"
FAILURE_PHASE="preflight"
FINAL_EXIT=1
ATTESTATION_WRITTEN=0

usage() {
    cat >&2 <<'EOF'
usage: run_tahoe_iwn_software_pmf_runtime.sh \
  --trace-tool /private/tmp/aiam-post-plti-trace-CANDIDATE/airport_itlwm_post_plti_trace \
  --release-zip /local/safe/AirportItlwm-Tahoe-IwnSoftwarePmfLab.kext.zip \
  --candidate-provenance /local/safe/tahoe_candidate_provenance.json \
  --out /fresh/local/evidence/dir \
  [--settle-seconds 1..120] [--ack-attempts 1..60] \
  [--radio-attempts 1..60] [--stable-read-delay-seconds 1..10]

Preconditions deliberately outside this runner:
  * the exact lab candidate passed private AuxKC admission, transactional
    guest-only activation, and a guest-only reboot;
  * the trace client is bound in the v2 candidate provenance and copied to
    the supplied restricted guest-local path;
  * the guest already has an authorized saved WPA2+PMF profile for a
    receivable laboratory AP.

The only wireless stimulus is the delegated single radio OFF/ON transition
with saved-profile autojoin.  A PASS means one sealed IWN software-PMF local
ownership chain and one sealed generic kernel chain were observed.  It does
not claim SAE/WPA3, protected-MPDU interoperability, traffic, rekey, roaming,
or physical-host validation.
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

while [ "$#" -gt 0 ]; do
    case "$1" in
        --trace-tool|--release-zip|--candidate-provenance|--out|--settle-seconds|--ack-attempts|--radio-attempts|--stable-read-delay-seconds)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --trace-tool) TRACE_TOOL="$2" ;;
                --release-zip) RELEASE_ZIP="$2" ;;
                --candidate-provenance) CANDIDATE_PROVENANCE="$2" ;;
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
[ -x "$POST_PLTI_RUNNER" ] && [ -x "$IDENTITY_CAPTURE" ] || {
    printf 'ERROR: required runtime/identity helper is unavailable\n' >&2
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

trace_client_sha256_from_provenance() {
    python3 - "$ROOT/scripts" "$CANDIDATE_PROVENANCE" <<'PY'
import sys

sys.path.insert(0, sys.argv[1])
from create_tahoe_candidate_provenance import (
    trace_client_sha256_from_candidate_provenance,
)

print(trace_client_sha256_from_candidate_provenance(sys.argv[2]))
PY
}

TRACE_CLIENT_SHA256="$(trace_client_sha256_from_provenance)" || {
    printf 'ERROR: --candidate-provenance lacks a valid trace-client receipt\n' >&2
    exit 2
}
[[ "$TRACE_CLIENT_SHA256" =~ ^[0-9a-f]{64}$ ]] || {
    printf 'ERROR: trace-client receipt digest is malformed\n' >&2
    exit 2
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
if not isinstance(checks, dict) or not checks or not all(value is True for value in checks.values()):
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
if re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?", release["release_tag"]) is None:
    raise SystemExit(1)
for key in ("archive_sha256", "binary_sha256"):
    if re.fullmatch(r"[0-9a-f]{64}", release[key]) is None:
        raise SystemExit(1)
if re.fullmatch(r"[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}", release["macho_uuid"]) is None:
    raise SystemExit(1)
print("\n".join(release[key] for key in fields))
PY
)" || return 1
    local -a identity_values
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
        return 0
    fi
    [ "${identity_values[0]}" = "$SOURCE_COMMIT" ] &&
        [ "${identity_values[1]}" = "$SOURCE_IDENTITY_SHA256" ] &&
        [ "${identity_values[2]}" = "$RELEASE_TAG" ] &&
        [ "${identity_values[3]}" = "$ARCHIVE_SHA256" ] &&
        [ "${identity_values[4]}" = "$BINARY_SHA256" ] &&
        [ "${identity_values[5]}" = "$MACHO_UUID" ] || return 1
    IDENTITY_AFTER_BOUND=1
}

remote_trace() {
    "${SSH[@]}" /bin/bash -s -- "$TRACE_TOOL" "$TRACE_CLIENT_SHA256" "$@" <<'REMOTE'
set -euo pipefail
tool="$1"
expected_sha256="$2"
shift 2
trace_client_binding() {
    local tool="$1" expected_sha256="$2" parent physical_parent observed
    case "$tool" in
        /private/tmp/aiam-post-plti-trace/airport_itlwm_post_plti_trace|/private/tmp/aiam-post-plti-trace-*/airport_itlwm_post_plti_trace) ;;
        *) return 64 ;;
    esac
    [[ "$expected_sha256" =~ ^[0-9a-f]{64}$ ]] || return 65
    parent="${tool%/airport_itlwm_post_plti_trace}"
    test -d "$parent" && test ! -L "$parent" || return 65
    physical_parent="$(CDPATH= cd -P -- "$parent" && pwd -P)" || return 65
    case "$physical_parent" in
        /private/tmp/aiam-post-plti-trace|/private/tmp/aiam-post-plti-trace-*) ;;
        *) return 65 ;;
    esac
    [ "$physical_parent" = "$parent" ] || return 65
    test -f "$tool" && test ! -L "$tool" && test -x "$tool" || return 65
    observed="$(LC_ALL=C PATH=/usr/bin:/bin /usr/bin/shasum -a 256 "$tool" |
        /usr/bin/awk -v path="$tool" '
            function is_lower_hex64(value) { return length(value) == 64 && value !~ /[^0-9a-f]/ }
            NR == 1 && NF == 2 && is_lower_hex64($1) && $2 == path { value = $1; next }
            { invalid = 1 }
            END { if (NR != 1 || invalid || value == "") exit 1; print value }
        ')" || return 65
    [ "$observed" = "$expected_sha256" ] || return 65
}
trace_client_binding "$tool" "$expected_sha256"
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

capture_pmf_report() {
    local label="$1"
    remote_trace get iwn-software-pmf-report >"$OUT_DIR/$label.stdout" 2>"$OUT_DIR/$label.stderr"
}

read_generic_attestation() {
    local path="$OUT_DIR/post-plti/runtime-attestation.json" values
    [ -f "$path" ] || return 1
    values="$(python3 - "$path" "$SOURCE_COMMIT" "$SOURCE_IDENTITY_SHA256" \
        "$RELEASE_TAG" "$ARCHIVE_SHA256" "$BINARY_SHA256" "$MACHO_UUID" <<'PY'
import json
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
expected = sys.argv[2:]
try:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != "itlwm-tahoe-post-plti-trace-runtime/v3":
        raise ValueError("schema")
    candidate = data.get("candidate")
    if not isinstance(candidate, dict):
        raise ValueError("candidate")
    for key, value in zip(("source_commit", "source_identity_sha256", "release_tag", "archive_sha256", "binary_sha256", "macho_uuid"), expected):
        if candidate.get(key) != value:
            raise ValueError("candidate identity")
    if candidate.get("identity_binding_precondition") != "PASS":
        raise ValueError("candidate binding")
    radio = data.get("radio_cycle")
    trace = data.get("trace")
    if not isinstance(radio, dict) or not isinstance(trace, dict):
        raise ValueError("radio/trace")
    scalars = ("reset_control_sequence", "reset_ack_generation", "capture_generation", "entry_count", "episode_count", "dropped_entries")
    for key in scalars:
        if type(trace.get(key)) is not int or trace[key] < 0 or trace[key] > 4294967295:
            raise ValueError(key)
    booleans = ("radio_off_observed", "radio_on_observed",
                "trace_armed_while_radio_off")
    for key in booleans:
        if not isinstance(radio.get(key), bool):
            raise ValueError(key)
    booleans = ("reset_ack_generation_synchronized", "initial_snapshot_buffer_generation_synchronized", "seal_control_acknowledged", "final_control_disabled", "double_read_stable", "backend_preflight_iwn")
    for key in booleans:
        if not isinstance(trace.get(key), bool):
            raise ValueError(key)
    if trace.get("backend") not in {"iwn", "iwx", "unsupported", "unknown"}:
        raise ValueError("backend")
    if trace.get("integrity") not in {"ok", "inconclusive"}:
        raise ValueError("integrity")
    if trace.get("verdict") not in {
        "KERNEL_CHAIN_OBSERVED", "BRANCH_NOT_OBSERVED",
        "RESUME_NO_STATE_REQUEST", "RESUME_NO_IWN_DISPATCH",
        "SCAN_COMMAND_REJECTED", "SCAN_INCOMPLETE", "SCAN_NO_CANDIDATE",
        "RESUME_NO_SELECTION", "AUTH_NOT_DRAINED", "TX_NO_COMPLETION",
        "NO_EAPOL", "BACKEND_UNSUPPORTED", "INTEGRITY_INCONCLUSIVE",
    }:
        raise ValueError("verdict")
    if trace.get("first_missing_stage") not in {
        "none", "state-scan-self-request", "iwn-scan-state",
        "iwn-scan-command", "scan-completion", "bss-selection",
        "join-bss", "auth-state", "auth-enqueue", "auth-dequeue",
        "auth-firmware-submit", "auth-exchange", "assoc-state",
        "assoc-enqueue", "assoc-dequeue", "assoc-firmware-submit",
        "assoc-exchange", "run-state", "eapol-decapped",
        "eapol-kernel-pae", "eapol-enqueue", "port-valid", "unknown",
    }:
        raise ValueError("first missing stage")
    if data.get("failure_phase") not in {
        "none", "preflight", "identity-attestation", "hostkey-pin",
        "guest-build-pin", "trace-client-preflight", "radio-precondition-on",
        "trace-preflight-reset-request", "trace-preflight-reset-sequence",
        "trace-preflight-reset-ack", "trace-preflight-final-off",
        "radio-off-request", "radio-off-observation", "trace-reset-request",
        "radio-off-before-final-reset", "radio-off-after-final-reset-ack",
        "radio-off-after-final-reset-sync",
        "trace-reset-sequence", "trace-backend-iwx-ordered-unsupported",
        "trace-backend-unsupported", "trace-reset-ack",
        "trace-reset-snapshot-buffer-sync", "radio-on-request",
        "radio-on-observation", "trace-pre-seal-observation", "trace-seal",
        "trace-first-read", "trace-second-read", "trace-double-read-unstable",
        "trace-final-off", "trace-success-invariants", "trace-verdict-diagnostic",
    }:
        raise ValueError("failure phase")
    if data.get("result") not in {"PASS", "INCONCLUSIVE"}:
        raise ValueError("result")
    if data["result"] == "PASS" and data["failure_phase"] != "none":
        raise ValueError("PASS failure phase")
    if data["result"] == "INCONCLUSIVE" and data["failure_phase"] == "none":
        raise ValueError("inconclusive failure phase")
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

read_pmf_report() {
    local path="$1"
    PMF_CAPTURE_GENERATION="$(extract_token "$path" capture_generation)" || return 1
    PMF_BACKEND="$(extract_token "$path" backend)" || return 1
    PMF_ENTRY_COUNT="$(extract_token "$path" entries)" || return 1
    PMF_INTEGRITY="$(extract_token "$path" integrity)" || return 1
    PMF_EPISODE_COUNT="$(extract_token "$path" episode_count)" || return 1
    PMF_ACTIVE_EPISODE="$(extract_token "$path" active_episode)" || return 1
    PMF_VERDICT="$(extract_token "$path" iwn_software_pmf_verdict)" || return 1
    PMF_FIRST_MISSING_STAGE="$(extract_token "$path" first_missing_stage)" || return 1
    for value in "$PMF_CAPTURE_GENERATION" "$PMF_ENTRY_COUNT" \
        "$PMF_EPISODE_COUNT" "$PMF_ACTIVE_EPISODE"; do
        is_u32 "$value" || return 1
    done
    [ "$PMF_BACKEND" = IWN ] || return 1
    case "$PMF_INTEGRITY" in ok|inconclusive) ;; *) return 1;; esac
    case "$PMF_VERDICT" in
        INITIAL_SOFTWARE_PMF_OBSERVED|BRANCH_NOT_OBSERVED|PTK_SOFTWARE_CCMP_NOT_OBSERVED|GTK_SOFTWARE_CCMP_NOT_OBSERVED|IGTK_STAGE_NOT_OBSERVED|IGTK_PUBLICATION_NOT_OBSERVED|SOFTWARE_KEYSET_PUBLICATION_NOT_OBSERVED|PORT_VALID_NOT_OBSERVED|BACKEND_UNSUPPORTED|INTEGRITY_INCONCLUSIVE) ;;
        *) return 1 ;;
    esac
    case "$PMF_FIRST_MISSING_STAGE" in
        none|ptk-software-ccmp|gtk-software-ccmp|igtk-stage|igtk-publication|software-keyset-publication|port-valid|capture-seal|unknown) ;;
        *) return 1 ;;
    esac
}

generic_chain_is_positive() {
    [ "$GENERIC_RESULT" = PASS ] &&
        [ "$GENERIC_FAILURE_PHASE" = none ] &&
        [ "$GENERIC_BACKEND" = iwn ] &&
        [ "$GENERIC_INTEGRITY" = ok ] &&
        [ "$GENERIC_VERDICT" = KERNEL_CHAIN_OBSERVED ] &&
        [ "$GENERIC_FIRST_MISSING_STAGE" = none ] &&
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

pmf_chain_is_positive() {
    [ "$PMF_CAPTURE_GENERATION" = "$GENERIC_CAPTURE_GENERATION" ] &&
        [ "$PMF_BACKEND" = IWN ] &&
        [ "$PMF_ENTRY_COUNT" = "$GENERIC_ENTRY_COUNT" ] &&
        [ "$PMF_ENTRY_COUNT" -gt 0 ] &&
        [ "$PMF_INTEGRITY" = ok ] &&
        [ "$PMF_EPISODE_COUNT" = 1 ] && [ "$PMF_ACTIVE_EPISODE" = 0 ] &&
        [ "$PMF_VERDICT" = INITIAL_SOFTWARE_PMF_OBSERVED ] &&
        [ "$PMF_FIRST_MISSING_STAGE" = none ]
}

write_safe_attestation() {
    [ "$ATTESTATION_WRITTEN" -eq 0 ] || return 0
    [ -n "$OUT_DIR" ] && [ -d "$OUT_DIR" ] || return 0
    python3 - "$OUT_DIR/runtime-attestation.json" \
        "$SOURCE_COMMIT" "$SOURCE_IDENTITY_SHA256" "$RELEASE_TAG" \
        "$ARCHIVE_SHA256" "$BINARY_SHA256" "$MACHO_UUID" \
        "$TRACE_CLIENT_SHA256" "$IDENTITY_BEFORE_BOUND" "$IDENTITY_AFTER_BOUND" \
        "$TRACE_CLIENT_PRE_BOUND" "$TRACE_CLIENT_POST_BOUND" "$GENERIC_RUNNER_EXIT" \
        "$GENERIC_RESULT" "$GENERIC_FAILURE_PHASE" "$GENERIC_RESET_SEQUENCE" \
        "$GENERIC_CAPTURE_GENERATION" "$GENERIC_BACKEND" "$GENERIC_INTEGRITY" \
        "$GENERIC_ENTRY_COUNT" "$GENERIC_EPISODE_COUNT" "$GENERIC_DROPPED_ENTRIES" \
        "$GENERIC_VERDICT" "$GENERIC_FIRST_MISSING_STAGE" "$GENERIC_RADIO_OFF" \
        "$GENERIC_RADIO_ON" "$GENERIC_RESET_SYNC" "$GENERIC_INITIAL_SYNC" \
        "$GENERIC_SEAL_ACK" "$GENERIC_FINAL_DISABLED" "$GENERIC_DOUBLE_READ" \
        "$GENERIC_ARMED_WHILE_RADIO_OFF" "$GENERIC_BACKEND_PREFLIGHT_IWN" \
        "$PMF_REPORT_ONE_READ" "$PMF_REPORT_TWO_READ" "$PMF_DOUBLE_READ_STABLE" \
        "$PMF_CAPTURE_GENERATION" "$PMF_BACKEND" "$PMF_ENTRY_COUNT" \
        "$PMF_INTEGRITY" "$PMF_EPISODE_COUNT" "$PMF_ACTIVE_EPISODE" \
        "$PMF_VERDICT" "$PMF_FIRST_MISSING_STAGE" "$RESULT" "$FAILURE_PHASE" <<'PY'
import json
import re
import sys
from pathlib import Path

(
    output, source_commit, source_identity, release_tag, archive_sha256,
    binary_sha256, macho_uuid, trace_client_sha256, identity_before,
    identity_after, client_pre, client_post, generic_exit, generic_result,
    generic_failure, reset_sequence, capture_generation, generic_backend,
    generic_integrity, generic_entries, generic_episodes, generic_dropped,
    generic_verdict, generic_missing, radio_off, radio_on, reset_sync,
    initial_sync, seal_ack, final_disabled, generic_double_read,
    generic_armed_while_radio_off, generic_backend_preflight_iwn,
    pmf_read_one, pmf_read_two, pmf_double_read, pmf_generation, pmf_backend,
    pmf_entries, pmf_integrity, pmf_episodes, pmf_active, pmf_verdict,
    pmf_missing, result, failure_phase,
) = sys.argv[1:]

def boolean(value: str) -> bool:
    return value == "1"

def integer(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError:
        return 0
    return parsed if parsed >= 0 else 0

candidate = {
    "source_commit": source_commit if re.fullmatch(r"[0-9a-f]{40}", source_commit) else "",
    "source_identity_sha256": source_identity if re.fullmatch(r"[0-9a-f]{64}", source_identity) else "",
    "release_tag": release_tag,
    "release_publication_model": "single_mutable_release_per_semantic_version",
    "archive_sha256": archive_sha256 if re.fullmatch(r"[0-9a-f]{64}", archive_sha256) else "",
    "binary_sha256": binary_sha256 if re.fullmatch(r"[0-9a-f]{64}", binary_sha256) else "",
    "macho_uuid": macho_uuid,
    "trace_client_sha256": trace_client_sha256 if re.fullmatch(r"[0-9a-f]{64}", trace_client_sha256) else "",
    "identity_before_bound": boolean(identity_before),
    "identity_after_bound": boolean(identity_after),
    "trace_client_pre_bound": boolean(client_pre),
    "trace_client_post_bound": boolean(client_post),
}
generic = {
    "delegated_runner_exit": integer(generic_exit),
    "result": generic_result,
    "failure_phase": generic_failure,
    "reset_control_sequence": integer(reset_sequence),
    "capture_generation": integer(capture_generation),
    "backend": generic_backend,
    "integrity": generic_integrity,
    "entry_count": integer(generic_entries),
    "episode_count": integer(generic_episodes),
    "dropped_entries": integer(generic_dropped),
    "verdict": generic_verdict,
    "first_missing_stage": generic_missing,
    "radio_off_observed": boolean(radio_off),
    "radio_on_observed": boolean(radio_on),
    "reset_ack_generation_synchronized": boolean(reset_sync),
    "initial_snapshot_buffer_generation_synchronized": boolean(initial_sync),
    "seal_control_acknowledged": boolean(seal_ack),
    "final_control_disabled": boolean(final_disabled),
    "double_read_stable": boolean(generic_double_read),
    "trace_armed_while_radio_off": boolean(generic_armed_while_radio_off),
    "backend_preflight_iwn": boolean(generic_backend_preflight_iwn),
}
pmf = {
    "report_one_read": boolean(pmf_read_one),
    "report_two_read": boolean(pmf_read_two),
    "double_read_stable": boolean(pmf_double_read),
    "capture_generation": integer(pmf_generation),
    "backend": pmf_backend,
    "entry_count": integer(pmf_entries),
    "integrity": pmf_integrity,
    "episode_count": integer(pmf_episodes),
    "active_episode": integer(pmf_active),
    "verdict": pmf_verdict,
    "first_missing_stage": pmf_missing,
}
document = {
    "schema": "itlwm-tahoe-iwn-software-pmf-runtime/v1",
    "candidate": candidate,
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
        "explicit_join_command": False,
        "explicit_scan_command": False,
        "explicit_profile_command": False,
        "explicit_route_command": False,
        "explicit_address_command": False,
        "explicit_dhcp_state_mutating_command": False,
    },
    "generic_trace": generic,
    "iwn_software_pmf_trace": pmf,
    "result": result,
    "failure_phase": failure_phase,
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
        "SAE or WPA3 functionality",
        "protected-MPDU interoperability or data-plane verification",
        "group rekey, reconnect, roaming, or multi-AP replacement",
        "physical-host validation",
        "proof beyond the local software PMF ownership trace verdict",
    ],
}
Path(output).write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY
    ATTESTATION_WRITTEN=1
}

cleanup() {
    local rc=$?
    trap - EXIT HUP INT TERM
    set +e
    if [ "$IDENTITY_BEFORE_BOUND" -eq 1 ] && [ "$IDENTITY_AFTER_BOUND" -eq 0 ]; then
        capture_identity after || true
    fi
    if [ "$TRACE_CLIENT_PRE_BOUND" -eq 1 ] && [ "$TRACE_CLIENT_POST_BOUND" -eq 0 ]; then
        if remote_trace_client_exists; then
            TRACE_CLIENT_POST_BOUND=1
        fi
    fi
    write_safe_attestation
    [ -z "$KNOWN_HOSTS" ] || rm -f "$KNOWN_HOSTS"
    exit "$rc"
}

trap cleanup EXIT
trap 'exit 130' HUP INT
trap 'exit 143' TERM

umask 077
mkdir "$OUT_DIR"
chmod 700 "$OUT_DIR"

KNOWN_HOSTS="$(mktemp /tmp/aiam-iwn-software-pmf-known-hosts.XXXXXX)"
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
TRACE_CLIENT_PRE_BOUND=1
capture_identity before || fail_phase candidate-identity-before

set +e
"$POST_PLTI_RUNNER" --trace-tool "$TRACE_TOOL" \
    --identity-evidence "$OUT_DIR/identity-before.json" --out "$OUT_DIR/post-plti" \
    --trace-client-sha256 "$TRACE_CLIENT_SHA256" \
    --arm-while-radio-off \
    --settle-seconds "$SETTLE_SECONDS" --ack-attempts "$ACK_ATTEMPTS" \
    --radio-attempts "$RADIO_ATTEMPTS" \
    --stable-read-delay-seconds "$STABLE_READ_DELAY_SECONDS" \
    >"$OUT_DIR/post-plti-runner.stdout" 2>"$OUT_DIR/post-plti-runner.stderr"
GENERIC_RUNNER_EXIT=$?
set -e

read_generic_attestation || fail_phase delegated-runner-attestation

if [ "$GENERIC_RUNNER_EXIT" -ne 0 ]; then
    RESULT="INCONCLUSIVE"
    FAILURE_PHASE="delegated-runner-failed"
    FINAL_EXIT=1
    printf 'INCONCLUSIVE: delegated post-PLTI runner did not complete cleanly\n'
    exit "$FINAL_EXIT"
fi

capture_pmf_report pmf-read-1 || fail_phase iwn-software-pmf-report-first-read
read_pmf_report "$OUT_DIR/pmf-read-1.stdout" || fail_phase iwn-software-pmf-report-first-parse
PMF_REPORT_ONE_READ=1
sleep "$STABLE_READ_DELAY_SECONDS"
capture_pmf_report pmf-read-2 || fail_phase iwn-software-pmf-report-second-read
read_pmf_report "$OUT_DIR/pmf-read-2.stdout" || fail_phase iwn-software-pmf-report-second-parse
PMF_REPORT_TWO_READ=1
cmp -s "$OUT_DIR/pmf-read-1.stdout" "$OUT_DIR/pmf-read-2.stdout" ||
    fail_phase iwn-software-pmf-double-read-unstable
PMF_DOUBLE_READ_STABLE=1
capture_identity after || fail_phase candidate-identity-after
remote_trace_client_exists || fail_phase trace-client-postflight
TRACE_CLIENT_POST_BOUND=1

if generic_chain_is_positive && pmf_chain_is_positive; then
    RESULT="PASS"
    FAILURE_PHASE="none"
    FINAL_EXIT=0
    printf 'PASS: one sealed IWN software-PMF local ownership chain observed\n'
else
    RESULT="INCONCLUSIVE"
    FAILURE_PHASE="trace-verdict-diagnostic"
    FINAL_EXIT=0
    printf 'INCONCLUSIVE: sealed IWN software-PMF aggregate retained as local-only diagnostic evidence\n'
fi
exit "$FINAL_EXIT"
