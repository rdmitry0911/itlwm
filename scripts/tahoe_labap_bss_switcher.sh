#!/usr/bin/env bash
# Bounded one-at-a-time local BSS replacement for the Tahoe LabAP scan test.
#
# The host AX211 advertises only one AP-type interface, so this helper never
# tries multi-BSS or creates a second VIF.  It temporarily replaces the pinned
# AIAMlab6235 hostapd process with a WPA2-PSK LabAP BSS on the same interface.
# An independently supervised watchdog restores the original BSS if the
# invoking process is interrupted.  It never changes addresses, routes, DHCP,
# DNS, forwarding, NetworkManager, or host reboot state.
# The normal lab launcher keeps sta0 administratively UP for firmware LAR;
# this helper requires and preserves that pre-existing state rather than
# changing it as an unrecorded side effect.
#
# `--activate` defaults to a randomly generated, ephemeral test passphrase.
# `--credential-stdin` is available only for a later real-credential run; it
# reads one WPA2 passphrase from stdin without placing it in argv, environment,
# state text, or output.  The staged hostapd config is mode 600 and is removed
# only after a verified rollback.
set -euo pipefail
umask 077

SELF="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")"
TOPOLOGY_PARSER="$(dirname -- "$SELF")/tahoe_labap_topology_parser.awk"

AP_IF="wlp0s20f3"
STA_IF="sta0"
HOSTAPD="/usr/sbin/hostapd"
HOSTAPD_CLI="/usr/sbin/hostapd_cli"
IW="/usr/sbin/iw"
IP="/usr/sbin/ip"
SYSCTL="/usr/sbin/sysctl"
SUDO="/usr/bin/sudo"
LIVE_CONFIG="/home/dima/Projects/ax211-5g-ap/hostapd-5g.conf"
LIVE_PID="/tmp/itlwm-lab-ap/hostapd-5g.pid"
LIVE_LOG="/tmp/itlwm-lab-ap/hostapd-5g.log"
CONTROL_DIR="/tmp/aiam-labap-bss-switch-control"
STATE_PREFIX="/tmp/aiam-labap-bss-switch."
MARKER="$CONTROL_DIR/active.state"
LOCK="$CONTROL_DIR/switchover.lock"

EXPECTED_LIVE_SSID="AIAMlab6235"
TEST_SSID="LabAP"
DIRECT_TEST_SSID="IWNDirectJoin"
TEST_MODE="labap"
ACTIVE_TEST_SSID="$TEST_SSID"
EXPECTED_CHANNEL=153
EXPECTED_WIDTH=80
EXPECTED_CENTER=5775
# The user-provided LabAP topology may use only the audited fixed allow-list:
# 2.4 GHz channels 9/13 (2452/2472 MHz), and 5 GHz channels 149/153/177
# (5745/5765/5885 MHz).  The passive host scan is broad because iw 6.7 cannot
# combine a frequency list with passive mode; the parser and the in-memory
# classifier reject every other frequency.
LABAP_FREQ_24_CH9=2452
LABAP_FREQ_24_CH13=2472
LABAP_FREQ_5_CH149=5745
LABAP_FREQ_5_CH153=5765
LABAP_FREQ_5_CH177=5885
EXPECTED_IW_VERSION="iw version 6.7"
LABAP_TOPOLOGY_SAMPLES=2
LABAP_TOPOLOGY_INTERVAL_SECONDS=1
LEASE_SECONDS=180
LEASE_SECONDS_EXPLICIT=0
LAR_SCAN_ATTEMPTS=30
LAR_SCAN_INTERVAL_SECONDS=2
LAR_STABILITY_POLLS=2
# The independently supervised setup window is deliberately separate from
# the on-air recovery lease.  It covers the permitted slow LAR scan path
# (bounded at 120 seconds in this lab) plus the exact hostapd handoff and
# state-promotion checks.  Callers cannot extend it.
SETUP_DEADLINE_SECONDS=180
MODE=""
STATE_DIR=""
CREDENTIAL_STDIN=0
DIRECT_JOIN=0
FROM_WATCHDOG=0
WATCHDOG_READY_FD=""

usage() {
    cat >&2 <<'EOF'
usage: tahoe_labap_bss_switcher.sh --preflight
       tahoe_labap_bss_switcher.sh --activate --state-dir /tmp/aiam-labap-bss-switch.NAME \
         [--lease-seconds 60..300] [--credential-stdin] [--direct-join]
       tahoe_labap_bss_switcher.sh --withdraw --state-dir /tmp/aiam-labap-bss-switch.NAME
       tahoe_labap_bss_switcher.sh --rollback --state-dir /tmp/aiam-labap-bss-switch.NAME
       tahoe_labap_bss_switcher.sh --status --state-dir /tmp/aiam-labap-bss-switch.NAME
       tahoe_labap_bss_switcher.sh --retire --state-dir /tmp/aiam-labap-bss-switch.NAME

The helper replaces only the pinned local hostapd BSS.  --withdraw stops only
the temporary LabAP BSS, leaving any independently operated OpenWrt LabAP BSS
available as the controlled-failure candidates.  Rollback restores AIAMlab6235.
The pre-existing sta0 LAR scan interface must already be administratively UP.
For a controlled withdrawal, two fresh passive scans must find at least two
external LabAP BSSes across both the 2.4 and 5 GHz bands before this helper
can replace the local BSS.
`--direct-join` instead uses one hard-coded, unique pure-WPA2 BSS and does
not claim or require the external multi-BSS topology.  It is only for a
bounded public-CoreWLAN manual-join experiment.
EOF
}

die() {
    printf 'LABAP_BSS_SWITCH_FAIL:%s\n' "$*" >&2
    exit 1
}

sudo_cmd() {
    "$SUDO" -n "$@"
}

is_canonical_decimal() {
    # Bash arithmetic treats a leading zero as octal.  Every state value used
    # in arithmetic must therefore have canonical base-10 spelling.
    case "$1" in ''|0[0-9]*|*[!0-9]*) return 1;; esac
}

is_canonical_positive_decimal() {
    is_canonical_decimal "$1" && [ "$1" -gt 0 ]
}

is_decimal_in_range() {
    local value="$1" minimum="$2" maximum="$3"
    is_canonical_decimal "$value" || return 1
    [ "$value" -ge "$minimum" ] && [ "$value" -le "$maximum" ]
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --preflight|--activate|--withdraw|--rollback|--status|--retire)
            [ -z "$MODE" ] || { usage; exit 2; }
            MODE="${1#--}"
            shift
            ;;
        --watchdog)
            [ -z "$MODE" ] || { usage; exit 2; }
            MODE=watchdog
            shift
            ;;
        --state-dir)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            STATE_DIR="$2"
            shift 2
            ;;
        --lease-seconds)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            LEASE_SECONDS="$2"
            LEASE_SECONDS_EXPLICIT=1
            shift 2
            ;;
        --credential-stdin)
            CREDENTIAL_STDIN=1
            shift
            ;;
        --direct-join)
            DIRECT_JOIN=1
            shift
            ;;
        --from-watchdog)
            FROM_WATCHDOG=1
            shift
            ;;
        --ready-fd)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            WATCHDOG_READY_FD="$2"
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

[ -n "$MODE" ] || { usage; exit 2; }
is_decimal_in_range "$LEASE_SECONDS" 60 300 || { usage; exit 2; }
is_decimal_in_range "$SETUP_DEADLINE_SECONDS" 120 180 || { usage; exit 2; }
if [ "$DIRECT_JOIN" -eq 1 ]; then
    [ "$MODE" = activate ] || { usage; exit 2; }
    TEST_MODE=direct
    ACTIVE_TEST_SSID="$DIRECT_TEST_SSID"
fi
case "$MODE" in
    preflight) [ -z "$STATE_DIR" ] && [ "$CREDENTIAL_STDIN" -eq 0 ] && [ "$FROM_WATCHDOG" -eq 0 ] || { usage; exit 2; };;
    activate) [ -n "$STATE_DIR" ] && [ "$FROM_WATCHDOG" -eq 0 ] || { usage; exit 2; };;
    withdraw) [ -n "$STATE_DIR" ] && [ "$CREDENTIAL_STDIN" -eq 0 ] && [ "$FROM_WATCHDOG" -eq 0 ] || { usage; exit 2; };;
    rollback) [ -n "$STATE_DIR" ] && [ "$CREDENTIAL_STDIN" -eq 0 ] || { usage; exit 2; };;
    status) [ -n "$STATE_DIR" ] && [ "$CREDENTIAL_STDIN" -eq 0 ] && [ "$DIRECT_JOIN" -eq 0 ] && [ "$FROM_WATCHDOG" -eq 0 ] && [ -z "$WATCHDOG_READY_FD" ] && [ "$LEASE_SECONDS_EXPLICIT" -eq 0 ] || { usage; exit 2; };;
    retire) [ -n "$STATE_DIR" ] && [ "$CREDENTIAL_STDIN" -eq 0 ] && [ "$DIRECT_JOIN" -eq 0 ] && [ "$FROM_WATCHDOG" -eq 0 ] && [ -z "$WATCHDOG_READY_FD" ] && [ "$LEASE_SECONDS_EXPLICIT" -eq 0 ] || { usage; exit 2; };;
    watchdog)
        [ -n "$STATE_DIR" ] && [ "$CREDENTIAL_STDIN" -eq 0 ] && [ "$DIRECT_JOIN" -eq 0 ] || { usage; exit 2; }
        case "$WATCHDOG_READY_FD" in ''|8) ;; *) usage; exit 2;; esac
        ;;
