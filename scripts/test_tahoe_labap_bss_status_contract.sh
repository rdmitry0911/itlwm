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
    "--preflight|--activate|--renew-for-withdraw|--withdraw|--rollback|--recovery-owner|--status|--retire",
    "LEASE_SECONDS_EXPLICIT=0",
    "LEASE_SECONDS_EXPLICIT=1",
    "status) [ -n \"$STATE_DIR\" ]",
    "recovery-owner) [ -n \"$STATE_DIR\" ]",
    "retire) [ -n \"$STATE_DIR\" ]",
    "[ \"$LEASE_SECONDS_EXPLICIT\" -eq 0 ]",
    "status) with_lock do_status;;",
    "recovery-owner) do_recovery_owner;;",
    "renew-for-withdraw) with_lock do_renew_for_withdraw;;",
    "retire) with_lock do_retire;;",
    "schema=tahoe-labap-bss-switch/v4",
    "schema=tahoe-labap-bss-switch/v3",
    "deadline_phase=",
    "SETUP_DEADLINE_SECONDS=180",
    "lease_not_after_monotonic_seconds=",
    "monotonic_uptime_seconds",
    "watchdog_remaining_seconds",
    "write_state_v2",
):
    require(token, "status admission/state boundary")

writer = body("write_state()", "v4 state writer")
for token in (
    "schema=tahoe-labap-bss-switch/v4",
    "armed:setup",
    "labap-active:active|labap-withdraw-armed:active|direct-active:active|withdrawn:active",
    "original-restored:setup|original-restored:active",
    "deadline_phase=%s",
    "lease_not_after_monotonic_seconds=%s",
):
    if token not in writer:
        fail(f"v4 writer lacks a phase-bounded state field: {token}")

state = body("status_state_is_current()", "status state validator")
for token in (
    "[ \"$schema\" = tahoe-labap-bss-switch/v4 ]",
    "case \"$state\" in labap-active|labap-withdraw-armed)",
    "[ \"$mode\" = labap ]",
    "is_hex64 \"$network\" && is_hex64 \"$fingerprint\"",
    "canonical_bssid \"$state_bssid\" >/dev/null",
    "[ \"$external_count\" -ge 2 ]",
    "is_decimal_in_range \"$lease_seconds\" 60 300",
    "[ \"$deadline_phase\" = active ]",
    "is_canonical_positive_decimal \"$lease_deadline\"",
    "[ \"$lease_deadline\" -gt \"$now\" ]",
    "[ \"$remaining\" -gt 0 ] && [ \"$remaining\" -le \"$lease_seconds\" ]",
    "marker_matches_state",
    "watchdog_owner_is_live",
):
    if token not in state:
        fail(f"missing fail-closed state check: {token}")

watchdog = body("watchdog_remaining_seconds()", "watchdog deadline reader")
for token in (
    "tahoe-labap-bss-switch/v4",
    "tahoe-labap-bss-switch/v3",
    "deadline_phase",
    "armed:setup",
    "labap-active:active|labap-withdraw-armed:active|direct-active:active|withdrawn:active",
    'maximum_seconds="$SETUP_DEADLINE_SECONDS"',
    'maximum_seconds="$stored_lease"',
    "lease_not_after_monotonic_seconds",
    "[ \"$stored_lease\" != \"$LEASE_SECONDS\" ]",
    "monotonic_uptime_seconds",
    "[ \"$deadline\" -le \"$now\" ]",
    "is_canonical_positive_decimal \"$deadline\"",
    "printf '0\\n'",
):
    if token not in watchdog:
        fail(f"watchdog does not enforce the absolute v3 deadline: {token}")
watchdog_main = body("do_watchdog()", "watchdog loop")
if 'remaining="$(watchdog_remaining_seconds)"' not in watchdog_main:
    fail("watchdog does not refresh the absolute deadline while waiting")
if '[ "$current_state" != original-restored ] || return 0' not in watchdog_main:
    fail("watchdog does not leave a verified rollback state without mutation")

promotion = body("promote_active_state()", "active lease promotion")
for token in (
    '[ "$schema" = tahoe-labap-bss-switch/v4 ]',
    '[ "$state" = armed ]',
    '[ "$deadline_phase" = setup ]',
    'remaining=$((setup_deadline - now))',
    '[ "$remaining" -gt 0 ] && [ "$remaining" -le "$SETUP_DEADLINE_SECONDS" ]',
    'active_deadline=$((now + lease_seconds))',
    '"$lease_seconds" active "$active_deadline"',
):
    if token not in promotion:
        fail(f"active lease promotion is not bounded/exact: {token}")

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

