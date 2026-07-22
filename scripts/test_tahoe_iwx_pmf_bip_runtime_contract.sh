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
OVERLAY_HELPER="$ROOT/scripts/tahoe_prepare_disposable_overlay.sh"
OVERLAY_EVIDENCE_CONTRACT="$ROOT/scripts/test_tahoe_disposable_overlay_evidence_contract.sh"
OVERLAY_PROTOCOL="$ROOT/docs/TAHOE_DISPOSABLE_OVERLAY_PROTOCOL.md"

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

cleanup_order_fixture() {
    local cleanup_source fixture_dir observed
    cleanup_source="$(awk '
        /^cleanup\(\) \{/ { capture = 1 }
        capture { print }
        capture && /^}$/ { exit }
    ' "$RUNNER")"
    [ -n "$cleanup_source" ] || fail 'runner cleanup body is missing'
    fixture_dir="$(mktemp -d /tmp/aiam-pmf-cleanup-order.XXXXXX)"
    if ! CLEANUP_SOURCE="$cleanup_source" CLEANUP_TEST_DIR="$fixture_dir" "$BASH" -c '
        set -euo pipefail
        eval "$CLEANUP_SOURCE"
        remote_radio_power() { printf "radio-on\n" >>"$CLEANUP_TEST_DIR/sequence"; }
        wait_for_radio_state() { return 0; }
        mock_ap_helper() {
            printf "ap-rollback\n" >>"$CLEANUP_TEST_DIR/sequence"
            printf "rollback_verified=true\n" >"$AP_STATE_DIR/rollback.status"
        }
        write_safe_attestation() { :; }
        disable_trace() { :; }
        capture_identity() { :; }
        OUT_DIR="$CLEANUP_TEST_DIR/out"
        AP_STATE_DIR="$CLEANUP_TEST_DIR/state"
        mkdir -p "$OUT_DIR" "$AP_STATE_DIR"
        AP_HELPER=mock_ap_helper
        RADIO_OFF_PENDING=1
        RADIO_RECOVERY_ATTEMPTED=0
        RADIO_ON_OBSERVED=0
        AP_ROLLBACK_VERIFIED=0
        AP_REQUIRED_ACTIVE=1
        AP_ROLLBACK_ATTEMPTED=0
        TRACE_MAY_BE_ARMED=0
        IDENTITY_BEFORE_BOUND=0
        IDENTITY_AFTER_BOUND=0
        KNOWN_HOSTS=""
        cleanup
    '; then
        unlink "$fixture_dir/sequence" 2>/dev/null || true
        unlink "$fixture_dir/state/rollback.status" 2>/dev/null || true
        unlink "$fixture_dir/out/ap-rollback-cleanup.stdout" 2>/dev/null || true
        unlink "$fixture_dir/out/ap-rollback-cleanup.stderr" 2>/dev/null || true
        rmdir "$fixture_dir/state" "$fixture_dir/out" "$fixture_dir" 2>/dev/null || true
        fail 'runner cleanup mock did not complete'
    fi
    observed="$(sed -n '1,2p' "$fixture_dir/sequence")"
    unlink "$fixture_dir/sequence" 2>/dev/null || true
    unlink "$fixture_dir/state/rollback.status" 2>/dev/null || true
    unlink "$fixture_dir/out/ap-rollback-cleanup.stdout" 2>/dev/null || true
    unlink "$fixture_dir/out/ap-rollback-cleanup.stderr" 2>/dev/null || true
    rmdir "$fixture_dir/state" "$fixture_dir/out" "$fixture_dir" 2>/dev/null || true
    [ "$observed" = $'ap-rollback\nradio-on' ] ||
        fail 'runner cleanup restores radio before AP rollback ownership'
}

for path in "$RUNNER" "$AP_HELPER" "$AP_FIXTURE" "$EVIDENCE_CONTRACT" "$PROTOCOL" \
            "$OVERLAY_HELPER" "$OVERLAY_EVIDENCE_CONTRACT" "$OVERLAY_PROTOCOL"; do
    [ -f "$path" ] || fail "required file is missing: ${path##*/}"