esac

[ "$AP_IF" = "wlp0s20f3" ] || die "AP interface is not pinned"
[ "$STA_IF" = "sta0" ] || die "scan interface is not pinned"

state_file() { printf '%s/state.txt\n' "$STATE_DIR"; }
test_config() { printf '%s/labap-hostapd.conf\n' "$STATE_DIR"; }
test_pid() { printf '%s/labap-hostapd.pid\n' "$STATE_DIR"; }
test_log() { printf '%s/labap-hostapd.log\n' "$STATE_DIR"; }
watchdog_pid_file() { printf '%s/watchdog.pid\n' "$STATE_DIR"; }
activation_phase_file() { printf '%s/activation.phase\n' "$STATE_DIR"; }

require_state_dir() {
    local resolved owner mode
    case "$STATE_DIR" in "$STATE_PREFIX"*) ;; *) die "state directory is outside the restricted prefix";; esac
    [ -d "$STATE_DIR" ] && [ ! -L "$STATE_DIR" ] || die "state directory is missing or symlinked"
    resolved="$(cd -P -- "$STATE_DIR" && pwd)"
    [ "$resolved" = "$STATE_DIR" ] || die "state directory is not canonical"
    owner="$(stat -c %u -- "$STATE_DIR")" || die "state directory owner is unreadable"
    [ "$owner" = "$(id -u)" ] || die "state directory owner is not the invoking user"
    mode="$(stat -c %a -- "$STATE_DIR")" || die "state directory mode is unreadable"
    [ "$mode" = 700 ] || die "state directory must be mode 700"
}

state_value() {
    local key="$1"
    [ -f "$(state_file)" ] && [ ! -L "$(state_file)" ] || return 1
    awk -F= -v wanted="$key" '
        $1 == wanted { if (++seen == 1) value = $2; else exit 2 }
        END { if (seen == 1 && value != "") print value; else exit 1 }
    ' "$(state_file)"
}

is_hex64() {
    [[ "$1" =~ ^[0-9a-f]{64}$ ]]
}

monotonic_uptime_seconds() {
    awk '
        NR == 1 && $1 ~ /^[0-9]+([.][0-9]+)?$/ {
            split($1, part, ".")
            if (part[1] !~ /^[0-9]+$/)
                exit 1
            print part[1]
            found = 1
            exit
        }
        END { if (!found) exit 1 }
    ' /proc/uptime
}

canonical_bssid() {
    local value="$1"
    [[ "$value" =~ ^([0-9A-Fa-f][0-9A-Fa-f]:){5}[0-9A-Fa-f][0-9A-Fa-f]$ ]] || return 1
    printf '%s\n' "${value,,}"
}

opaque_sha256() {
    local value="$1" digest
    digest="$(printf '%s' "$value" | sha256sum | awk '
        NR == 1 && NF == 2 && $1 ~ /^[0-9a-f]{64}$/ { print $1; next }
        { invalid = 1 }
        END { if (NR != 1 || invalid) exit 1 }
    ')" || return 1
    is_hex64 "$digest" || return 1
    printf '%s\n' "$digest"
}

select_test_mode() {
    case "$1" in
        labap)
            TEST_MODE=labap
            ACTIVE_TEST_SSID="$TEST_SSID"
            ;;
        direct)
            TEST_MODE=direct
            ACTIVE_TEST_SSID="$DIRECT_TEST_SSID"
            ;;
        *)
            return 1
            ;;
    esac
}

load_test_mode_from_state() {
    local mode schema
    mode="$(state_value test_mode 2>/dev/null || true)"
    if [ -z "$mode" ]; then
        schema="$(state_value schema 2>/dev/null || true)"
        [ "$schema" = tahoe-labap-bss-switch/v1 ] || return 1
        mode=labap
    fi
    select_test_mode "$mode"
}

write_state_v2() {
    local state="$1" mode="$2" network="$3" fingerprint="$4" bssid="$5" external_count="$6" tmp
    case "$mode" in labap|direct) ;; *) return 1;; esac
    tmp="$STATE_DIR/.state.$$"
    [ ! -e "$tmp" ] && [ ! -L "$tmp" ] || return 1
    {
        printf 'schema=tahoe-labap-bss-switch/v2\n'
        printf 'state=%s\n' "$state"
        printf 'test_mode=%s\n' "$mode"
        printf 'network_signature_before=%s\n' "$network"
        printf 'live_config_fingerprint=%s\n' "$fingerprint"
        printf 'live_bssid_before=%s\n' "$bssid"
        printf 'external_labap_bss_count=%s\n' "$external_count"
    } >"$tmp" || return 1
    chmod 600 "$tmp" || return 1
    mv -f -- "$tmp" "$(state_file)"
}

write_state_v3() {
    local state="$1" mode="$2" network="$3" fingerprint="$4" bssid="$5" external_count="$6" lease_seconds="$7" lease_deadline="$8" tmp
    case "$mode" in labap|direct) ;; *) return 1;; esac
    is_decimal_in_range "$lease_seconds" 60 300 || return 1
    is_canonical_positive_decimal "$lease_deadline" || return 1
    tmp="$STATE_DIR/.state.$$"
    [ ! -e "$tmp" ] && [ ! -L "$tmp" ] || return 1
    {
        printf 'schema=tahoe-labap-bss-switch/v3\n'
        printf 'state=%s\n' "$state"
        printf 'test_mode=%s\n' "$mode"
        printf 'network_signature_before=%s\n' "$network"
        printf 'live_config_fingerprint=%s\n' "$fingerprint"
        printf 'live_bssid_before=%s\n' "$bssid"
        printf 'external_labap_bss_count=%s\n' "$external_count"
        printf 'lease_seconds=%s\n' "$lease_seconds"
        printf 'lease_not_after_monotonic_seconds=%s\n' "$lease_deadline"
    } >"$tmp" || return 1
    chmod 600 "$tmp" || return 1
    mv -f -- "$tmp" "$(state_file)"
}

write_state() {
    local state="$1" mode="$2" network="$3" fingerprint="$4" bssid="$5" external_count="$6"
    local lease_seconds="$7" deadline_phase="$8" deadline="$9" tmp
    case "$mode" in labap|direct) ;; *) return 1;; esac
    case "$state:$deadline_phase" in
        armed:setup|labap-active:active|direct-active:active|withdrawn:active|\
        original-restored:setup|original-restored:active) ;;
        *) return 1;;
    esac
    is_decimal_in_range "$lease_seconds" 60 300 || return 1
    is_canonical_positive_decimal "$deadline" || return 1
    tmp="$STATE_DIR/.state.$$"
    [ ! -e "$tmp" ] && [ ! -L "$tmp" ] || return 1
    {
        printf 'schema=tahoe-labap-bss-switch/v4\n'
        printf 'state=%s\n' "$state"
        printf 'test_mode=%s\n' "$mode"
        printf 'network_signature_before=%s\n' "$network"
        printf 'live_config_fingerprint=%s\n' "$fingerprint"
        printf 'live_bssid_before=%s\n' "$bssid"
        printf 'external_labap_bss_count=%s\n' "$external_count"
        printf 'lease_seconds=%s\n' "$lease_seconds"
        printf 'deadline_phase=%s\n' "$deadline_phase"
        printf 'lease_not_after_monotonic_seconds=%s\n' "$deadline"
    } >"$tmp" || return 1
    chmod 600 "$tmp" || return 1
    mv -f -- "$tmp" "$(state_file)"
}

set_state() {
    local next="$1" mode network fingerprint bssid external_count schema lease_seconds deadline_phase lease_deadline
    load_test_mode_from_state || return 1
    mode="$TEST_MODE"
    network="$(state_value network_signature_before)" || return 1
    fingerprint="$(state_value live_config_fingerprint)" || return 1
    bssid="$(state_value live_bssid_before)" || return 1
    external_count="$(state_value external_labap_bss_count)" || return 1
    schema="$(state_value schema)" || return 1
    case "$schema" in
        tahoe-labap-bss-switch/v4)
            lease_seconds="$(state_value lease_seconds)" || return 1
            deadline_phase="$(state_value deadline_phase)" || return 1
            lease_deadline="$(state_value lease_not_after_monotonic_seconds)" || return 1
            write_state "$next" "$mode" "$network" "$fingerprint" "$bssid" "$external_count" "$lease_seconds" "$deadline_phase" "$lease_deadline"
            ;;
        tahoe-labap-bss-switch/v3)
            lease_seconds="$(state_value lease_seconds)" || return 1
            lease_deadline="$(state_value lease_not_after_monotonic_seconds)" || return 1
            write_state_v3 "$next" "$mode" "$network" "$fingerprint" "$bssid" "$external_count" "$lease_seconds" "$lease_deadline"
            ;;
        tahoe-labap-bss-switch/v1|tahoe-labap-bss-switch/v2)
            write_state_v2 "$next" "$mode" "$network" "$fingerprint" "$bssid" "$external_count"
            ;;
        *)
            return 1
            ;;
    esac
}

