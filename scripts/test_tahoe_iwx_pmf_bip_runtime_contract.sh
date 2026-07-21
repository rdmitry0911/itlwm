#!/usr/bin/env bash
# Static safety contract for the dedicated IWX PMF/BIP runtime gate.
#
# It deliberately executes only shell syntax/help and an in-memory evidence
# fixture.  It does not contact the QEMU guest or alter the laboratory AP.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_iwx_pmf_bip_runtime.sh"
AP_HELPER="$ROOT/scripts/tahoe_pmf_required_ap_switchover.sh"
AP_FIXTURE="$ROOT/scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh"
EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh"
PROTOCOL="$ROOT/docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md"

fail() {
    printf 'FAIL: IWX PMF/BIP runtime contract: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    local path="$1" needle="$2" label="$3"
    grep -Fq -- "$needle" "$path" || fail "missing $label"
}

forbid_literal() {
    local path="$1" needle="$2" label="$3"
    ! grep -Fq -- "$needle" "$path" || fail "forbidden $label"
}

for path in "$RUNNER" "$AP_HELPER" "$AP_FIXTURE" "$EVIDENCE_CONTRACT" "$PROTOCOL"; do
    [ -f "$path" ] || fail "required file is missing: ${path##*/}"
done
[ -x "$RUNNER" ] && [ -x "$AP_HELPER" ] && [ -x "$AP_FIXTURE" ] && [ -x "$EVIDENCE_CONTRACT" ] ||
    fail 'runtime scripts must be executable'
bash -n "$RUNNER"
bash -n "$AP_HELPER"
bash -n "$AP_FIXTURE"
"$RUNNER" --help >/dev/null 2>&1
"$AP_HELPER" --help >/dev/null 2>&1
"$EVIDENCE_CONTRACT" --self-test
"$AP_FIXTURE"

for needle in \
    'PINNED_GUEST="devops@127.0.0.1"' \
    'PINNED_PORT=3322' \
    'PINNED_GUEST_BUILD="25C56"' \
    'StrictHostKeyChecking=yes' \
    'PINNED_GUEST_HOSTKEY_SHA256' \
    'PINNED_WIFI_INTERFACE="en1"' \
    'PINNED_MANAGEMENT_INTERFACE="en0"' \
    'itlwm-tahoe-lab-kext-identity-binding/v2' \
    'candidate_kext_bound' \
    'ready_for_exact_candidate_runtime_experiment' \
    'get pmf-bip-progress' \
    'INITIAL_PMF_BIP_READY' \
    'get pmf-bip-report' \
    'CROSS_SLOT_REKEY_OBSERVED' \
    'run_bounded_traffic_probe' \
    'saved_profile_autojoin_only' \
    'runtime-attestation.json' \
    'host_ip_nat_forwarding_route_mutated' \
    'AP_REQUIRED_WAS_ACTIVE' \
    'AP_ROLLBACK_VERIFIED' \
    'RADIO_OFF_PENDING=1' \
    'RADIO_RECOVERY_ATTEMPTED=1'; do
    require_literal "$RUNNER" "$needle" "runner safety token: $needle"
done

# This is an observation-only/micro-stimulus runner.  It may read route and
# interface state but must not perform a join, scan, address/route mutation,
# guest reboot, or direct kext operation.
for needle in \
    '-setairportnetwork' \
    'airport -s' \
    'wdutil scan' \
    'route add' \
    'route delete' \
    'route change' \
    'ipconfig set' \
    '-setmanual' \
    '-setdhcp' \
    'kmutil ' \
    'kextload' \
    'kextutil' \
    'shutdown -r' \
    '/sbin/reboot' \
    'scp ' \
    'rsync ' \
    'curl ' \
    'AIAM_IWX_PMF_BIP_TEST_MODE'; do
    forbid_literal "$RUNNER" "$needle" "runner capability: $needle"
done

for needle in \
    'AP_IF="${AIAM_PMF_AP_INTERFACE:-wlp0s20f3}"' \
    'OPTIONAL_CONFIG="${AIAM_PMF_AP_OPTIONAL_CONFIG:-$LAB_ROOT/hostapd-5g.conf}"' \
    'REQUIRED_CONFIG="${AIAM_PMF_AP_REQUIRED_CONFIG:-$LAB_ROOT/hostapd-5g-wpa2-pmf.conf}"' \
    'EXPECTED_CHANNEL=153' \
    'EXPECTED_WIDTH_MHZ=80' \
    'EXPECTED_CENTER1_MHZ=5775' \
    'CONFIG_VALIDATION_FAILURE=ssid-pair-mismatch' \
    'state=rollback-armed' \
    'mark_required_active' \
    'start_watchdog' \
    '9>&-' \
    'finish_armed_rollback' \
    'setsid "$SELF" --watchdog' \
    'for attempt in $(seq 1 3)' \
    'raw REKEY_GTK' \
    'rollback_verified=true' \
    'host_network_signature' \
    'runtime_ap_is_pinned'; do
    require_literal "$AP_HELPER" "$needle" "AP helper safety token: $needle"