done
[ -x "$RUNNER" ] && [ -x "$AP_HELPER" ] && [ -x "$AP_FIXTURE" ] && [ -x "$EVIDENCE_CONTRACT" ] && \
    [ -x "$OVERLAY_HELPER" ] && [ -x "$OVERLAY_EVIDENCE_CONTRACT" ] ||
    fail 'runtime scripts must be executable'
bash -n "$RUNNER"
bash -n "$AP_HELPER"
bash -n "$AP_FIXTURE"
"$RUNNER" --help >/dev/null 2>&1
"$AP_HELPER" --help >/dev/null 2>&1
"$EVIDENCE_CONTRACT" --self-test
"$AP_FIXTURE"
cleanup_order_fixture

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
    'AP_ROLLBACK_ATTEMPTED=1' \
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
    'state directory permissions are not restricted' \
    '/usr/bin/stat -c %u' \
    '/usr/bin/stat -c %a' \
    'state=rollback-armed' \
    'mark_required_active' \
    'start_watchdog' \
    'watchdog_process_matches' \
    'watchdog_owner_is_current' \
    'PMF_AP_WATCHDOG_READY' \
    '--ready-fd 8' \
    'read -r -t 5 -u 8' \
    'host network invariants changed before optional-PMF stop' \
    'rollback watchdog is not exact before optional-PMF stop' \
    'config_pair_signature' \
    'config_pair_signature_before' \
    'config_pair_matches_state' \
    'PMF configurations changed before optional-PMF stop' \
    'required-PMF hostapd post-start attestation failed' \
    'required-PMF host-network invariants changed before state promotion' \
    'required-PMF configuration changed before state promotion' \
    'rollback watchdog is not exact before required-PMF state promotion' \
    'required-PMF hostapd is not exact before final state promotion' \
    'rollback watchdog is not exact before bounded group-rekey' \
    'staged PMF configuration pair changed before bounded group-rekey' \
    'staged PMF configuration pair changed before optional-PMF restart' \
    'optional_hostapd_exact_and_pinned' \
    'optional-PMF hostapd process or AP shape is not exact before rollback verification' \
    'rollback could not safely cancel its watchdog' \
    'finish_post_transition_rollback' \
    'optional-PMF state retained' \
    '9>&-' \
    'finish_armed_rollback' \
    'setsid "$SELF" --watchdog' \
    'for attempt in $(seq 1 3)' \
    'raw REKEY_GTK' \
    'rekey_attempted=true' \
    'bounded group-rekey request was already recorded for this PMF-required transaction' \
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
        "TRACE_MAY_BE_ARMED=1",
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

initial_progress = runner[runner.find("wait_for_initial_pmf_progress() {"):
                          runner.find("read_final_trace_once() {")]
for token in (
    'extract_u32 "$OUT_DIR/$snapshot.stdout" active_episode',
    'extract_u32 "$OUT_DIR/$progress.stdout" active_episode',
    '[ "$snapshot_episode" -gt 0 ]',
    '[ "$progress_episode" -gt 0 ]',
    '[ "$snapshot_episode" = "$progress_episode" ]',
    'file_has_token "$OUT_DIR/$snapshot.stdout" episode_count 1',
    'file_has_token "$OUT_DIR/$progress.stdout" episode_count 1',
):
    if token not in initial_progress:
        fail(f"initial PMF progress lacks live-episode fence: {token}")

attestation = runner[runner.find("write_safe_attestation() {"):runner.find("cleanup() {")]
for token in ("PINNED_PROFILE_SSID", "PINNED_LAB_GATEWAY",
              "PINNED_WIFI_INTERFACE", "ACTIVE_CLIENT_MAC",
              "DEFAULT_ROUTE_BASELINE", "MANAGEMENT_IPV4_BASELINE",
              "LAB_IPV4_BASELINE", "LAB_ROUTE_BASELINE"):
    if token in attestation:
        fail(f"sanitized attestation serializes runtime identity: {token}")