state_deadline_is_current() {
    local expected_state="$1" expected_phase="$2" maximum_seconds="$3"
    local schema state deadline_phase lease_seconds deadline now remaining

    is_canonical_positive_decimal "$maximum_seconds" || return 1
    schema="$(state_value schema)" || return 1
    state="$(state_value state)" || return 1
    deadline_phase="$(state_value deadline_phase)" || return 1
    lease_seconds="$(state_value lease_seconds)" || return 1
    deadline="$(state_value lease_not_after_monotonic_seconds)" || return 1
    [ "$schema" = tahoe-labap-bss-switch/v4 ] || return 1
    [ "$state" = "$expected_state" ] || return 1
    [ "$deadline_phase" = "$expected_phase" ] || return 1
    is_decimal_in_range "$lease_seconds" 60 300 || return 1
    is_canonical_positive_decimal "$deadline" || return 1
    now="$(monotonic_uptime_seconds)" || return 1
    is_canonical_decimal "$now" || return 1
    [ "$deadline" -gt "$now" ] || return 1
    remaining=$((deadline - now))
    [ "$remaining" -gt 0 ] && [ "$remaining" -le "$maximum_seconds" ]
}

setup_deadline_is_current() {
    state_deadline_is_current armed setup "$SETUP_DEADLINE_SECONDS"
}

active_lease_is_current() {
    local expected_state="$1" schema lease_seconds deadline now remaining

    case "$expected_state" in labap-active|direct-active|withdrawn) ;; *) return 1;; esac
    schema="$(state_value schema)" || return 1
    case "$schema" in
        tahoe-labap-bss-switch/v4)
            lease_seconds="$(state_value lease_seconds)" || return 1
            is_decimal_in_range "$lease_seconds" 60 300 || return 1
            state_deadline_is_current "$expected_state" active "$lease_seconds"
            ;;
        tahoe-labap-bss-switch/v3)
            [ "$(state_value state)" = "$expected_state" ] || return 1
            lease_seconds="$(state_value lease_seconds)" || return 1
            deadline="$(state_value lease_not_after_monotonic_seconds)" || return 1
            is_decimal_in_range "$lease_seconds" 60 300 || return 1
            is_canonical_positive_decimal "$deadline" || return 1
            now="$(monotonic_uptime_seconds)" || return 1
            is_canonical_decimal "$now" || return 1
            [ "$deadline" -gt "$now" ] || return 1
            remaining=$((deadline - now))
            [ "$remaining" -gt 0 ] && [ "$remaining" -le "$lease_seconds" ]
            ;;
        *)
            return 1
            ;;
    esac
}

promote_active_state() {
    local next="$1" mode network fingerprint bssid external_count schema state deadline_phase
    local lease_seconds setup_deadline now remaining active_deadline

    load_test_mode_from_state || return 1
    mode="$TEST_MODE"
    case "$mode:$next" in labap:labap-active|direct:direct-active) ;; *) return 1;; esac
    schema="$(state_value schema)" || return 1
    state="$(state_value state)" || return 1
    deadline_phase="$(state_value deadline_phase)" || return 1
    network="$(state_value network_signature_before)" || return 1
    fingerprint="$(state_value live_config_fingerprint)" || return 1
    bssid="$(state_value live_bssid_before)" || return 1
    external_count="$(state_value external_labap_bss_count)" || return 1
    lease_seconds="$(state_value lease_seconds)" || return 1
    setup_deadline="$(state_value lease_not_after_monotonic_seconds)" || return 1
    [ "$schema" = tahoe-labap-bss-switch/v4 ] || return 1
    [ "$state" = armed ] || return 1
    [ "$deadline_phase" = setup ] || return 1
    is_decimal_in_range "$lease_seconds" 60 300 || return 1
    is_canonical_positive_decimal "$setup_deadline" || return 1
    now="$(monotonic_uptime_seconds)" || return 1
    is_canonical_decimal "$now" || return 1
    [ "$setup_deadline" -gt "$now" ] || return 1
    remaining=$((setup_deadline - now))
    [ "$remaining" -gt 0 ] && [ "$remaining" -le "$SETUP_DEADLINE_SECONDS" ] || return 1
    active_deadline=$((now + lease_seconds))
    [ "$active_deadline" -gt "$now" ] || return 1
    write_state "$next" "$mode" "$network" "$fingerprint" "$bssid" "$external_count" "$lease_seconds" active "$active_deadline"
}

# This is diagnostic-only: it deliberately stores one fixed phase token, not
# the BSS identity, credential, or any host/network value.  Unlike state.txt,
# it remains after a verified rollback so an interrupted foreground activation
# can be distinguished from a normal short observation of state=armed.
record_activation_phase() {
    local phase="$1" path tmp
    case "$phase" in
        state-written|marker-published|watchdog-ready|pre-live-stop|live-stopped|test-started|promoted|\
        interrupted-HUP|interrupted-INT|interrupted-TERM) ;;
        *) return 1;;
    esac
    path="$(activation_phase_file)"
    [ ! -L "$path" ] || return 1
    tmp="$STATE_DIR/.activation-phase.$$"
    [ ! -e "$tmp" ] && [ ! -L "$tmp" ] || return 1
    printf 'phase=%s\n' "$phase" >"$tmp" || return 1
    chmod 600 "$tmp" || return 1
    mv -f -- "$tmp" "$path"
}

live_config_fingerprint() {
    sudo_cmd sha256sum "$LIVE_CONFIG" | awk 'NF >= 1 && $1 ~ /^[0-9a-f]{64}$/ { print $1; exit }'
}

host_network_signature() {
    {
        sudo_cmd "$IP" -4 route show default
        sudo_cmd "$IP" -6 route show default
        sudo_cmd "$IP" -4 -o addr show dev "$AP_IF"
        sudo_cmd "$IP" -6 -o addr show dev "$AP_IF"
        sudo_cmd "$SYSCTL" -n net.ipv4.ip_forward
        sudo_cmd "$SYSCTL" -n net.ipv6.conf.all.forwarding
    } | sha256sum | awk 'NF == 2 && $1 ~ /^[0-9a-f]{64}$/ { print $1; exit }'
}

config_value() {
    local config="$1" key="$2"
    sudo_cmd awk -v wanted="$key" '
        index($0, wanted "=") == 1 {
            if (++seen != 1) exit 2
            value = substr($0, length(wanted) + 2)
        }
        END { if (seen == 1 && value != "") print value; else exit 1 }
    ' "$config"
}

expect_live_value() {
    local key="$1" expected="$2" value
    value="$(config_value "$LIVE_CONFIG" "$key")" || return 1
    [ "$value" = "$expected" ]
}

validate_live_config() {
    [ -f "$LIVE_CONFIG" ] && [ ! -L "$LIVE_CONFIG" ] || return 1
    expect_live_value interface "$AP_IF" || return 1
    expect_live_value driver nl80211 || return 1
    expect_live_value ssid "$EXPECTED_LIVE_SSID" || return 1
    expect_live_value hw_mode a || return 1
    expect_live_value channel 149 || return 1
    expect_live_value ieee80211n 1 || return 1
    expect_live_value ieee80211ac 1 || return 1
    expect_live_value vht_oper_chwidth 1 || return 1
    expect_live_value vht_oper_centr_freq_seg0_idx 155 || return 1
    expect_live_value wpa 2 || return 1
    expect_live_value wpa_key_mgmt 'WPA-PSK WPA-PSK-SHA256 SAE' || return 1
    expect_live_value rsn_pairwise CCMP || return 1
    expect_live_value ieee80211w 1 || return 1
    expect_live_value ctrl_interface /run/hostapd || return 1
    [ "$(sudo_cmd grep -c '^wpa_passphrase=' "$LIVE_CONFIG")" = 1 ] || return 1
    ! sudo_cmd grep -q '^bss=' "$LIVE_CONFIG"
}

pid_from_file() {
    local pidfile="$1" pid
    [ -r "$pidfile" ] && [ ! -L "$pidfile" ] || return 1
    pid="$(sudo_cmd cat "$pidfile" 2>/dev/null | tr -d '[:space:]')" || return 1
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] || return 1
    printf '%s\n' "$pid"
}

process_args() { sudo_cmd ps -p "$1" -o args= 2>/dev/null; }
process_uid() { sudo_cmd ps -p "$1" -o uid= 2>/dev/null | tr -d '[:space:]'; }
process_exe() { sudo_cmd readlink -f "/proc/$1/exe" 2>/dev/null; }

hostapd_process_matches() {
    local pid="$1" config="$2" pidfile="$3" logfile="$4" args uid exe
    sudo_cmd kill -0 "$pid" 2>/dev/null || return 1
    uid="$(process_uid "$pid")" || return 1
    [ "$uid" = 0 ] || return 1
    exe="$(process_exe "$pid")" || return 1
    [ "$exe" = "$HOSTAPD" ] || return 1
    args="$(process_args "$pid")" || return 1
    [[ " $args " == *" -B "* ]] || return 1
    [[ " $args " == *" -P $pidfile "* ]] || return 1
    [[ " $args " == *" -f $logfile "* ]] || return 1
    [[ " $args " == *" $config "* ]]
}

hostapd_status_snapshot() {
    sudo_cmd "$HOSTAPD_CLI" -p /run/hostapd -i "$AP_IF" status 2>/dev/null
}

hostapd_status_snapshot_is() {
    local expected_ssid="$1" status="$2"
    printf '%s\n' "$status" | grep -Fxq 'state=ENABLED' || return 1
    printf '%s\n' "$status" | grep -Fxq "ssid[0]=$expected_ssid"
}

hostapd_status_snapshot_bssid() {
    local status="$1"
    printf '%s\n' "$status" | awk -F= '
        $1 == "bssid[0]" {
            if (++seen != 1 || $2 == "")
                exit 1
            value = $2
        }
        END { if (seen != 1 || value == "") exit 1; print value }
    '
}

