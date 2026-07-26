#!/usr/bin/env bash
# Static safety contract for the bounded local LabAP BSS switcher.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$ROOT/scripts/tahoe_labap_bss_switcher.sh"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

require() {
    grep -Fq -- "$1" "$SCRIPT" || fail "missing: $1"
}

forbid() {
    if grep -Fq -- "$1" "$SCRIPT"; then
        fail "forbidden side-effect surface: $1"
    fi
}

test -f "$SCRIPT" || fail "switcher missing"
bash -n "$SCRIPT" || fail "switcher is not syntactically valid bash"

for token in \
    'AP_IF="wlp0s20f3"' \
    'STA_IF="sta0"' \
    'EXPECTED_LIVE_SSID="AIAMlab6235"' \
    'TEST_SSID="LabAP"' \
    'DIRECT_TEST_SSID="IWNDirectJoin"' \
    'ACTIVE_TEST_SSID="$TEST_SSID"' \
    "ignore_broadcast_ssid=0" \
    '--direct-join' \
    'direct join requires credential stdin' \
    'stable_direct_test_ssid_absence' \
    'direct join test does not authorize withdrawal' \
    'schema=tahoe-labap-bss-switch/v3' \
    'schema=tahoe-labap-bss-switch/v2' \
    'tahoe-labap-bss-switch/v1' \
    'load_test_mode_from_state' \
    '"$SELF" --rollback --state-dir "$STATE_DIR" --from-watchdog' \
    'LABAP_FREQ_24_CH9=2452' \
    'LABAP_FREQ_24_CH13=2472' \
    'LABAP_FREQ_5_CH149=5745' \
    'LABAP_FREQ_5_CH153=5765' \
    'LABAP_FREQ_5_CH177=5885' \
    '-v freq24_ch9="$LABAP_FREQ_24_CH9"' \
    '-v freq24_ch13="$LABAP_FREQ_24_CH13"' \
    '-v freq5_ch149="$LABAP_FREQ_5_CH149"' \
    '-v freq5_ch153="$LABAP_FREQ_5_CH153"' \
    '-v freq5_ch177="$LABAP_FREQ_5_CH177"' \
    '"$LABAP_FREQ_24_CH9"|"$LABAP_FREQ_24_CH13"' \
    '"$LABAP_FREQ_5_CH149"|"$LABAP_FREQ_5_CH153"|"$LABAP_FREQ_5_CH177"' \
    'EXPECTED_IW_VERSION="iw version 6.7"' \
    'LABAP_TOPOLOGY_SAMPLES=2' \
    'LABAP_TOPOLOGY_INTERVAL_SECONDS=1' \
    'scan flush passive' \
    'TOPOLOGY_PARSER=' \
    '--credential-stdin' \
    'IFS= read -r -s passphrase' \
    'generated_passphrase' \
    'scan_external_labap_topology' \
    'stable_external_labap_topology' \
    'SCAN_EXTERNAL_CROSS_BAND_PAIRS' \
    'grep -Fxq -- "$pair" <<<"$second_pairs"' \
    'external_band_count' \
    'external LabAP passive topology is not stable across two scans' \
    'sta_interface_is_up' \
    'must already be administratively UP' \
    'scan_for_lar_country' \
    'ensure_ap_channel_ir' \
    'LAR_STABILITY_POLLS=2' \
    'consecutive post-scan regulatory polls' \
    'ap_phy_country' \
    'ap_channel_allows_ir' \
    'no[- ]ir' \
    'process_exe' \
    'hostapd_process_matches' \
    'hostapd_status_is' \
    'runtime_ap_is_pinned' \
    'live_config_matches_state' \
    'LABAP_BSS_WATCHDOG_READY' \
    'setsid "$SELF" --watchdog' \
    'while marker_matches_state' \
    'start_watchdog' \
    'activation.phase' \
    'record_activation_phase' \
    'arm_activate_signal_recovery' \
    'disarm_activate_signal_recovery' \
    'recover_after_activate_signal' \
    'interrupted-HUP' \
    'interrupted-INT' \
    'interrupted-TERM' \
    'clear_own_watchdog_receipt' \
    'stop_exact_hostapd "$(test_config)"' \
    'LABAP_BSS_SWITCH=WITHDRAWN' \
    'LABAP_BSS_SWITCH=ORIGINAL_RESTORED'; do
    require "$token"
done

# The catchable-signal handler must be armed only after the rollback state is
# durable, but before the marker appears.  The exact owner must still be
# ready before the first hostapd stop, and successful promotion must disarm
# the foreground-only handler so the independent watchdog remains the owner.
awk '
    /^do_activate\(\)/ { in_activate = 1 }
    in_activate && /write_state armed/ { write_state_line = NR }
    in_activate && /^[[:space:]]*arm_activate_signal_recovery$/ { arm_line = NR }
    in_activate && /write_marker/ { marker_line = NR }
    in_activate && /start_watchdog/ { watchdog_line = NR }
    in_activate && /stop_exact_hostapd "\$LIVE_CONFIG"/ { stop_line = NR }
    in_activate && /set_state "\$active_state"/ { promote_line = NR }
    in_activate && /disarm_activate_signal_recovery/ { disarm_line = NR }
    END {
        exit !(write_state_line < arm_line && arm_line < marker_line &&
            marker_line < watchdog_line && watchdog_line < stop_line &&
            promote_line < disarm_line)
    }
' "$SCRIPT" || fail "activation signal-recovery ordering is unsafe"

# No password argv/env mechanism and no broad system-network control path.
for token in \
    'LABAP_PSK=' \
    '--psk' \
    '--ssid' \
    'nmcli' \
    'dnsmasq' \
    'iw reg set' \
    'iptables' \
    'ip addr flush' \
    'ip route replace' \
    'sysctl -w' \
    'pkill' \
    'killall' \
    'rm -rf' \
    'hostapd -t'; do
    forbid "$token"
done

bash "$ROOT/scripts/test_tahoe_labap_topology_parser.sh" ||
    fail "topology parser fixtures failed"
bash "$ROOT/scripts/test_tahoe_labap_bss_status_contract.sh" ||
    fail "hash-only status contract failed"

# The watchdog must be able to invoke its child rollback without being rejected
# by argument validation.  An empty, private state directory makes the child
# fail before any host/network mutation, which distinguishes parser acceptance
# from a real rollback authorization.
contract_state="$(mktemp -d /tmp/aiam-labap-bss-switch.contract.XXXXXX)"
chmod 700 "$contract_state"
contract_output="$contract_state/rollback.out"
if "$SCRIPT" --rollback --state-dir "$contract_state" --from-watchdog \
    >"$contract_output" 2>&1; then
    rmdir "$contract_state"
    fail "watchdog rollback fixture unexpectedly succeeded"
fi
grep -Fq 'state test mode is invalid' "$contract_output" || {
    rm -f "$contract_output"
    rmdir "$contract_state"
    fail "watchdog rollback argv was rejected before state validation"
}
rm -f "$contract_output"
rmdir "$contract_state"

printf 'PASS: Tahoe LabAP BSS switcher safety contract\n'