activate = helper[helper.find("do_activate() {"):helper.find("do_rekey() {")]
ordered(activate, "AP configuration admission",
        'config_signature="$(config_pair_signature)"',
        "validate_config_pair",
        'current_config_signature="$(config_pair_signature)"',
        '[ "$current_config_signature" = "$config_signature" ]',
        'network_signature="$(host_network_signature)"')
ordered(activate, "AP activation rollback ownership",
        "write_state \"$network_signature\" \"$config_signature\"",
        "write_marker",
        "start_watchdog",
        'stop_configured_hostapd "$OPTIONAL_CONFIG"',
        'start_configured_hostapd "$REQUIRED_CONFIG"',
        'configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID"',
        "runtime_ap_is_pinned",
        'current_signature="$(host_network_signature)"',
        '[ "$current_signature" != "$network_signature" ]',
        'current_config_signature="$(config_pair_signature)"',
        '[ "$current_config_signature" != "$config_signature" ]',
        "watchdog_owner_is_current",
        'configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID"',
        "runtime_ap_is_pinned",
        "mark_required_active")
if "finish_armed_rollback" not in activate:
    fail("activation failure does not retain a rollback owner")
post_transition_activation = activate[activate.find('if ! stop_configured_hostapd'):]
if post_transition_activation.count("finish_post_transition_rollback") != 8:
    fail("post-transition activation failures do not all verify network recovery")
post_watchdog_activation = activate[activate.find("if ! start_watchdog;"):]
ordered(post_watchdog_activation, "AP pre-stop host-network fence",
        "start_watchdog",
        'current_signature="$(host_network_signature)"',
        '[ "$current_signature" != "$network_signature" ]',
        'current_config_signature="$(config_pair_signature)"',
        '[ "$current_config_signature" != "$config_signature" ]',
        "watchdog_owner_is_current",
        'stop_configured_hostapd "$OPTIONAL_CONFIG"')

watchdog_start = helper[helper.find("start_watchdog() {"):
                        helper.find("cancel_watchdog() {")]
ordered(watchdog_start, "watchdog readiness ownership",
        'mkfifo -m 600 "$ready_fifo"',
        'setsid "$SELF" --watchdog',
        '--ready-fd 8',
        'read -r -t 5 -u 8 ready_line',
        'watchdog_process_matches "$watchdog_pid"',
        'write_watchdog_pid "$watchdog_pid"',
        "watchdog_owner_is_current")
watchdog_owner = helper[helper.find("watchdog_owner_is_current() {"):
                        helper.find("stop_unready_watchdog() {")]
ordered(watchdog_owner, "watchdog owner identity",
        'pid_from_file "$(watchdog_pid_file)"',
        "watchdog_process_matches")
watchdog = helper[helper.find("do_watchdog() {"):helper.find("with_lock() {")]
ordered(watchdog, "watchdog ready acknowledgement",
        "require_state_dir",
        "marker_matches_state",
        "PMF_AP_WATCHDOG_READY",
        'sleep "$LEASE_SECONDS"')