hostapd_status_is() {
    local expected_ssid="$1" status
    status="$(hostapd_status_snapshot)" || return 1
    hostapd_status_snapshot_is "$expected_ssid" "$status"
}

runtime_ap_is_pinned() {
    sudo_cmd "$IW" dev "$AP_IF" info 2>/dev/null | awk \
        -v iface="$AP_IF" -v channel="$EXPECTED_CHANNEL" \
        -v width="$EXPECTED_WIDTH" -v center="$EXPECTED_CENTER" '
        $1 == "Interface" && $2 == iface { saw_interface = 1 }
        $1 == "type" && $2 == "AP" { saw_type = 1 }
        $1 == "channel" && $2 == channel &&
            $0 ~ ("width: " width " MHz") &&
            $0 ~ ("center1: " center " MHz") { saw_channel = 1 }
        END { exit !(saw_interface && saw_type && saw_channel) }
    '
}

runtime_bssid() {
    sudo_cmd "$IW" dev "$AP_IF" info 2>/dev/null | awk '/^[[:space:]]*addr / { print $2; exit }'
}

live_hostapd_active() {
    local pid
    pid="$(pid_from_file "$LIVE_PID")" || return 1
    hostapd_process_matches "$pid" "$LIVE_CONFIG" "$LIVE_PID" "$LIVE_LOG" &&
        hostapd_status_is "$EXPECTED_LIVE_SSID" && runtime_ap_is_pinned
}

test_hostapd_process_active() {
    local pid
    pid="$(pid_from_file "$(test_pid)")" || return 1
    hostapd_process_matches "$pid" "$(test_config)" "$(test_pid)" "$(test_log)"
}

test_hostapd_active() {
    test_hostapd_process_active &&
        hostapd_status_is "$ACTIVE_TEST_SSID" && runtime_ap_is_pinned
}

wait_for_hostapd() {
    local kind="$1" attempt
    for attempt in $(seq 1 20); do
        case "$kind" in
            live) live_hostapd_active && return 0;;
            test) test_hostapd_active && return 0;;
        esac
        sleep 1
    done
    return 1
}

stop_exact_hostapd() {
    local config="$1" pidfile="$2" logfile="$3" pid attempt
    pid="$(pid_from_file "$pidfile")" || return 1
    hostapd_process_matches "$pid" "$config" "$pidfile" "$logfile" || return 1
    sudo_cmd kill -TERM "$pid"
    for attempt in $(seq 1 20); do
        if ! sudo_cmd kill -0 "$pid" 2>/dev/null; then
            if [ -e "$pidfile" ] && [ ! -L "$pidfile" ]; then
                sudo_cmd unlink "$pidfile" || return 1
            fi
            [ ! -e "$pidfile" ] && [ ! -L "$pidfile" ] && return 0
        fi
        sleep 1
    done
    return 1
}

start_exact_hostapd() {
    local config="$1" pidfile="$2" logfile="$3" kind="$4"
    [ ! -e "$pidfile" ] && [ ! -L "$pidfile" ] || return 1
    ensure_ap_channel_ir || return 1
    sudo_cmd "$HOSTAPD" -B -P "$pidfile" -f "$logfile" "$config" 9>&-
    wait_for_hostapd "$kind"
}

valid_wpa_passphrase() {
    local passphrase="$1"
    [ "${#passphrase}" -ge 8 ] && [ "${#passphrase}" -le 63 ] || return 1
    [[ "$passphrase" =~ ^[[:print:]]+$ ]] || return 1
    [[ "$passphrase" != *$'\r'* && "$passphrase" != *$'\n'* ]]
}

generated_passphrase() {
    od -An -N 24 -tx1 /dev/urandom | tr -d '[:space:]'
}

write_test_config() {
    local passphrase="$1" config
    config="$(test_config)"
    [ ! -e "$config" ] && [ ! -L "$config" ] || return 1
    valid_wpa_passphrase "$passphrase" || return 1
    {
        printf 'interface=%s\n' "$AP_IF"
        printf '%s\n' 'driver=nl80211'
        printf 'ssid=%s\n' "$ACTIVE_TEST_SSID"
        printf '%s\n' 'ignore_broadcast_ssid=0'
        printf '%s\n' 'hw_mode=a'
        printf '%s\n' 'channel=149'
        printf '%s\n' 'ieee80211n=1'
        printf '%s\n' 'ht_capab=[HT40+][SHORT-GI-20][SHORT-GI-40]'
        printf '%s\n' 'ieee80211ac=1'
        printf '%s\n' 'vht_capab=[SHORT-GI-80]'
        printf '%s\n' 'vht_oper_chwidth=1'
        printf '%s\n' 'vht_oper_centr_freq_seg0_idx=155'
        printf '%s\n' 'wmm_enabled=1'
        printf '%s\n' 'auth_algs=1'
        printf '%s\n' 'wpa=2'
        printf 'wpa_passphrase=%s\n' "$passphrase"
        printf '%s\n' 'wpa_key_mgmt=WPA-PSK'
        printf '%s\n' 'rsn_pairwise=CCMP'
        printf '%s\n' 'ieee80211w=0'
        printf '%s\n' 'ctrl_interface=/run/hostapd'
        printf '%s\n' 'logger_stdout=-1'
        printf '%s\n' 'logger_syslog=-1'
    } >"$config" || return 1
    chmod 600 "$config"
}

validate_test_config() {
    local config="$(test_config)"
    [ -f "$config" ] && [ ! -L "$config" ] || return 1
    [ "$(stat -c %a -- "$config")" = 600 ] || return 1
    [ "$(grep -c "^interface=$AP_IF$" "$config")" = 1 ] || return 1
    [ "$(grep -c "^ssid=$ACTIVE_TEST_SSID$" "$config")" = 1 ] || return 1
    [ "$(grep -c '^ignore_broadcast_ssid=0$' "$config")" = 1 ] || return 1
    [ "$(grep -c '^ignore_broadcast_ssid=' "$config")" = 1 ] || return 1
    [ "$(grep -c '^wpa=2$' "$config")" = 1 ] || return 1
    [ "$(grep -c '^wpa=' "$config")" = 1 ] || return 1
    [ "$(grep -c '^wpa_key_mgmt=WPA-PSK$' "$config")" = 1 ] || return 1
    [ "$(grep -c '^wpa_key_mgmt=' "$config")" = 1 ] || return 1
    [ "$(grep -c '^rsn_pairwise=CCMP$' "$config")" = 1 ] || return 1
    [ "$(grep -c '^rsn_pairwise=' "$config")" = 1 ] || return 1
    [ "$(grep -c '^ieee80211w=0$' "$config")" = 1 ] || return 1
    [ "$(grep -c '^ieee80211w=' "$config")" = 1 ] || return 1
    [ "$(grep -c '^ctrl_interface=/run/hostapd$' "$config")" = 1 ] || return 1
    [ "$(grep -c '^wpa_passphrase=' "$config")" = 1 ] || return 1
    ! grep -Fq 'SAE' "$config"
}

scan_external_labap_topology() {
    local output records bssid frequency extra bssid24 bssid5
    local -A seen_all=()
    local -A seen24=()
    local -A seen5=()

    SCAN_EXTERNAL_BSS_COUNT=0
    SCAN_EXTERNAL_BAND_COUNT=0
    SCAN_EXTERNAL_CROSS_BAND_PAIRS=""

    sta_interface_is_up || return 1
    [ -f "$TOPOLOGY_PARSER" ] && [ ! -L "$TOPOLOGY_PARSER" ] || return 1
    [ "$("$IW" --version 2>/dev/null)" = "$EXPECTED_IW_VERSION" ] || return 1

    # iw 6.7 documents that a scan without `passive` sends wildcard probes.
    # `flush passive` is supported on this exact host and refuses stale cache
    # data rather than quietly falling back to an active scan.
    output="$(sudo_cmd "$IW" dev "$STA_IF" scan flush passive)" || return 1
    records="$(printf '%s\n' "$output" | awk \
        -v target_ssid="$TEST_SSID" \
        -v freq24_ch9="$LABAP_FREQ_24_CH9" \
        -v freq24_ch13="$LABAP_FREQ_24_CH13" \
        -v freq5_ch149="$LABAP_FREQ_5_CH149" \
        -v freq5_ch153="$LABAP_FREQ_5_CH153" \
        -v freq5_ch177="$LABAP_FREQ_5_CH177" \
        -f "$TOPOLOGY_PARSER")" || return 1

    while IFS=' ' read -r bssid frequency extra; do
        [ -z "$bssid" ] && continue
        [ -n "$frequency" ] && [ -z "$extra" ] || return 1
        case "$frequency" in
            "$LABAP_FREQ_24_CH9"|"$LABAP_FREQ_24_CH13") seen24["$bssid"]=1;;
            "$LABAP_FREQ_5_CH149"|"$LABAP_FREQ_5_CH153"|"$LABAP_FREQ_5_CH177") seen5["$bssid"]=1;;
            *) return 1;;
        esac
        seen_all["$bssid"]=1
    done <<<"$records"

    # One BSSID observed in both frequency slots is not the two-radio
    # topology this controlled-failure fixture requires.  Treat it as an
    # ambiguous parser/RF result rather than constructing a synthetic pair.
    for bssid in "${!seen24[@]}"; do
        [ -z "${seen5[$bssid]+present}" ] || return 1
    done

    SCAN_EXTERNAL_BSS_COUNT="${#seen_all[@]}"
    [ "${#seen24[@]}" -ne 0 ] && SCAN_EXTERNAL_BAND_COUNT=$((SCAN_EXTERNAL_BAND_COUNT + 1))
    [ "${#seen5[@]}" -ne 0 ] && SCAN_EXTERNAL_BAND_COUNT=$((SCAN_EXTERNAL_BAND_COUNT + 1))

    # Keep candidate identities only in process memory so two independent
    # scans can prove a stable cross-band pair.  No BSSID is printed or stored.
    for bssid24 in "${!seen24[@]}"; do
        for bssid5 in "${!seen5[@]}"; do
            [ "$bssid24" != "$bssid5" ] || return 1
            if [ -n "$SCAN_EXTERNAL_CROSS_BAND_PAIRS" ]; then
                SCAN_EXTERNAL_CROSS_BAND_PAIRS+=$'\n'
            fi
            SCAN_EXTERNAL_CROSS_BAND_PAIRS+="${bssid24}|${bssid5}"
        done
    done
}