range_gate = body("is_canonical_decimal()", "canonical decimal lease gate")
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

retire = body("do_retire()", "verified state retirement")
for token in (
    "[ \"$(state_value state)\" = original-restored ]",
    "retire_state_is_safe",
    "active marker blocks retirement",
    "watchdog receipt blocks retirement",
    "retire_regular_mode_600 \"$(state_file)\"",
    "rollback_verified=true",
    "retire_activation_phase_is_safe",
    "retire_directory_has_only_receipts",
    "live_config_matches_state",
    "live_hostapd_active",
    "host_network_signature",
    "unlink \"$(state_file)\"",
    "rmdir \"$STATE_DIR\"",
    "LABAP_BSS_SWITCH=RETIRED",
):
    if token not in retire:
        fail(f"missing verified retirement condition: {token}")
for forbidden in ("rm -rf", "start_exact_hostapd", "stop_exact_hostapd",
                  "write_state", "scan_"):
    if forbidden in retire:
        fail(f"retirement has an unsafe mutation surface: {forbidden}")

retire_state = body("retire_state_is_safe()", "retired v4 state validator")
for token in (
    "tahoe-labap-bss-switch/v4",
    "deadline_phase",
    "case \"$deadline_phase\" in setup|active)",
    "is_decimal_in_range \"$lease_seconds\" 60 300",
    "is_canonical_positive_decimal \"$deadline\"",
):
    if token not in retire_state:
        fail(f"retirement does not validate the v4 deadline receipt: {token}")

withdraw = body("do_withdraw()", "one-shot withdrawal")
for token in (
    "active_lease_is_current labap-withdraw-armed",
    "renewed LabAP lease is expired or malformed",
    "renewed LabAP lease changed before withdrawal",
    "set_state withdrawn",
):
    if token not in withdraw:
        fail(f"withdrawal is not fail-closed on the active lease: {token}")

renew = body("renew_active_lease_for_withdraw()", "one-shot renewed lease")
for token in (
    '[ "$schema" = tahoe-labap-bss-switch/v4 ]',
    '[ "$state" = labap-active ]',
    'active_lease_is_current labap-active',
    'write_state labap-withdraw-armed',
):
    if token not in renew:
        fail(f"renewal lacks exact one-shot transition: {token}")
for forbidden in ("start_exact_hostapd", "stop_exact_hostapd", "scan_", "ensure_ap_channel_ir"):
    if forbidden in renew:
        fail(f"renewal has a host/network mutation surface: {forbidden}")
rollback = body("do_rollback()", "watchdog rollback gate")
for token in (
    '[ "$FROM_WATCHDOG" -eq 1 ]',
    'watchdog_remaining_seconds',
    '[ "$watchdog_remaining" -eq 0 ] || return 1',
):
    if token not in rollback:
        fail(f"watchdog rollback does not recheck the live v4 deadline: {token}")

owner = body("do_recovery_owner()", "read-only recovery owner")
for token in (
    "require_state_dir",
    "watchdog_recovery_owner_is_current",
    "recovery_owner_is_proven_unarmed",
    "LABAP_BSS_RECOVERY_OWNER=WATCHDOG",
    "LABAP_BSS_RECOVERY_OWNER=NONE",
    "recovery ownership is ambiguous; state retained",
):
    if token not in owner:
        fail(f"recovery-owner lacks fixed fail-closed result: {token}")
for forbidden in ("with_lock", "start_exact_hostapd", "stop_exact_hostapd", "write_state",
                  "set_state", "scan_", "sudo_cmd", "unlink", "rmdir"):
    if forbidden in owner:
        fail(f"recovery-owner is not strictly read-only: {forbidden}")
owner_live = body("watchdog_recovery_owner_is_current()", "live recovery owner validator")
for token in (
    "[ \"$schema\" = tahoe-labap-bss-switch/v4 ]",
    "marker_matches_state",
    "watchdog_owner_is_live",
    "labap:labap-withdraw-armed:active",
):
    if token not in owner_live:
        fail(f"recovery-owner lacks exact v4 watchdog scope: {token}")