if "AIAM_PMF_AP_TEST_WATCHDOG_EXIT_BEFORE_READY" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the pre-ack watchdog failure discriminator")
if "FAKE_DRIFT_ON_ROUTE_CALL" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the pre-stop host-network drift discriminator")
if "FAKE_TERMINATE_WATCHDOG_ON_ROUTE_CALL" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the pre-transition watchdog death discriminator")
if "FAKE_MUTATE_REQUIRED_CONFIG_ON_ROUTE_CALL" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the pre-stop configuration drift discriminator")
if "FAKE_TERMINATE_REQUIRED_ON_IW" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the pre-promotion required-child death discriminator")
if "FAKE_TERMINATE_REQUIRED_DURING_REKEY" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the post-ack required-child death discriminator")
if "FAKE_TERMINATE_OPTIONAL_ON_IW" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the rollback optional-child death discriminator")
if "FAKE_TERMINATE_OPTIONAL_ON_IW_CALL" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the post-transition optional-child death discriminator")
if "FOREIGN_WATCHDOG_PID" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the rollback witness commit-order discriminator")
if "rekey-post-drift-retry" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the acknowledged-rekey retry discriminator")
if "FAKE_MUTATE_NETWORK_ON_REQUIRED_START" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the post-transition network drift discriminator")
if "POSTSTART_NETWORK_STATE_DIR" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the successful required-start network drift discriminator")
if "POSTPROMOTION_WATCHDOG_STATE_DIR" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the post-start watchdog death discriminator")
if "FINAL_REQUIRED_PROMOTION_STATE_DIR" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the final required-process death discriminator")
if "FAKE_TERMINATE_REQUIRED_ON_ROUTE_CALL" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the post-start required-process route discriminator")
if "REKEY_WATCHDOG_ROUTE_CALL" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the bounded-rekey watchdog death discriminator")
if "FAKE_MUTATE_REQUIRED_CONFIG_ON_START" not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the transition configuration drift discriminator")
if 'chmod 777 "$UNSAFE_STATE_DIR"' not in Path(sys.argv[2]).with_name("test_tahoe_pmf_required_ap_switchover_fixture.sh").read_text(encoding="utf-8"):
    fail("AP fixture lacks the restricted-state admission discriminator")

state_dir = helper[helper.find("require_state_dir() {"):helper.find("state_file() {")]
ordered(state_dir, "restricted AP state directory admission",
        'state directory must be canonical',
        '/usr/bin/stat -c %u',
        '/usr/bin/id -u',
        '/usr/bin/stat -c %a',
        '[ "$mode" = 700 ]')

rollback = helper[helper.find("do_rollback() {"):helper.find("do_watchdog() {")]
ordered(rollback, "AP rollback sequence",
        'stop_configured_hostapd "$REQUIRED_CONFIG"',
        "config_pair_matches_state",
        'start_configured_hostapd "$OPTIONAL_CONFIG"',
        "runtime_ap_is_pinned",
        "host_network_signature",
        "optional_hostapd_exact_and_pinned",
        "cancel_watchdog",
        "clear_marker",
        "rollback_verified=true")

post_transition_rollback = helper[helper.find("finish_post_transition_rollback() {"):
                                  helper.find("do_preflight() {")]
ordered(post_transition_rollback, "post-transition rollback network verification",
        'state_value host_network_signature_before',
        "restore_optional_after_activation_failure",
        'after_signature="$(host_network_signature)"',
        '[ "$after_signature" = "$before_signature" ]',
        "optional_hostapd_exact_and_pinned",
        "cancel_watchdog",
        "clear_marker")

armed_rollback = helper[helper.find("finish_armed_rollback() {"):
                        helper.find("finish_post_transition_rollback() {")]
ordered(armed_rollback, "armed rollback final optional-process attestation",
        "restore_optional_after_activation_failure",
        "optional_hostapd_exact_and_pinned",
        "cancel_watchdog",
        "clear_marker")

state_write = helper[helper.find("write_state() {"):helper.find("mark_required_active() {")]
ordered(state_write, "AP state configuration baseline",
        'host_network_signature_before=%s',
        'config_pair_signature_before=%s',
        'rollback_verified=false')
state_mark = helper[helper.find("mark_required_active() {"):helper.find("marker_matches_state() {")]
ordered(state_mark, "required AP state configuration baseline",
        'state_value host_network_signature_before',
        'state_value config_pair_signature_before',
        'host_network_signature_before=%s',
        'config_pair_signature_before=%s')
state_pair = helper[helper.find("config_pair_matches_state() {"):helper.find("write_state() {")]
ordered(state_pair, "AP state configuration comparison",
        'state_value config_pair_signature_before',
        'current_signature="$(config_pair_signature)"',
        '[ "$current_signature" = "$before_signature" ]')