scan_has_required_external_topology() {
    [ "$SCAN_EXTERNAL_BSS_COUNT" -ge 2 ] || return 1
    [ "$SCAN_EXTERNAL_BAND_COUNT" -eq 2 ] || return 1
    [ -n "$SCAN_EXTERNAL_CROSS_BAND_PAIRS" ]
}

stable_external_labap_topology() {
    local first_pairs second_pairs pair stable_pair=0
    local first_count first_bands

    [ "$LABAP_TOPOLOGY_SAMPLES" -eq 2 ] || return 1
    scan_external_labap_topology || return 1
    scan_has_required_external_topology || return 2
    first_pairs="$SCAN_EXTERNAL_CROSS_BAND_PAIRS"
    first_count="$SCAN_EXTERNAL_BSS_COUNT"
    first_bands="$SCAN_EXTERNAL_BAND_COUNT"

    sleep "$LABAP_TOPOLOGY_INTERVAL_SECONDS"

    scan_external_labap_topology || return 1
    scan_has_required_external_topology || return 2
    second_pairs="$SCAN_EXTERNAL_CROSS_BAND_PAIRS"
    while IFS= read -r pair; do
        [ -n "$pair" ] || continue
        if grep -Fxq -- "$pair" <<<"$second_pairs"; then
            stable_pair=1
            break
        fi
    done <<<"$first_pairs"
    [ "$stable_pair" -eq 1 ] || return 2

    # Report only conservative aggregate facts.  The pair identities used for
    # the intersection go out of scope with this function.
    if [ "$first_count" -lt "$SCAN_EXTERNAL_BSS_COUNT" ]; then
        STABLE_EXTERNAL_BSS_COUNT="$first_count"
    else
        STABLE_EXTERNAL_BSS_COUNT="$SCAN_EXTERNAL_BSS_COUNT"
    fi
    if [ "$first_bands" -lt "$SCAN_EXTERNAL_BAND_COUNT" ]; then
        STABLE_EXTERNAL_BAND_COUNT="$first_bands"
    else
        STABLE_EXTERNAL_BAND_COUNT="$SCAN_EXTERNAL_BAND_COUNT"
    fi
}

direct_test_ssid_absent_once() {
    local output
    sta_interface_is_up || return 1
    [ "$("$IW" --version 2>/dev/null)" = "$EXPECTED_IW_VERSION" ] || return 1
    output="$(sudo_cmd "$IW" dev "$STA_IF" scan flush passive)" || return 1
    if printf '%s\n' "$output" | awk -v target="$DIRECT_TEST_SSID" '
        $1 == "SSID:" {
            value = substr($0, index($0, ":") + 1)
            sub(/^[[:space:]]*/, "", value)
            if (value == target) found = 1
        }
        END { exit found ? 1 : 0 }
    '; then
        return 0
    fi
    return 1
}

stable_direct_test_ssid_absence() {
    direct_test_ssid_absent_once || return 1
    sleep "$LABAP_TOPOLOGY_INTERVAL_SECONDS"
    direct_test_ssid_absent_once
}

sta_interface_is_up() {
    sudo_cmd "$IP" -o link show dev "$STA_IF" 2>/dev/null | awk '
        match($0, /<[^>]+>/) {
            flags = substr($0, RSTART + 1, RLENGTH - 2)
            count = split(flags, part, ",")
            for (item = 1; item <= count; ++item)
                if (part[item] == "UP") up = 1
        }
        END { exit !up }
    '
}

# LAR is a PHY capability, not a property of the particular test SSID.  On
# rollback the external LabAP may itself have disappeared; use a passive,
# flushed scan for any Country-IE anchor, while keeping the LabAP topology check as a
# separate admission gate before the original BSS is stopped.
scan_for_lar_country() {
    sta_interface_is_up || return 1
    # Do not send wildcard probe requests while acquiring the Country-IE
    # evidence used for a later local AP transmit permit.
    sudo_cmd "$IW" dev "$STA_IF" scan flush passive >/dev/null 2>&1
}

ap_phy_index() {
    sudo_cmd "$IW" dev "$AP_IF" info 2>/dev/null | awk '
        $1 == "wiphy" { print $2; exit }
    '
}

ap_phy_country() {
    local phy
    phy="$(ap_phy_index)" || return 1
    case "$phy" in ''|*[!0-9]*) return 1;; esac
    sudo_cmd "$IW" reg get | awk -v wanted="phy#$phy" '
        $1 == wanted { found = 1; next }
        found && $1 == "country" {
            country = $2
            sub(/:.*/, "", country)
            print country
            exit
        }
    '
}

ap_channel_allows_ir() {
    local phy
    phy="$(ap_phy_index)" || return 1
    case "$phy" in ''|*[!0-9]*) return 1;; esac
    sudo_cmd "$IW" phy "phy$phy" channels | awk '
        /^[[:space:]]*\* [0-9]+ MHz \[149\]/ {
            in_channel = 1
            if (tolower($0) ~ /no[- ]ir/) denied = 1
            next
        }
        in_channel && /^[[:space:]]*\* / { exit }
        in_channel && tolower($0) ~ /no[- ]ir/ { denied = 1 }
        END { exit !(in_channel && !denied) }
    '
}

ensure_ap_channel_ir() {
    local attempt poll country

    # An AP teardown can leave the old permitted channel snapshot visible for
    # a short period before iwlmvm resets LAR to 00.  Never treat that first
    # snapshot as a transmit permit: acquire a fresh Country-IE observation
    # and require consecutive post-scan regulatory polls before hostapd gets
    # a chance to transmit.
    for attempt in $(seq 1 "$LAR_SCAN_ATTEMPTS"); do
        if ! scan_for_lar_country; then
            sleep "$LAR_SCAN_INTERVAL_SECONDS"
            continue
        fi
        for poll in $(seq 1 "$LAR_STABILITY_POLLS"); do
            sleep "$LAR_SCAN_INTERVAL_SECONDS"
            country="$(ap_phy_country 2>/dev/null || true)"
            if [ -z "$country" ] || [ "$country" = "00" ] ||
                ! ap_channel_allows_ir; then
                break
            fi
        done
        if [ "$poll" = "$LAR_STABILITY_POLLS" ] &&
            [ -n "$country" ] && [ "$country" != "00" ] &&
            ap_channel_allows_ir; then
            return 0
        fi
    done
    return 1
}

write_marker() {
    [ ! -e "$MARKER" ] && [ ! -L "$MARKER" ] || return 1
    printf '%s\n' "$STATE_DIR" >"$MARKER"
    chmod 600 "$MARKER"
}

marker_matches_state() {
    [ -f "$MARKER" ] && [ ! -L "$MARKER" ] &&
        [ "$(cat "$MARKER" 2>/dev/null)" = "$STATE_DIR" ]
}

clear_marker() {
    if marker_matches_state; then
        unlink "$MARKER"
    elif [ -e "$MARKER" ] || [ -L "$MARKER" ]; then
        return 1
    fi
}

watchdog_process_matches() {
    local pid="$1" value index self_count=0 watchdog_count=0 state_count=0
    local -a argv=()
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] || return 1
    kill -0 "$pid" 2>/dev/null || return 1
    [ -r "/proc/$pid/cmdline" ] || return 1
    while IFS= read -r -d '' value; do
        [ -n "$value" ] || return 1
        argv+=("$value")
    done <"/proc/$pid/cmdline"
    [ "${#argv[@]}" -ne 0 ] || return 1
    for ((index = 0; index < ${#argv[@]}; index++)); do
        case "${argv[$index]}" in
            "$SELF") self_count=$((self_count + 1));;
            --watchdog) watchdog_count=$((watchdog_count + 1));;
            --state-dir)
                [ $((index + 1)) -lt "${#argv[@]}" ] || return 1
                [ "${argv[$((index + 1))]}" = "$STATE_DIR" ] || return 1
                state_count=$((state_count + 1))
                ;;
        esac
    done
    [ "$self_count" -eq 1 ] && [ "$watchdog_count" -eq 1 ] &&
        [ "$state_count" -eq 1 ]
}

watchdog_process_is_zombie() {
    local pid state
    pid="$1"
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] || return 1
    state="$(ps -p "$pid" -o stat= 2>/dev/null | tr -d '[:space:]')"
    [[ "$state" == Z* ]]
}

watchdog_process_can_rollback() {
    local pid="$1" state
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] || return 1
    state="$(ps -p "$pid" -o stat= 2>/dev/null | tr -d '[:space:]')"
    case "$state" in ''|Z*|T*|t*) return 1;; esac
    return 0
}