for forbidden in ("start_exact_hostapd", "stop_exact_hostapd", "scan_", "sudo_cmd",
                  "write_state", "set_state"):
    if forbidden in owner_live:
        fail(f"recovery-owner liveness check mutates host/network: {forbidden}")
owner_none = body("recovery_owner_is_proven_unarmed()", "proven unarmed owner result")
for token in (
    "[ ! -e \"$MARKER\" ] && [ ! -L \"$MARKER\" ]",
    '"$(state_file)"', '"$(test_config)"', '"$(watchdog_pid_file)"',
    '"$STATE_DIR"/*', 'return 1',
):
    if token not in owner_none:
        fail(f"NONE is not restricted to an exact empty unarmed state: {token}")


class RenewalModel:
    def __init__(self):
        self.state = "labap-active"
        self.deadline = 300

    def renew_for_withdraw(self, now: int) -> None:
        if self.state != "labap-active" or not now < self.deadline:
            raise ValueError("renew rejected")
        self.state = "labap-withdraw-armed"
        self.deadline = now + 300

    def statusable(self, now: int) -> bool:
        return self.state in {"labap-active", "labap-withdraw-armed"} and now < self.deadline

    def watchdog_may_rollback_under_lock(self, now: int) -> bool:
        return now >= self.deadline


model = RenewalModel()
# A stale watchdog expiry at 300 must re-read the atomically renewed deadline
# under lock: after renewal at 299 the intermediate state is statusable and
# rollback is not yet authorized.
model.renew_for_withdraw(299)
assert model.statusable(300)
assert not model.watchdog_may_rollback_under_lock(300)
try:
    model.renew_for_withdraw(300)
except ValueError:
    pass
else:
    raise AssertionError("second renewal was accepted")


class RecoveryOwnerModel:
    def __init__(self, *, exact_v4: bool, marker: bool, live_watchdog: bool, empty: bool):
        self.exact_v4 = exact_v4
        self.marker = marker
        self.live_watchdog = live_watchdog
        self.empty = empty

    def owner(self) -> str:
        if self.exact_v4 and self.marker and self.live_watchdog:
            return "WATCHDOG"
        if self.empty and not self.marker:
            return "NONE"
        raise ValueError("ambiguous")


assert RecoveryOwnerModel(exact_v4=False, marker=False, live_watchdog=False, empty=True).owner() == "NONE"
assert RecoveryOwnerModel(exact_v4=True, marker=True, live_watchdog=True, empty=False).owner() == "WATCHDOG"
for fixture in (
    RecoveryOwnerModel(exact_v4=True, marker=False, live_watchdog=False, empty=False),
    RecoveryOwnerModel(exact_v4=False, marker=True, live_watchdog=False, empty=False),
):
    try:
        fixture.owner()
    except ValueError:
        pass
    else:
        raise AssertionError("ambiguous recovery state was downgraded to NONE")

print("PASS: Tahoe LabAP hash-only status contract")
PY

# An empty private directory lets the real parser/status entry path reject the
# request before it can invoke sudo, hostapd, or a network operation.  Neither
# a malformed state nor a caller-selected lease may ever yield a schema line.
state_dir="$(mktemp -d /tmp/aiam-labap-bss-switch.status-contract.XXXXXX)"
chmod 700 "$state_dir"
trap 'rmdir "$state_dir" 2>/dev/null || true' EXIT
if output="$("$switcher" --recovery-owner --state-dir "$state_dir" 2>&1)"; then
    [ "$output" = 'LABAP_BSS_RECOVERY_OWNER=NONE' ] || {
        printf '%s\n' 'FAIL: empty recovery-owner fixture emitted a nonfixed result' >&2
        exit 1
    }
else
    printf '%s\n' 'FAIL: empty recovery-owner fixture was not recognized as unarmed' >&2
    exit 1
fi
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
if output="$("$switcher" --retire --state-dir "$state_dir" 2>&1)"; then
    printf '%s\n' 'FAIL: empty retirement fixture unexpectedly succeeded' >&2
    exit 1
fi
case "$output" in *LABAP_BSS_SWITCH=RETIRED*)
    printf '%s\n' 'FAIL: rejected retirement emitted a success line' >&2
    exit 1;; esac

printf '%s\n' 'PASS: Tahoe LabAP hash-only status parser rejection'