restore = helper[helper.find("restore_optional_after_activation_failure() {"):
                 helper.find("finish_armed_rollback() {")]
ordered(restore, "optional restart configuration guard",
        'configured_hostapd_active "$REQUIRED_CONFIG"',
        'stop_configured_hostapd "$REQUIRED_CONFIG"',
        'configured_hostapd_active "$OPTIONAL_CONFIG"',
        "config_pair_matches_state",
        'start_configured_hostapd "$OPTIONAL_CONFIG"',
        "optional_hostapd_exact_and_pinned")

optional_attestation = helper[helper.find("optional_hostapd_exact_and_pinned() {"):
                              helper.find("wait_hostapd_active() {")]
ordered(optional_attestation, "optional rollback process/AP attestation",
        'configured_hostapd_active "$OPTIONAL_CONFIG" "$OPTIONAL_PID"',
        "runtime_ap_is_pinned")

rekey_helper = helper[helper.find("do_rekey() {"):helper.find("do_rollback() {")]
ordered(rekey_helper, "AP rekey host-network fence",
        "rekey_request_is_fresh",
        'state_value host_network_signature_before',
        "config_pair_matches_state",
        'host_network_signature)',
        'host network invariants changed before bounded group-rekey',
        'configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID"',
        'required-PMF hostapd process is not exact before bounded group-rekey',
        "watchdog_owner_is_current",
        'rollback watchdog is not exact before bounded group-rekey',
        "record_rekey_request",
        'raw REKEY_GTK',
        'configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID"',
        'required-PMF hostapd process is not exact after bounded group-rekey',
        'host_network_signature)',
        'host network invariants changed during bounded group-rekey',
        'rekey_requested=true')

rekey_receipt = helper[helper.find("rekey_request_is_fresh() {"):
                       helper.find("state_value() {")]
ordered(rekey_receipt, "one-shot AP rekey receipt",
        'rekey.requested',
        'rekey.status',
        "record_rekey_request",
        'rekey_attempted=true',
        "chmod 600")

cleanup = runner[runner.find("cleanup() {"):runner.find("trap cleanup EXIT")]
if '[ -n "$AP_STATE_DIR" ] && [ "$AP_ROLLBACK_VERIFIED" -eq 0 ]' not in cleanup:
    fail("cleanup does not own rollback from every allocated AP state directory")
if 'AP_REQUIRED_ACTIVE" -eq 1' in cleanup:
    fail("cleanup rollback remains gated on the advisory required-active flag")
ordered(cleanup, "runner cleanup rollback ownership",
        'AP_ROLLBACK_ATTEMPTED=1',
        '"$AP_HELPER" --rollback --state-dir "$AP_STATE_DIR"',
        'rollback_verified=true',
        'if [ "$RADIO_OFF_PENDING" -eq 1 ]',
        'remote_radio_power on')

explicit_rollback = main.find('AP_ROLLBACK_ATTEMPTED=1\n"$AP_HELPER" --rollback --state-dir "$AP_STATE_DIR"')
if explicit_rollback < 0:
    fail("normal PMF sequence lacks an explicit rollback-attempt witness")

trace_arm = main.find("TRACE_MAY_BE_ARMED=1")
trace_reset = main.find("capture_trace_client reset reset")
if not (0 <= trace_arm < trace_reset):
    fail("trace cleanup is not armed before the reset request")

for token in ("wpa_passphrase", "optional_ssid", "required_ssid",
              "optional_passphrase", "required_passphrase"):
    if re.search(rf"printf[^\n]*{re.escape(token)}", helper):
        fail(f"AP helper renders credential or wireless identity: {token}")

for token in (
    "initial active prefix", "does not establish final success",
    "rollback watchdog", "local-only", "does not prove pure SAE",
    "precondition failure", "fresh disposable overlay", "REKEY_GTK",
    "tahoe_prepare_disposable_overlay.sh", "local-only receipt",
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