watchdog_owner_is_current() {
    local pid
    pid="$(pid_from_file "$(watchdog_pid_file)")" || return 1
    watchdog_process_matches "$pid"
}

watchdog_owner_is_live() {
    local pid
    pid="$(pid_from_file "$(watchdog_pid_file)")" || return 1
    watchdog_process_matches "$pid" && watchdog_process_can_rollback "$pid"
}

write_watchdog_pid() {
    local pid="$1" path
    path="$(watchdog_pid_file)"
    [ ! -e "$path" ] && [ ! -L "$path" ] || return 1
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] || return 1
    printf '%s\n' "$pid" >"$path"
    chmod 600 "$path"
}

stop_unready_watchdog() {
    local pid="$1"
    case "$pid" in ''|*[!0-9]*) return 0;; esac
    [ "$pid" -gt 1 ] || return 0
    kill "$pid" >/dev/null 2>&1 || true
}

start_watchdog() {
    local fifo="$STATE_DIR/.watchdog-ready-fifo.$$" launcher_pid watchdog_pid ready_line=""
    [ ! -e "$fifo" ] && [ ! -L "$fifo" ] || return 1
    mkfifo -m 600 "$fifo" || return 1
    exec 8<>"$fifo"
    setsid "$SELF" --watchdog --state-dir "$STATE_DIR" \
        --lease-seconds "$LEASE_SECONDS" --ready-fd 8 \
        8>&8 9>&- </dev/null >/dev/null 2>&1 &
    launcher_pid=$!
    if ! IFS= read -r -t 5 -u 8 ready_line; then
        stop_unready_watchdog "$launcher_pid"
        exec 8>&-
        unlink "$fifo" || true
        return 1
    fi
    exec 8>&-
    unlink "$fifo" || true
    case "$ready_line" in
        LABAP_BSS_WATCHDOG_READY:[0-9]*) watchdog_pid="${ready_line#LABAP_BSS_WATCHDOG_READY:}";;
        *) stop_unready_watchdog "$launcher_pid"; return 1;;
    esac
    if ! watchdog_process_matches "$watchdog_pid" ||
        ! write_watchdog_pid "$watchdog_pid" ||
        ! watchdog_owner_is_current; then
        stop_unready_watchdog "$watchdog_pid"
        [ "$launcher_pid" = "$watchdog_pid" ] || stop_unready_watchdog "$launcher_pid"
        return 1
    fi
}

cancel_watchdog() {
    local pid attempt path
    path="$(watchdog_pid_file)"
    [ -e "$path" ] || return 0
    [ ! -L "$path" ] || return 1
    pid="$(cat "$path" | tr -d '[:space:]')"
    # finish_rollback() calls us only after the original BSS has passed every
    # runtime invariant.  A watchdog may already have exited after attempting
    # that same rollback; its stale receipt must not turn a verified recovery
    # into a false failure or leave the marker armed.
    if ! watchdog_process_matches "$pid"; then
        unlink "$path"
        return 0
    fi
    kill "$pid"
    for attempt in $(seq 1 10); do
        # `kill -0` remains true for a dead child until its launcher reaps
        # the zombie.  The watchdog has no more executable path in that
        # state, so waiting for reaping would falsely reject an otherwise
        # verified rollback and leave its receipt/marker behind.
        if ! kill -0 "$pid" 2>/dev/null || watchdog_process_is_zombie "$pid"; then
            unlink "$path"
            return 0
        fi
        sleep 1
    done
    return 1
}

clear_own_watchdog_receipt() {
    local path receipt
    path="$(watchdog_pid_file)"
    [ -e "$path" ] || return 0
    [ ! -L "$path" ] || return 1
    receipt="$(cat "$path" | tr -d '[:space:]')"
    [ "$receipt" = "$$" ] || return 1
    unlink "$path"
}

live_config_matches_state() {
    [ "$(live_config_fingerprint)" = "$(state_value live_config_fingerprint)" ]
}

restore_live() {
    if test_hostapd_process_active; then
        stop_exact_hostapd "$(test_config)" "$(test_pid)" "$(test_log)" || return 1
    fi
    if ! live_hostapd_active; then
        live_config_matches_state || return 1
        start_exact_hostapd "$LIVE_CONFIG" "$LIVE_PID" "$LIVE_LOG" live || return 1
    fi
    live_hostapd_active || return 1
    [ "$(runtime_bssid)" = "$(state_value live_bssid_before)" ] || return 1
    [ "$(host_network_signature)" = "$(state_value network_signature_before)" ]
}

remove_test_artifacts() {
    local path
    for path in "$(test_config)" "$(test_pid)" "$(test_log)"; do
        if [ -L "$path" ]; then
            return 1
        elif [ -e "$path" ]; then
            unlink "$path" || return 1
        fi
    done
}

finish_rollback() {
    restore_live || return 1
    set_state original-restored || return 1
    if [ "$FROM_WATCHDOG" -eq 0 ]; then
        cancel_watchdog || return 1
    fi
    clear_marker || return 1
    remove_test_artifacts || return 1
    printf 'rollback_verified=true\n' >"$STATE_DIR/rollback.status"
    chmod 600 "$STATE_DIR/rollback.status"
}

# Once state=armed exists, a normal shell signal must not leave a stale
# marker merely because the foreground process disappeared between watchdog
# arming and state promotion.  The handler runs in the lock-owning activation
# shell, so it uses the same verified rollback path rather than racing a new
# lock contender.  SIGKILL remains intentionally delegated to the independent
# watchdog after it has been armed.
recover_after_activate_signal() {
    local signal="$1" status="$2"
    trap - HUP INT TERM
    record_activation_phase "interrupted-$signal" || true
    if finish_rollback; then
        printf 'LABAP_BSS_SWITCH_INTERRUPTED signal=%s recovery=original-restored\n' "$signal" >&2
    else
        printf 'LABAP_BSS_SWITCH_INTERRUPTED signal=%s recovery=watchdog-marker-retained\n' "$signal" >&2
    fi
    exit "$status"
}

arm_activate_signal_recovery() {
    trap 'recover_after_activate_signal HUP 129' HUP
    trap 'recover_after_activate_signal INT 130' INT
    trap 'recover_after_activate_signal TERM 143' TERM
}

disarm_activate_signal_recovery() {
    trap - HUP INT TERM
}

recover_after_activate_failure() {
    if finish_rollback; then
        die "temporary test BSS activation failed; original BSS restored"
    fi
    die "temporary test BSS activation failed; watchdog/marker retained for recovery"
}

# The status path is deliberately narrower than rollback: it admits only a
# current v4 LabAP activation with an active (not setup) deadline and keeps
# every raw identifier in shell memory.
# Its sole successful output is produced by do_status below as fixed hashes.
status_state_is_current() {
    local schema state mode network fingerprint state_bssid external_count
    local lease_seconds deadline_phase lease_deadline now remaining

    schema="$(state_value schema)" || return 1
    state="$(state_value state)" || return 1
    mode="$(state_value test_mode)" || return 1
    [ "$schema" = tahoe-labap-bss-switch/v4 ] || return 1
    [ "$state" = labap-active ] || return 1
    [ "$mode" = labap ] || return 1
    network="$(state_value network_signature_before)" || return 1
    fingerprint="$(state_value live_config_fingerprint)" || return 1
    state_bssid="$(state_value live_bssid_before)" || return 1
    external_count="$(state_value external_labap_bss_count)" || return 1
    lease_seconds="$(state_value lease_seconds)" || return 1
    deadline_phase="$(state_value deadline_phase)" || return 1
    lease_deadline="$(state_value lease_not_after_monotonic_seconds)" || return 1
    is_hex64 "$network" && is_hex64 "$fingerprint" || return 1
    canonical_bssid "$state_bssid" >/dev/null || return 1
    case "$external_count" in ''|*[!0-9]*) return 1;; esac
    [ "$external_count" -ge 2 ] || return 1
    is_decimal_in_range "$lease_seconds" 60 300 || return 1
    [ "$deadline_phase" = active ] || return 1
    is_canonical_positive_decimal "$lease_deadline" || return 1
    now="$(monotonic_uptime_seconds)" || return 1
    is_canonical_decimal "$now" || return 1
    [ "$lease_deadline" -gt "$now" ] || return 1
    remaining=$((lease_deadline - now))
    [ "$remaining" -gt 0 ] && [ "$remaining" -le "$lease_seconds" ] || return 1
    marker_matches_state || return 1
    watchdog_owner_is_live || return 1
    STATUS_LEASE_SECONDS="$lease_seconds"
    STATUS_LEASE_REMAINING_SECONDS="$remaining"
}

status_runtime_is_exact() {
    local status runtime_bssid status_bssid state_bssid

    validate_test_config || return 1
    test_hostapd_process_active || return 1
    runtime_ap_is_pinned || return 1
    status="$(hostapd_status_snapshot)" || return 1
    hostapd_status_snapshot_is "$TEST_SSID" "$status" || return 1
    runtime_bssid="$(canonical_bssid "$(runtime_bssid)")" || return 1
    status_bssid="$(canonical_bssid "$(hostapd_status_snapshot_bssid "$status")")" || return 1
    state_bssid="$(canonical_bssid "$(state_value live_bssid_before)")" || return 1
    [ "$runtime_bssid" = "$status_bssid" ] || return 1
    [ "$runtime_bssid" = "$state_bssid" ] || return 1
    STATUS_RUNTIME_BSSID="$runtime_bssid"
}

