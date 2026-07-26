#!/usr/bin/env bash
# Static and parser-level contract for the hash-only active-LabAP attestation.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
switcher="$root/scripts/tahoe_labap_bss_switcher.sh"

[ -f "$switcher" ] || {
    printf '%s\n' 'FAIL: LabAP switcher is missing' >&2
    exit 1
}
bash -n "$switcher"

python3 - "$switcher" <<'PY'
from pathlib import Path
import sys


source = Path(sys.argv[1]).read_text()


def fail(message: str) -> None:
    raise SystemExit(f"LabAP hash-only status contract: {message}")


def require(token: str, label: str) -> None:
    if token not in source:
        fail(f"missing {label}: {token}")


def body(marker: str, label: str) -> str:
    start = source.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing {label} body")
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:offset]
    fail(f"unterminated {label}")


# A status request has no caller-chosen lease and is locked like every state
# operation.  It is deliberately rejected for every non-LabAP activation.
for token in (
    "--preflight|--activate|--withdraw|--rollback|--status",
    "LEASE_SECONDS_EXPLICIT=0",
    "LEASE_SECONDS_EXPLICIT=1",
    "status) [ -n \"$STATE_DIR\" ]",
    "[ \"$LEASE_SECONDS_EXPLICIT\" -eq 0 ]",
    "status) with_lock do_status;;",
    "schema=tahoe-labap-bss-switch/v3",
    "lease_not_after_monotonic_seconds=",
    "monotonic_uptime_seconds",
    "watchdog_remaining_seconds",
    "write_state_v2",
):
    require(token, "status admission/state boundary")

state = body("status_state_is_current()", "status state validator")
for token in (
    "[ \"$schema\" = tahoe-labap-bss-switch/v3 ]",
    "[ \"$state\" = labap-active ]",
    "[ \"$mode\" = labap ]",
    "is_hex64 \"$network\" && is_hex64 \"$fingerprint\"",
    "canonical_bssid \"$state_bssid\" >/dev/null",
    "[ \"$external_count\" -ge 2 ]",
    "is_decimal_in_range \"$lease_seconds\" 60 300",
    "[ \"$lease_deadline\" -gt \"$now\" ]",
    "[ \"$remaining\" -gt 0 ] && [ \"$remaining\" -le \"$lease_seconds\" ]",
    "marker_matches_state",
    "watchdog_owner_is_live",
):
    if token not in state:
        fail(f"missing fail-closed state check: {token}")

watchdog = body("watchdog_remaining_seconds()", "watchdog deadline reader")
for token in (
    "tahoe-labap-bss-switch/v3",
    "lease_not_after_monotonic_seconds",
    "[ \"$stored_lease\" != \"$LEASE_SECONDS\" ]",
    "monotonic_uptime_seconds",
    "[ \"$deadline\" -le \"$now\" ]",
    "printf '0\\n'",
):
    if token not in watchdog:
        fail(f"watchdog does not enforce the absolute v3 deadline: {token}")
watchdog_main = body("do_watchdog()", "watchdog loop")
if 'remaining="$(watchdog_remaining_seconds)"' not in watchdog_main:
    fail("watchdog does not refresh the absolute deadline while waiting")

watchdog_match = body("watchdog_process_matches()", "watchdog argv matcher")
for token in (
    '"/proc/$pid/cmdline"',
    "read -r -d '' value",
    "--state-dir",
    '"${argv[$((index + 1))]}" = "$STATE_DIR"',
    '"$self_count" -eq 1',
    '"$watchdog_count" -eq 1',
    '"$state_count" -eq 1',
):
    if token not in watchdog_match:
        fail(f"watchdog argv match is not exact: {token}")
watchdog_live = body("watchdog_process_can_rollback()", "watchdog liveness gate")
if "''|Z*|T*|t*" not in watchdog_live:
    fail("status can attest a stopped, traced, or zombie watchdog")

range_gate = body("is_decimal_in_range()", "canonical decimal lease gate")
if "0[0-9]*" not in range_gate:
    fail("lease gate accepts an octal-looking decimal spelling")