done

for needle in \
    'ip addr add' \
    'ip addr replace' \
    'ip route add' \
    'ip route del' \
    'iptables ' \
    'nft add' \
    'sysctl -w' \
    'nmcli ' \
    'dnsmasq' \
    'systemctl ' \
    '/sbin/reboot' \
    'shutdown -r' \
    'set -x' \
    'config_sha256'; do
    forbid_literal "$AP_HELPER" "$needle" "AP helper capability: $needle"
done

forbid_literal "$AP_HELPER" 'rekey_gtk' 'non-portable lower-case hostapd CLI alias'

python3 - "$RUNNER" "$AP_HELPER" "$PROTOCOL" <<'PY'
from pathlib import Path
import re
import sys


runner = Path(sys.argv[1]).read_text(encoding="utf-8")
helper = Path(sys.argv[2]).read_text(encoding="utf-8")
protocol = Path(sys.argv[3]).read_text(encoding="utf-8")


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: IWX PMF/BIP runtime contract: {message}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        pos = text.find(token, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = pos + len(token)


# Exclude cleanup definitions so the static ordering describes the successful
# main line, not an emergency restoration path.
main_start = runner.find('umask 077\nmkdir "$OUT_DIR"')
if main_start < 0:
    fail("runner main entry is missing")
main = runner[main_start:]
ordered(main, "runner PMF/BIP sequence",
        "capture_identity before",
        '"$AP_HELPER" --preflight',
        "capture_trace_client reset reset",
        "wait_for_control_ack reset-ack",
        "wait_for_reset_snapshot_buffer_sync",
        "remote_radio_power off",
        '"$AP_HELPER" --activate --state-dir "$AP_STATE_DIR"',
        "remote_radio_power on",
        "wait_for_initial_pmf_progress",
        "run_bounded_traffic_probe",
        '"$AP_HELPER" --rekey --state-dir "$AP_STATE_DIR"',
        "seal_trace || fail_phase trace-seal",
        "read_final_trace_once read-1",
        "read_final_trace_once read-2",
        "disable_trace || fail_phase trace-final-off",
        '"$AP_HELPER" --rollback --state-dir "$AP_STATE_DIR"',
        "capture_identity after")

rekey = main.find('"$AP_HELPER" --rekey --state-dir "$AP_STATE_DIR"')
initial = main.find("wait_for_initial_pmf_progress")
traffic = main.find("run_bounded_traffic_probe")
if not (0 <= initial < traffic < rekey):
    fail("rekey is not gated by initial PMF progress and traffic")

attestation = runner[runner.find("write_safe_attestation() {"):runner.find("cleanup() {")]
for token in ("PINNED_PROFILE_SSID", "PINNED_LAB_GATEWAY",
              "PINNED_WIFI_INTERFACE", "ACTIVE_CLIENT_MAC",
              "DEFAULT_ROUTE_BASELINE", "MANAGEMENT_IPV4_BASELINE",
              "LAB_IPV4_BASELINE", "LAB_ROUTE_BASELINE"):
    if token in attestation:
        fail(f"sanitized attestation serializes runtime identity: {token}")

activate = helper[helper.find("do_activate() {"):helper.find("do_rekey() {")]
ordered(activate, "AP activation rollback ownership",
        "write_state \"$network_signature\"",
        "write_marker",
        "start_watchdog",
        'stop_configured_hostapd "$OPTIONAL_CONFIG"',
        'start_configured_hostapd "$REQUIRED_CONFIG"',
        "mark_required_active")
if "finish_armed_rollback" not in activate:
    fail("activation failure does not retain a rollback owner")

rollback = helper[helper.find("do_rollback() {"):helper.find("do_watchdog() {")]
ordered(rollback, "AP rollback sequence",
        'stop_configured_hostapd "$REQUIRED_CONFIG"',
        'start_configured_hostapd "$OPTIONAL_CONFIG"',
        "runtime_ap_is_pinned",
        "host_network_signature",
        "rollback_verified=true",
        "cancel_watchdog",
        "clear_marker")

for token in ("wpa_passphrase", "optional_ssid", "required_ssid",
              "optional_passphrase", "required_passphrase"):
    if re.search(rf"printf[^\n]*{re.escape(token)}", helper):
        fail(f"AP helper renders credential or wireless identity: {token}")

for token in (
    "initial active prefix", "does not establish final success",
    "rollback watchdog", "local-only", "does not prove pure SAE",
    "precondition failure", "fresh disposable overlay", "REKEY_GTK",
):
    if token not in protocol:
        fail(f"runtime protocol omits boundary: {token}")
for pattern, label in (
    (r"\b(?:\d{1,3}\.){3}\d{1,3}\b", "IPv4 literal"),
    (r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", "MAC literal"),
):
    if re.search(pattern, protocol):
        fail(f"runtime protocol contains {label}")

print("PASS: IWX PMF/BIP runtime static safety contract")
PY