do_status() {
    local ssid_sha256 bssid_sha256

    require_state_dir
    status_state_is_current || die "active LabAP status is not exact"
    status_runtime_is_exact || die "active LabAP runtime identity is not exact"
    # Re-attest after gathering the runtime snapshot.  The switch lock blocks
    # normal withdrawal/rollback and this second check fails closed if an
    # independently supervised owner changed before the only output site.
    status_state_is_current || die "active LabAP status changed during attestation"
    status_runtime_is_exact || die "active LabAP runtime changed during attestation"
    ssid_sha256="$(opaque_sha256 "$TEST_SSID")" || die "active LabAP SSID digest is invalid"
    bssid_sha256="$(opaque_sha256 "$STATUS_RUNTIME_BSSID")" || die "active LabAP BSSID digest is invalid"
    is_hex64 "$ssid_sha256" && is_hex64 "$bssid_sha256" || die "active LabAP digest shape is invalid"
    # This last check is intentionally after every slow external query and
    # hash calculation, so the advertised remaining lease was current at the
    # only output site rather than at an earlier attestation stage.
    status_state_is_current || die "active LabAP lease changed before status output"
    printf 'LABAP_BSS_STATUS schema=tahoe-labap-bss-status/v1 active=1 target_ssid_sha256=%s target_bssid_sha256=%s lease_seconds=%s lease_remaining_seconds=%s\n' \
        "$ssid_sha256" "$bssid_sha256" "$STATUS_LEASE_SECONDS" "$STATUS_LEASE_REMAINING_SECONDS"
}

do_preflight() {
    local external_count external_band_count
    sudo_cmd true || die "noninteractive sudo is unavailable"
    validate_live_config || die "pinned live hostapd configuration is not exact"
    live_hostapd_active || die "pinned live hostapd process/AP shape is not exact"
    sta_interface_is_up || die "LAR scan interface is not administratively up"
    host_network_signature >/dev/null || die "host network invariants are unreadable"
    stable_external_labap_topology || die "external LabAP passive topology is not stable across two scans"
    external_count="$STABLE_EXTERNAL_BSS_COUNT"
    external_band_count="$STABLE_EXTERNAL_BAND_COUNT"
    printf 'LABAP_BSS_PREFLIGHT=PASS external_bss_count=%s external_band_count=%s\n' \
        "$external_count" "$external_band_count"
}

do_activate() {
    local external_count external_band_count network fingerprint bssid passphrase="" active_state setup_now setup_deadline
    require_state_dir
    [ ! -e "$(state_file)" ] && [ ! -L "$(state_file)" ] || die "state directory is not fresh"
    [ ! -e "$MARKER" ] && [ ! -L "$MARKER" ] || die "another LabAP switch is already active"
    sudo_cmd true || die "noninteractive sudo is unavailable"
    validate_live_config || die "pinned live hostapd configuration is not exact"
    live_hostapd_active || die "pinned live hostapd process/AP shape is not exact"
    sta_interface_is_up || die "LAR scan interface is not administratively up"
    network="$(host_network_signature)" || die "host network invariants are unreadable"
    fingerprint="$(live_config_fingerprint)" || die "live configuration fingerprint is unreadable"
    bssid="$(runtime_bssid)" || die "live BSSID is unreadable"
    [ -n "$bssid" ] || die "live BSSID is empty"
    if [ "$TEST_MODE" = labap ]; then
        stable_external_labap_topology || die "external LabAP passive topology is not stable across two scans"
        external_count="$STABLE_EXTERNAL_BSS_COUNT"
        external_band_count="$STABLE_EXTERNAL_BAND_COUNT"
    else
        [ "$CREDENTIAL_STDIN" -eq 1 ] || die "direct join requires credential stdin"
        [ "$DIRECT_TEST_SSID" != "$TEST_SSID" ] || die "direct test SSID collides with LabAP fixture"
        [ "$DIRECT_TEST_SSID" != "$EXPECTED_LIVE_SSID" ] || die "direct test SSID collides with live BSS"
        stable_direct_test_ssid_absence || die "direct test SSID is visible in passive RF scans"
        external_count=0
        external_band_count=0
    fi

    if [ "$CREDENTIAL_STDIN" -eq 1 ]; then
        IFS= read -r -s passphrase || die "credential stdin ended before one passphrase"
    else
        passphrase="$(generated_passphrase)" || die "could not generate temporary passphrase"
    fi
    valid_wpa_passphrase "$passphrase" || die "credential is not a valid WPA2 passphrase"
    write_test_config "$passphrase" || die "could not stage temporary hostapd configuration"
    passphrase=""
    validate_test_config || die "temporary hostapd configuration failed local validation"

    setup_now="$(monotonic_uptime_seconds)" || die "monotonic setup clock is unavailable"
    is_canonical_decimal "$setup_now" || die "monotonic setup clock is invalid"
    setup_deadline=$((setup_now + SETUP_DEADLINE_SECONDS))
    [ "$setup_deadline" -gt "$setup_now" ] || die "monotonic setup deadline is invalid"
    # Arm an independently bounded setup deadline before publishing the marker.
    # This preserves rollback ownership through the LAR/hostapd handoff without
    # consuming any of the exact on-air lease that begins only at promotion.
    write_state armed "$TEST_MODE" "$network" "$fingerprint" "$bssid" "$external_count" "$LEASE_SECONDS" setup "$setup_deadline" || die "could not write rollback state"
    record_activation_phase state-written || true
    arm_activate_signal_recovery
    mkdir -p "$CONTROL_DIR"
    chmod 700 "$CONTROL_DIR"
    write_marker || die "could not publish rollback marker"
    record_activation_phase marker-published || true
    start_watchdog || { clear_marker || true; die "could not arm rollback watchdog"; }
    record_activation_phase watchdog-ready || true

    if ! live_config_matches_state || ! live_hostapd_active ||
        [ "$(host_network_signature)" != "$network" ] ||
        ! watchdog_owner_is_current || ! setup_deadline_is_current; then
        recover_after_activate_failure
    fi

    record_activation_phase pre-live-stop || true
    if ! stop_exact_hostapd "$LIVE_CONFIG" "$LIVE_PID" "$LIVE_LOG"; then
        recover_after_activate_failure
    fi
    record_activation_phase live-stopped || true
    setup_deadline_is_current || recover_after_activate_failure
    if ! start_exact_hostapd "$(test_config)" "$(test_pid)" "$(test_log)" test; then
        recover_after_activate_failure
    fi
    record_activation_phase test-started || true
    if ! test_hostapd_active ||
        [ "$(host_network_signature)" != "$network" ] ||
        ! watchdog_owner_is_current || ! setup_deadline_is_current; then
        recover_after_activate_failure
    fi
    active_state=labap-active
    [ "$TEST_MODE" = labap ] || active_state=direct-active
    promote_active_state "$active_state" || recover_after_activate_failure
    record_activation_phase promoted || true
    disarm_activate_signal_recovery
    if [ "$TEST_MODE" = direct ]; then
        printf 'DIRECT_JOIN_BSS_SWITCH=ACTIVE collision_free=1\n'
    else
        printf 'LABAP_BSS_SWITCH=ACTIVE external_bss_count=%s external_band_count=%s\n' \
            "$external_count" "$external_band_count"
    fi
}

do_withdraw() {
    require_state_dir
    load_test_mode_from_state || die "state test mode is invalid"
    [ "$TEST_MODE" = labap ] || die "direct join test does not authorize withdrawal"
    marker_matches_state || die "active marker does not authorize this switch"
    [ "$(state_value state)" = labap-active ] || die "state does not authorize a one-shot withdrawal"
    active_lease_is_current labap-active || die "active LabAP lease is expired or malformed"
    test_hostapd_active || die "temporary LabAP hostapd is not exact"
    watchdog_owner_is_current || die "rollback watchdog is not current"
    active_lease_is_current labap-active || die "active LabAP lease changed before withdrawal"
    stop_exact_hostapd "$(test_config)" "$(test_pid)" "$(test_log)" || die "temporary LabAP hostapd did not stop"
    [ ! -e "$(test_pid)" ] && [ ! -L "$(test_pid)" ] || die "temporary LabAP pidfile remains after withdrawal"
    set_state withdrawn || die "could not record one-shot withdrawal"
    printf 'LABAP_BSS_SWITCH=WITHDRAWN\n'
}

do_rollback() {
    require_state_dir
    load_test_mode_from_state || die "state test mode is invalid"
    marker_matches_state || die "active marker does not authorize rollback"
    case "$(state_value state)" in armed|labap-active|direct-active|withdrawn) ;; *) die "state does not authorize rollback";; esac
    if finish_rollback; then
        printf 'LABAP_BSS_SWITCH=ORIGINAL_RESTORED\n'
        return 0
    fi
    die "original BSS restoration could not be verified; marker/watchdog retained"
}