runtime = body("status_runtime_is_exact()", "runtime validator")
for token in (
    "validate_test_config",
    "test_hostapd_process_active",
    "runtime_ap_is_pinned",
    "hostapd_status_snapshot",
    "hostapd_status_snapshot_is \"$TEST_SSID\" \"$status\"",
    "hostapd_status_snapshot_bssid \"$status\"",
    "canonical_bssid",
    "[ \"$runtime_bssid\" = \"$status_bssid\" ]",
    "[ \"$runtime_bssid\" = \"$state_bssid\" ]",
):
    if token not in runtime:
        fail(f"missing exact runtime binding: {token}")

canonical = body("canonical_bssid()", "BSSID canonicalizer")
for token in (
    "[0-9A-Fa-f]",
    "${value,,}",
):
    if token not in canonical:
        fail(f"missing canonical BSSID rule: {token}")

status = body("do_status()", "status emitter")
for token in (
    "status_state_is_current",
    "status_runtime_is_exact",
    "opaque_sha256 \"$TEST_SSID\"",
    "opaque_sha256 \"$STATUS_RUNTIME_BSSID\"",
    "LABAP_BSS_STATUS schema=tahoe-labap-bss-status/v1 active=1",
    "target_ssid_sha256=%s target_bssid_sha256=%s",
    "lease_seconds=%s lease_remaining_seconds=%s",
    "\"$ssid_sha256\" \"$bssid_sha256\" \"$STATUS_LEASE_SECONDS\" \"$STATUS_LEASE_REMAINING_SECONDS\"",
):
    if token not in status:
        fail(f"missing aggregate-only status field: {token}")
for forbidden in (
    "start_exact_hostapd",
    "stop_exact_hostapd",
    "write_state",
    "set_state",
    "unlink",
    "scan_",
    "kill ",
    "runtime_bssid\"",
    "state_bssid\"",
):
    if forbidden in status:
        fail(f"status body has mutating/raw output surface: {forbidden}")
if source.count("LABAP_BSS_STATUS schema=") != 1:
    fail("status schema must have exactly one output site")
if "LABAP_BSS_STATUS" in runtime or "LABAP_BSS_STATUS" in state:
    fail("precondition helpers must not emit a partial schema line")

print("PASS: Tahoe LabAP hash-only status contract")
PY

# An empty private directory lets the real parser/status entry path reject the
# request before it can invoke sudo, hostapd, or a network operation.  Neither
# a malformed state nor a caller-selected lease may ever yield a schema line.
state_dir="$(mktemp -d /tmp/aiam-labap-bss-switch.status-contract.XXXXXX)"
chmod 700 "$state_dir"
trap 'rmdir "$state_dir" 2>/dev/null || true' EXIT
if output="$("$switcher" --status --state-dir "$state_dir" 2>&1)"; then
    printf '%s\n' 'FAIL: empty status fixture unexpectedly succeeded' >&2
    exit 1
fi
case "$output" in *LABAP_BSS_STATUS*)
    printf '%s\n' 'FAIL: rejected status emitted a schema line' >&2
    exit 1;; esac
if output="$("$switcher" --status --state-dir "$state_dir" --lease-seconds 60 2>&1)"; then
    printf '%s\n' 'FAIL: status accepted a caller-selected lease' >&2
    exit 1
fi
case "$output" in *LABAP_BSS_STATUS*)
    printf '%s\n' 'FAIL: rejected explicit-lease status emitted a schema line' >&2
    exit 1;; esac
for invalid_lease in 060 00180; do
    if output="$("$switcher" --activate --state-dir "$state_dir" --lease-seconds "$invalid_lease" 2>&1)"; then
        printf '%s\n' 'FAIL: activation accepted an octal-looking lease' >&2
        exit 1
    fi
    case "$output" in *LABAP_BSS_STATUS*)
        printf '%s\n' 'FAIL: rejected lease emitted a status schema line' >&2
        exit 1;; esac
done

printf '%s\n' 'PASS: Tahoe LabAP hash-only status parser rejection'