retire_directory_has_only_receipts() {
    local entry
    for entry in "$STATE_DIR"/* "$STATE_DIR"/.[!.]* "$STATE_DIR"/..?*; do
        [ -e "$entry" ] || [ -L "$entry" ] || continue
        case "$entry" in
            "$(state_file)"|"$STATE_DIR/rollback.status"|"$(activation_phase_file)") ;;
            *) return 1;;
        esac
    done
}

retire_regular_mode_600() {
    local path="$1"
    [ -f "$path" ] && [ ! -L "$path" ] || return 1
    [ "$(stat -c %a -- "$path")" = 600 ]
}

retire_state_is_safe() {
    local schema state deadline_phase lease_seconds deadline

    schema="$(state_value schema)" || return 1
    state="$(state_value state)" || return 1
    [ "$state" = original-restored ] || return 1
    case "$schema" in
        tahoe-labap-bss-switch/v4)
            deadline_phase="$(state_value deadline_phase)" || return 1
            lease_seconds="$(state_value lease_seconds)" || return 1
            deadline="$(state_value lease_not_after_monotonic_seconds)" || return 1
            case "$deadline_phase" in setup|active) ;; *) return 1;; esac
            is_decimal_in_range "$lease_seconds" 60 300 || return 1
            is_canonical_positive_decimal "$deadline"
            ;;
        tahoe-labap-bss-switch/v3)
            lease_seconds="$(state_value lease_seconds)" || return 1
            deadline="$(state_value lease_not_after_monotonic_seconds)" || return 1
            is_decimal_in_range "$lease_seconds" 60 300 &&
                is_canonical_positive_decimal "$deadline"
            ;;
        tahoe-labap-bss-switch/v1|tahoe-labap-bss-switch/v2)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

retire_activation_phase_is_safe() {
    local phase
    [ ! -e "$(activation_phase_file)" ] && [ ! -L "$(activation_phase_file)" ] && return 0
    retire_regular_mode_600 "$(activation_phase_file)" || return 1
    phase="$(cat "$(activation_phase_file)" 2>/dev/null)" || return 1
    case "$phase" in
        phase=state-written|phase=marker-published|phase=watchdog-ready|phase=pre-live-stop|\
        phase=live-stopped|phase=test-started|phase=promoted|phase=interrupted-HUP|\
        phase=interrupted-INT|phase=interrupted-TERM) return 0;;
        *) return 1;;
    esac
}

do_retire() {
    local state_bssid

    require_state_dir
    load_test_mode_from_state || die "state test mode is invalid"
    [ "$(state_value state)" = original-restored ] || die "state is not a verified rollback"
    retire_state_is_safe || die "rollback state deadline receipt is invalid"
    [ ! -e "$MARKER" ] && [ ! -L "$MARKER" ] || die "active marker blocks retirement"
    [ ! -e "$(watchdog_pid_file)" ] && [ ! -L "$(watchdog_pid_file)" ] || die "watchdog receipt blocks retirement"
    [ ! -e "$(test_config)" ] && [ ! -L "$(test_config)" ] || die "temporary configuration blocks retirement"
    [ ! -e "$(test_pid)" ] && [ ! -L "$(test_pid)" ] || die "temporary pid receipt blocks retirement"
    [ ! -e "$(test_log)" ] && [ ! -L "$(test_log)" ] || die "temporary log blocks retirement"
    retire_regular_mode_600 "$(state_file)" || die "rollback state receipt is not exact"
    retire_regular_mode_600 "$STATE_DIR/rollback.status" || die "rollback proof is not exact"
    [ "$(cat "$STATE_DIR/rollback.status" 2>/dev/null)" = rollback_verified=true ] || die "rollback proof is invalid"
    retire_activation_phase_is_safe || die "activation phase receipt is invalid"
    retire_directory_has_only_receipts || die "state directory contains unrecognized entries"
    live_config_matches_state || die "live configuration changed after rollback"
    live_hostapd_active || die "live hostapd is not exact after rollback"
    state_bssid="$(canonical_bssid "$(state_value live_bssid_before)")" || die "rollback BSSID receipt is invalid"
    [ "$(canonical_bssid "$(runtime_bssid)")" = "$state_bssid" ] || die "live BSSID changed after rollback"
    [ "$(host_network_signature)" = "$(state_value network_signature_before)" ] || die "host network changed after rollback"
    unlink "$(state_file)" || die "could not remove rollback state"
    unlink "$STATE_DIR/rollback.status" || die "could not remove rollback proof"
    if [ -e "$(activation_phase_file)" ]; then
        unlink "$(activation_phase_file)" || die "could not remove activation phase"
    fi
    rmdir "$STATE_DIR" || die "could not remove retired state directory"
    printf 'LABAP_BSS_SWITCH=RETIRED\n'
}

# v4 separates a bounded setup deadline from the on-air recovery deadline.
# The watchdog accepts only setup timing while state=armed, then reads a newly
# written active deadline after verified promotion.  Thus a permitted slow LAR
# handoff cannot consume the active lease, while a stalled setup still rolls
# back.  v3 remains readable only so pre-existing receipts can be recovered.
watchdog_remaining_seconds() {
    local schema state deadline_phase stored_lease deadline now remaining maximum_seconds

    schema="$(state_value schema 2>/dev/null || true)"
    case "$schema" in
        tahoe-labap-bss-switch/v4)
            state="$(state_value state 2>/dev/null || true)"
            deadline_phase="$(state_value deadline_phase 2>/dev/null || true)"
            stored_lease="$(state_value lease_seconds 2>/dev/null || true)"
            deadline="$(state_value lease_not_after_monotonic_seconds 2>/dev/null || true)"
            if ! is_decimal_in_range "$stored_lease" 60 300 ||
                [ "$stored_lease" != "$LEASE_SECONDS" ] ||
                ! is_canonical_positive_decimal "$deadline"; then
                printf '0\n'
                return 0
            fi
            case "$state:$deadline_phase" in
                armed:setup) maximum_seconds="$SETUP_DEADLINE_SECONDS";;
                labap-active:active|direct-active:active|withdrawn:active) maximum_seconds="$stored_lease";;
                *)
                    printf '0\n'
                    return 0
                    ;;
            esac
            now="$(monotonic_uptime_seconds 2>/dev/null || true)"
            if ! is_canonical_decimal "$now" || [ "$deadline" -le "$now" ]; then
                printf '0\n'
                return 0
            fi
            remaining=$((deadline - now))
            if [ "$remaining" -le 0 ] || [ "$remaining" -gt "$maximum_seconds" ]; then
                printf '0\n'
                return 0
            fi
            printf '%s\n' "$remaining"
            ;;
        tahoe-labap-bss-switch/v3)
            stored_lease="$(state_value lease_seconds 2>/dev/null || true)"
            deadline="$(state_value lease_not_after_monotonic_seconds 2>/dev/null || true)"
            if ! is_decimal_in_range "$stored_lease" 60 300 ||
                [ "$stored_lease" != "$LEASE_SECONDS" ] ||
                ! is_canonical_positive_decimal "$deadline"; then
                printf '0\n'
                return 0
            fi
            now="$(monotonic_uptime_seconds 2>/dev/null || true)"
            if ! is_canonical_decimal "$now" || [ "$deadline" -le "$now" ]; then
                printf '0\n'
                return 0
            fi
            remaining=$((deadline - now))
            if [ "$remaining" -le 0 ] || [ "$remaining" -gt "$stored_lease" ]; then
                printf '0\n'
                return 0
            fi
            printf '%s\n' "$remaining"
            ;;
        tahoe-labap-bss-switch/v1|tahoe-labap-bss-switch/v2)
            is_decimal_in_range "$LEASE_SECONDS" 60 300 || return 1
            printf '%s\n' "$LEASE_SECONDS"
            ;;
        *)
            printf '0\n'
            ;;
    esac
}

do_watchdog() {
    local remaining chunk current_state
    require_state_dir
    marker_matches_state || return 1
    if [ -n "$WATCHDOG_READY_FD" ]; then
        printf 'LABAP_BSS_WATCHDOG_READY:%s\n' "$$" >&8 || return 1
    fi
    while marker_matches_state; do
        current_state="$(state_value state 2>/dev/null || true)"
        [ "$current_state" != original-restored ] || return 0
        remaining="$(watchdog_remaining_seconds)" || return 1
        case "$remaining" in ''|*[!0-9]*) return 1;; esac
        [ "$remaining" -gt 0 ] || break
        chunk=30
        [ "$remaining" -lt "$chunk" ] && chunk="$remaining"
        sleep "$chunk"
        marker_matches_state || return 0
        current_state="$(state_value state 2>/dev/null || true)"
        case "$current_state" in
            armed|labap-active|direct-active|withdrawn) ;;
            *) return 0;;
        esac
    done
    # The foreground activation/rollback takes a non-blocking switch lock.
    # If the lease fires while it is reacquiring LAR, a single rollback try
    # would otherwise exit and silently lose the only recovery owner.  Keep
    # retrying while the marker remains authorized; a successful foreground
    # rollback removes it and makes this loop terminate without mutation.
    while marker_matches_state; do
        if "$SELF" --rollback --state-dir "$STATE_DIR" --from-watchdog; then
            clear_own_watchdog_receipt || return 1
            return 0
        fi
        sleep 2
    done
}

with_lock() {
    [ ! -e "$CONTROL_DIR" ] && [ ! -L "$CONTROL_DIR" ] && mkdir "$CONTROL_DIR" || true
    [ -d "$CONTROL_DIR" ] && [ ! -L "$CONTROL_DIR" ] || die "control directory is unavailable or symlinked"
    chmod 700 "$CONTROL_DIR"
    exec 9>"$LOCK"
    flock -n 9 || die "another LabAP BSS operation is in progress"
    "$@"
}

case "$MODE" in
    preflight) with_lock do_preflight;;
    activate) with_lock do_activate;;
    withdraw) with_lock do_withdraw;;
    rollback) with_lock do_rollback;;
    status) with_lock do_status;;
    retire) with_lock do_retire;;
    watchdog) do_watchdog;;
esac
