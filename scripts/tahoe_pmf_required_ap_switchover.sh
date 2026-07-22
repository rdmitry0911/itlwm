#!/usr/bin/env bash
# One-at-a-time, rollback-bounded hostapd transition for the pinned PMF lab.
#
# It changes only the existing hostapd process bound to the lab AP interface.
# It never configures an address, route, NAT, forwarding, DHCP, or host reboot.
# The caller must use --rollback in an EXIT trap; activate also starts a short
# watchdog lease so an interrupted caller cannot leave required PMF enabled.
set -euo pipefail

SELF="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")"
TEST_MODE="${AIAM_PMF_AP_TEST_MODE:-0}"

LAB_ROOT="${AIAM_PMF_AP_LAB_ROOT:-/home/dima/Projects/ax211-5g-ap}"
AP_IF="${AIAM_PMF_AP_INTERFACE:-wlp0s20f3}"
OPTIONAL_CONFIG="${AIAM_PMF_AP_OPTIONAL_CONFIG:-$LAB_ROOT/hostapd-5g.conf}"
REQUIRED_CONFIG="${AIAM_PMF_AP_REQUIRED_CONFIG:-$LAB_ROOT/hostapd-5g-wpa2-pmf.conf}"
HOSTAPD_BIN="${AIAM_PMF_AP_HOSTAPD:-/usr/sbin/hostapd}"
HOSTAPD_CLI="${AIAM_PMF_AP_HOSTAPD_CLI:-/usr/sbin/hostapd_cli}"
IW_TOOL="${AIAM_PMF_AP_IW:-/usr/sbin/iw}"
IP_TOOL="${AIAM_PMF_AP_IP:-/usr/sbin/ip}"
SYSCTL_TOOL="${AIAM_PMF_AP_SYSCTL:-/usr/sbin/sysctl}"
SUDO_TOOL="${AIAM_PMF_AP_SUDO:-/usr/bin/sudo}"
RUN_DIR="${AIAM_PMF_AP_RUN_DIR:-/tmp/itlwm-lab-ap}"
STATE_PREFIX="${AIAM_PMF_AP_STATE_PREFIX:-/tmp/aiam-pmf-required-switch.}"
CONTROL_DIR="${AIAM_PMF_AP_CONTROL_DIR:-/tmp/aiam-pmf-required-ap-control}"

OPTIONAL_PID="$RUN_DIR/hostapd-5g.pid"
OPTIONAL_LOG="$RUN_DIR/hostapd-5g.log"
REQUIRED_PID="$RUN_DIR/hostapd-5g-pmf-required.pid"
REQUIRED_LOG="$RUN_DIR/hostapd-5g-pmf-required.log"
ACTIVE_MARKER="$CONTROL_DIR/active.state"
LOCK_PATH="$CONTROL_DIR/switchover.lock"

EXPECTED_CHANNEL=153
EXPECTED_WIDTH_MHZ=80
EXPECTED_CENTER1_MHZ=5775

MODE=""
STATE_DIR=""
LEASE_SECONDS=180
FROM_WATCHDOG=0
WATCHDOG_READY_FD=""
CONFIG_VALIDATION_FAILURE=unknown

usage() {
    cat >&2 <<'EOF'
usage: tahoe_pmf_required_ap_switchover.sh --preflight
       tahoe_pmf_required_ap_switchover.sh --activate \
         --state-dir /tmp/aiam-pmf-required-switch.NAME --lease-seconds 60..300
       tahoe_pmf_required_ap_switchover.sh --rekey \
         --state-dir /tmp/aiam-pmf-required-switch.NAME
       tahoe_pmf_required_ap_switchover.sh --rollback \
         --state-dir /tmp/aiam-pmf-required-switch.NAME

Only the pinned local hostapd process for the laboratory AP is accepted.  The
optional-PMF configuration must already be running.  Activation replaces it
with the staged WPA2-PSK required-PMF configuration, starts a bounded rollback
watchdog, and returns no credential or wireless identity.  Rollback restores
the original optional-PMF configuration before reporting success.
EOF
}

die() {
    printf 'PMF_AP_SWITCHOVER_FAIL:%s\n' "$*" >&2
    exit 1
}

is_decimal_in_range() {
    local value="$1" min="$2" max="$3"
    case "$value" in ''|*[!0-9]*) return 1;; esac
    [ "$value" -ge "$min" ] && [ "$value" -le "$max" ]
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --preflight)
            [ -z "$MODE" ] || { usage; exit 2; }
            MODE=preflight
            shift
            ;;
        --activate|--rekey|--rollback)
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
            shift 2
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
case "$MODE" in
    preflight) [ -z "$STATE_DIR" ] && [ -z "$WATCHDOG_READY_FD" ] || { usage; exit 2; };;
    activate|rekey|rollback) [ -n "$STATE_DIR" ] && [ -z "$WATCHDOG_READY_FD" ] || { usage; exit 2; };;
    watchdog)
        [ -n "$STATE_DIR" ] || { usage; exit 2; }
        case "$WATCHDOG_READY_FD" in ''|8) ;; *) usage; exit 2;; esac
        ;;
    *) usage; exit 2;;
esac
is_decimal_in_range "$LEASE_SECONDS" 60 300 || { usage; exit 2; }
[ "$AP_IF" = wlp0s20f3 ] || die "AP interface is not the pinned laboratory interface"

if [ "$TEST_MODE" != 1 ]; then
    for name in AIAM_PMF_AP_LAB_ROOT AIAM_PMF_AP_INTERFACE \
                AIAM_PMF_AP_OPTIONAL_CONFIG AIAM_PMF_AP_REQUIRED_CONFIG \
                AIAM_PMF_AP_HOSTAPD AIAM_PMF_AP_HOSTAPD_CLI AIAM_PMF_AP_IW \
                AIAM_PMF_AP_IP AIAM_PMF_AP_SYSCTL AIAM_PMF_AP_SUDO \
                AIAM_PMF_AP_RUN_DIR AIAM_PMF_AP_STATE_PREFIX \
                AIAM_PMF_AP_CONTROL_DIR; do
        [ -z "${!name:-}" ] || die "custom AP control path is test-only"
    done
fi

sudo_cmd() {
    "$SUDO_TOOL" -n "$@"
}

config_value() {
    local config="$1" key="$2"
    awk -v wanted="$key" '
        index($0, wanted "=") == 1 {
            if (++seen != 1) exit 2
            value = substr($0, length(wanted) + 2)
        }
        END {
            if (seen == 1 && value != "") print value
            else exit 1
        }
    ' "$config"
}

expect_config_value() {
    local config="$1" key="$2" expected="$3" value label
    case "$config" in
        "$OPTIONAL_CONFIG") label=optional;;
        "$REQUIRED_CONFIG") label=required;;
        *) label=unknown;;
    esac
    if ! value="$(config_value "$config" "$key")"; then
        CONFIG_VALIDATION_FAILURE="$label:$key:missing-or-duplicate"
        return 1
    fi
    if [ "$value" != "$expected" ]; then
        CONFIG_VALIDATION_FAILURE="$label:$key:unexpected"
        return 1
    fi
}

validate_config_pair() {
    local optional_ssid optional_passphrase required_ssid required_passphrase
    CONFIG_VALIDATION_FAILURE=unknown
    [ -f "$OPTIONAL_CONFIG" ] && [ ! -L "$OPTIONAL_CONFIG" ] || {
        CONFIG_VALIDATION_FAILURE=optional:unsafe-or-missing
        return 1
    }
    [ -f "$REQUIRED_CONFIG" ] && [ ! -L "$REQUIRED_CONFIG" ] || {
        CONFIG_VALIDATION_FAILURE=required:unsafe-or-missing
        return 1
    }
    for config in "$OPTIONAL_CONFIG" "$REQUIRED_CONFIG"; do
        expect_config_value "$config" interface "$AP_IF" || return 1
        expect_config_value "$config" driver nl80211 || return 1
        expect_config_value "$config" hw_mode a || return 1
        expect_config_value "$config" channel 149 || return 1
        expect_config_value "$config" ieee80211n 1 || return 1
        expect_config_value "$config" ieee80211ac 1 || return 1
        expect_config_value "$config" vht_oper_chwidth 1 || return 1
        expect_config_value "$config" vht_oper_centr_freq_seg0_idx 155 || return 1
        expect_config_value "$config" wpa 2 || return 1
        expect_config_value "$config" rsn_pairwise CCMP || return 1
        expect_config_value "$config" ctrl_interface /run/hostapd || return 1
    done
    expect_config_value "$OPTIONAL_CONFIG" ieee80211w 1 || return 1
    expect_config_value "$OPTIONAL_CONFIG" wpa_key_mgmt 'WPA-PSK WPA-PSK-SHA256 SAE' || return 1
    expect_config_value "$REQUIRED_CONFIG" ieee80211w 2 || return 1
    expect_config_value "$REQUIRED_CONFIG" wpa_key_mgmt WPA-PSK-SHA256 || return 1
    optional_ssid="$(config_value "$OPTIONAL_CONFIG" ssid)" || {
        CONFIG_VALIDATION_FAILURE=optional:ssid:missing-or-duplicate
        return 1
    }
    required_ssid="$(config_value "$REQUIRED_CONFIG" ssid)" || {
        CONFIG_VALIDATION_FAILURE=required:ssid:missing-or-duplicate
        return 1
    }
    optional_passphrase="$(config_value "$OPTIONAL_CONFIG" wpa_passphrase)" || {
        CONFIG_VALIDATION_FAILURE=optional:wpa-passphrase:missing-or-duplicate
        return 1
    }
    required_passphrase="$(config_value "$REQUIRED_CONFIG" wpa_passphrase)" || {
        CONFIG_VALIDATION_FAILURE=required:wpa-passphrase:missing-or-duplicate
        return 1
    }
    [ "$optional_ssid" = "$required_ssid" ] || {
        CONFIG_VALIDATION_FAILURE=ssid-pair-mismatch
        return 1
    }
    [ "$optional_passphrase" = "$required_passphrase" ] || {
        CONFIG_VALIDATION_FAILURE=wpa-passphrase-pair-mismatch
        return 1
    }
}

config_pair_signature() {
    [ -f "$OPTIONAL_CONFIG" ] && [ ! -L "$OPTIONAL_CONFIG" ] || return 1
    [ -f "$REQUIRED_CONFIG" ] && [ ! -L "$REQUIRED_CONFIG" ] || return 1
    {
        sha256sum <"$OPTIONAL_CONFIG"
        sha256sum <"$REQUIRED_CONFIG"
    } | sha256sum | awk 'NF == 2 && $1 ~ /^[0-9a-f]{64}$/ { print $1; exit }'
}

runtime_ap_is_pinned() {
    sudo_cmd "$IW_TOOL" dev "$AP_IF" info 2>/dev/null | awk \
        -v iface="$AP_IF" -v channel="$EXPECTED_CHANNEL" \
        -v width="$EXPECTED_WIDTH_MHZ" -v center="$EXPECTED_CENTER1_MHZ" '
        $1 == "Interface" && $2 == iface { saw_interface = 1 }
        $1 == "type" && $2 == "AP" { saw_type = 1 }
        $1 == "channel" && $2 == channel &&
            $0 ~ ("width: " width " MHz") &&
            $0 ~ ("center1: " center " MHz") { saw_channel = 1 }
        END { exit !(saw_interface && saw_type && saw_channel) }
    '
}

host_network_signature() {
    {
        sudo_cmd "$IP_TOOL" -4 route show default
        sudo_cmd "$IP_TOOL" -4 -o addr show dev "$AP_IF"
        sudo_cmd "$SYSCTL_TOOL" -n net.ipv4.ip_forward
    } | sha256sum | awk 'NF == 2 && $1 ~ /^[0-9a-f]{64}$/ { print $1; exit }'
}

pid_from_file() {
    local pidfile="$1" pid
    [ -r "$pidfile" ] && [ ! -L "$pidfile" ] || return 1
    pid="$(sudo_cmd /bin/cat "$pidfile" 2>/dev/null | tr -d '[:space:]')" || return 1
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] || return 1
    printf '%s\n' "$pid"
}

process_args() {
    local pid="$1"
    sudo_cmd /bin/ps -p "$pid" -o args= 2>/dev/null
}

hostapd_matches() {
    local pid="$1" config="$2" pidfile="$3" args
    sudo_cmd /bin/kill -0 "$pid" 2>/dev/null || return 1
    args="$(process_args "$pid")" || return 1
    [[ " $args " == *" $HOSTAPD_BIN "* ]] || return 1
    [[ " $args " == *" -P $pidfile "* ]] || return 1
    [[ " $args " == *" -f "* ]] || return 1
    [[ " $args " == *" $config "* ]]
}

configured_hostapd_active() {
    local config="$1" pidfile="$2" pid
    pid="$(pid_from_file "$pidfile")" || return 1
    hostapd_matches "$pid" "$config" "$pidfile"
}

optional_hostapd_exact_and_pinned() {
    configured_hostapd_active "$OPTIONAL_CONFIG" "$OPTIONAL_PID" &&
        runtime_ap_is_pinned
}

wait_hostapd_active() {
    local config="$1" pidfile="$2" attempt
    for attempt in $(seq 1 20); do
        if configured_hostapd_active "$config" "$pidfile" &&
            runtime_ap_is_pinned; then
            return 0
        fi
        sleep 1
    done
    return 1
}

stop_configured_hostapd() {
    local config="$1" pidfile="$2" pid attempt
    pid="$(pid_from_file "$pidfile")" || return 1
    hostapd_matches "$pid" "$config" "$pidfile" || return 1
    sudo_cmd /bin/kill -TERM "$pid"
    for attempt in $(seq 1 20); do
        if ! hostapd_matches "$pid" "$config" "$pidfile"; then
            [ ! -e "$pidfile" ] && [ ! -L "$pidfile" ] && return 0
        fi
        sleep 1
    done
    return 1
}

start_configured_hostapd() {
    local config="$1" pidfile="$2" logfile="$3"
    [ ! -e "$pidfile" ] && [ ! -L "$pidfile" ] || return 1
    # The helper owns a flock on fd 9.  A daemon must never inherit it, or the
    # subsequent rekey/rollback invocation would deadlock behind the AP.
    sudo_cmd "$HOSTAPD_BIN" -B -P "$pidfile" -f "$logfile" "$config" 9>&-
    wait_hostapd_active "$config" "$pidfile"
}

require_state_dir() {
    local resolved owner mode
    case "$STATE_DIR" in "$STATE_PREFIX"*) ;; *) die "state directory is outside the restricted temporary prefix";; esac
    [ -d "$STATE_DIR" ] && [ ! -L "$STATE_DIR" ] || die "state directory is missing or symlinked"
    resolved="$(cd -P -- "$STATE_DIR" && pwd)"
    [ "$resolved" = "$STATE_DIR" ] || die "state directory must be canonical"
    owner="$(/usr/bin/stat -c %u -- "$STATE_DIR" 2>/dev/null)" ||
        die "state directory owner is unreadable"
    [ "$owner" = "$(/usr/bin/id -u)" ] ||
        die "state directory is not owned by the invoking user"
    mode="$(/usr/bin/stat -c %a -- "$STATE_DIR" 2>/dev/null)" ||
        die "state directory permissions are unreadable"
    [ "$mode" = 700 ] || die "state directory permissions are not restricted"
}

state_file() {
    printf '%s/state.txt\n' "$STATE_DIR"
}

rekey_request_is_fresh() {
    [ ! -e "$STATE_DIR/rekey.requested" ] &&
        [ ! -L "$STATE_DIR/rekey.requested" ] &&
        [ ! -e "$STATE_DIR/rekey.status" ] &&
        [ ! -L "$STATE_DIR/rekey.status" ]
}

rollback_receipt_is_fresh() {
    local path="$STATE_DIR/rollback.status"
    [ ! -e "$path" ] && [ ! -L "$path" ]
}

record_rekey_request() {
    local path="$STATE_DIR/rekey.requested"
    [ ! -e "$path" ] && [ ! -L "$path" ] || return 1
    umask 077
    printf 'rekey_attempted=true\n' >"$path" || return 1
    chmod 600 "$path"
}

state_value() {
    local key="$1"
    [ -f "$(state_file)" ] && [ ! -L "$(state_file)" ] || return 1
    awk -F= -v wanted="$key" '
        $1 == wanted { if (++seen == 1) value = $2; else exit 2 }
        END { if (seen == 1 && value != "") print value; else exit 1 }
    ' "$(state_file)"
}

config_pair_matches_state() {
    local before_signature current_signature
    before_signature="$(state_value config_pair_signature_before)" || return 1
    current_signature="$(config_pair_signature)" || return 1
    [ "$current_signature" = "$before_signature" ]
}

write_state() {
    local network_signature="$1" config_signature="$2"
    umask 077
    {
        printf 'schema=itlwm-pmf-required-ap-switchover/v1\n'
        # The watchdog is armed before the optional AP can be stopped.  This
        # state can restore optional PMF but cannot authorize a rekey until
        # the required process is explicitly promoted below.
        printf 'state=rollback-armed\n'
        printf 'host_network_signature_before=%s\n' "$network_signature"
        printf 'config_pair_signature_before=%s\n' "$config_signature"
        printf 'rollback_verified=false\n'
    } >"$(state_file)"
    chmod 600 "$(state_file)"
}

mark_required_active() {
    local network_signature config_signature tmp
    [ "$(state_value state)" = rollback-armed ] || return 1
    network_signature="$(state_value host_network_signature_before)" || return 1
    config_signature="$(state_value config_pair_signature_before)" || return 1
    tmp="$STATE_DIR/.state-required.$$"
    [ ! -e "$tmp" ] && [ ! -L "$tmp" ] || return 1
    umask 077
    {
        printf 'schema=itlwm-pmf-required-ap-switchover/v1\n'
        printf 'state=required\n'
        printf 'host_network_signature_before=%s\n' "$network_signature"
        printf 'config_pair_signature_before=%s\n' "$config_signature"
        printf 'rollback_verified=false\n'
    } >"$tmp" || return 1
    chmod 600 "$tmp" || return 1
    /bin/mv -f -- "$tmp" "$(state_file)"
}

marker_matches_state() {
    [ -f "$ACTIVE_MARKER" ] && [ ! -L "$ACTIVE_MARKER" ] || return 1
    [ "$(/bin/cat "$ACTIVE_MARKER" 2>/dev/null)" = "$STATE_DIR" ]
}

write_marker() {
    [ ! -e "$ACTIVE_MARKER" ] && [ ! -L "$ACTIVE_MARKER" ] || return 1
    umask 077
    printf '%s\n' "$STATE_DIR" >"$ACTIVE_MARKER"
    chmod 600 "$ACTIVE_MARKER"
}

clear_marker() {
    if marker_matches_state; then
        /usr/bin/unlink "$ACTIVE_MARKER"
    elif [ -e "$ACTIVE_MARKER" ] || [ -L "$ACTIVE_MARKER" ]; then
        return 1
    fi
}

watchdog_pid_file() {
    printf '%s/watchdog.pid\n' "$STATE_DIR"
}

watchdog_process_matches() {
    local pid="$1" args
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] || return 1
    /bin/kill -0 "$pid" 2>/dev/null || return 1
    args="$(/bin/ps -p "$pid" -o args= 2>/dev/null || true)"
    [[ " $args " == *" $SELF "* &&
       " $args " == *" --watchdog "* &&
       "$args" == *"--state-dir $STATE_DIR"* ]]
}

watchdog_owner_is_current() {
    local pid
    pid="$(pid_from_file "$(watchdog_pid_file)")" || return 1
    watchdog_process_matches "$pid"
}

stop_unready_watchdog() {
    local pid="$1"
    case "$pid" in ''|*[!0-9]*) return 0;; esac
    [ "$pid" -gt 1 ] || return 0
    /bin/kill "$pid" >/dev/null 2>&1 || true
}

write_watchdog_pid() {
    local pid="$1" path
    path="$(watchdog_pid_file)"
    [ ! -e "$path" ] && [ ! -L "$path" ] || return 1
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] || return 1
    umask 077
    printf '%s\n' "$pid" >"$path" || return 1
    chmod 600 "$path"
}

start_watchdog() {
    local ready_fifo="$STATE_DIR/.watchdog-ready-fifo.$$"
    local launcher_pid watchdog_pid ready_line=""

    [ ! -e "$ready_fifo" ] && [ ! -L "$ready_fifo" ] || return 1
    umask 077
    /usr/bin/mkfifo -m 600 "$ready_fifo" || return 1

    # Open both directions before spawning so neither side can block at FIFO
    # open.  The one-shot read below is a readiness handshake, not a retry or
    # a timer-based assertion that a background PID probably execed.
    exec 8<>"$ready_fifo"
    setsid "$SELF" --watchdog --state-dir "$STATE_DIR" \
        --lease-seconds "$LEASE_SECONDS" --ready-fd 8 \
        8>&8 9>&- </dev/null >/dev/null 2>&1 &
    launcher_pid=$!
    case "$launcher_pid" in ''|*[!0-9]*)
        exec 8>&-
        /usr/bin/unlink "$ready_fifo" || true
        return 1
        ;;
    esac

    if ! IFS= read -r -t 5 -u 8 ready_line; then
        stop_unready_watchdog "$launcher_pid"
        exec 8>&-
        /usr/bin/unlink "$ready_fifo" || true
        return 1
    fi
    exec 8>&-
    /usr/bin/unlink "$ready_fifo" || true

    case "$ready_line" in
        PMF_AP_WATCHDOG_READY:[0-9]*) watchdog_pid="${ready_line#PMF_AP_WATCHDOG_READY:}";;
        *)
            stop_unready_watchdog "$launcher_pid"
            return 1
            ;;
    esac
    if ! watchdog_process_matches "$watchdog_pid" ||
        ! write_watchdog_pid "$watchdog_pid" ||
        ! watchdog_owner_is_current; then
        stop_unready_watchdog "$watchdog_pid"
        if [ "$launcher_pid" != "$watchdog_pid" ]; then
            stop_unready_watchdog "$launcher_pid"
        fi
        return 1
    fi
}

cancel_watchdog() {
    local pid
    [ -r "$(watchdog_pid_file)" ] || return 0
    pid="$(/bin/cat "$(watchdog_pid_file)" | tr -d '[:space:]')"
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    if /bin/kill -0 "$pid" 2>/dev/null; then
        watchdog_process_matches "$pid" || return 1
        /bin/kill "$pid"
    fi
}

restore_optional_after_activation_failure() {
    if configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID"; then
        stop_configured_hostapd "$REQUIRED_CONFIG" "$REQUIRED_PID" || return 1
    fi
    if ! configured_hostapd_active "$OPTIONAL_CONFIG" "$OPTIONAL_PID"; then
        config_pair_matches_state || return 1
        start_configured_hostapd "$OPTIONAL_CONFIG" "$OPTIONAL_PID" "$OPTIONAL_LOG" || return 1
    fi
    # A start helper has only observed one instant.  Recovery may release its
    # marker-bound owner only while the exact optional process is still live
    # and the AP shape remains pinned at this final restoration edge.
    optional_hostapd_exact_and_pinned
}

finish_armed_rollback() {
    # Preserve the watchdog and marker if an immediate restore or watchdog
    # cancellation cannot be proven.  A stale marker is preferable to a
    # required-PMF AP without a bounded restoration owner.
    restore_optional_after_activation_failure || return 1
    optional_hostapd_exact_and_pinned || return 1
    cancel_watchdog || return 1
    clear_marker
}

finish_post_transition_rollback() {
    local before_signature after_signature
    before_signature="$(state_value host_network_signature_before)" || return 1
    restore_optional_after_activation_failure || return 1
    after_signature="$(host_network_signature)" || return 1
    [ "$after_signature" = "$before_signature" ] || return 1
    config_pair_matches_state || return 1
    optional_hostapd_exact_and_pinned || return 1
    after_signature="$(host_network_signature)" || return 1
    [ "$after_signature" = "$before_signature" ] || return 1
    config_pair_matches_state || return 1
    cancel_watchdog || return 1
    clear_marker
}

do_preflight() {
    validate_config_pair ||
        die "optional/required PMF configurations failed validation ($CONFIG_VALIDATION_FAILURE)"
    configured_hostapd_active "$OPTIONAL_CONFIG" "$OPTIONAL_PID" ||
        die "the current optional-PMF hostapd process is not exact"
    runtime_ap_is_pinned || die "the lab AP is not at the pinned channel/width"
    host_network_signature >/dev/null || die "host network invariants are unreadable"
    printf 'PMF_AP_PREFLIGHT=PASS\n'
}

do_activate() {
    local network_signature current_signature config_signature current_config_signature
    local pre_stop_failure="" post_start_failure=""
    require_state_dir
    [ ! -e "$(state_file)" ] && [ ! -L "$(state_file)" ] ||
        die "state directory is not fresh"
    [ ! -e "$ACTIVE_MARKER" ] && [ ! -L "$ACTIVE_MARKER" ] ||
        die "another PMF-required AP switchover is already active"
    config_signature="$(config_pair_signature)" ||
        die "optional/required PMF configuration pair is unreadable"
    validate_config_pair ||
        die "optional/required PMF configurations failed validation ($CONFIG_VALIDATION_FAILURE)"
    current_config_signature="$(config_pair_signature)" ||
        die "optional/required PMF configuration pair is unreadable after validation"
    [ "$current_config_signature" = "$config_signature" ] ||
        die "optional/required PMF configurations changed during activation admission"
    configured_hostapd_active "$OPTIONAL_CONFIG" "$OPTIONAL_PID" ||
        die "the current optional-PMF hostapd process is not exact"
    runtime_ap_is_pinned || die "the lab AP is not at the pinned channel/width"
    network_signature="$(host_network_signature)" || die "host network invariants are unreadable"

    # Establish the restoration owner before any hostapd process can change.
    # If this process dies between stop/start, the separate session restores
    # optional PMF from the restricted state directory.
    write_state "$network_signature" "$config_signature" ||
        die "required-PMF rollback state setup failed"
    write_marker || die "required-PMF active-state marker setup failed"
    if ! start_watchdog; then
        clear_marker || true
        die "required-PMF rollback watchdog setup failed"
    fi

    # The original signature is a transaction admission predicate, not merely
    # an eventual rollback assertion.  Re-read it after the independent
    # watchdog is ready and immediately before the first hostapd mutation.
    # A drift in that bounded interval must retain optional PMF rather than
    # briefly enter required PMF and discover the mismatch only at rollback.
    if ! current_signature="$(host_network_signature)"; then
        pre_stop_failure="host network invariants are unreadable before optional-PMF stop"
    elif [ "$current_signature" != "$network_signature" ]; then
        pre_stop_failure="host network invariants changed before optional-PMF stop"
    elif ! current_config_signature="$(config_pair_signature)"; then
        pre_stop_failure="optional/required PMF configurations are unreadable before optional-PMF stop"
    elif [ "$current_config_signature" != "$config_signature" ]; then
        pre_stop_failure="optional/required PMF configurations changed before optional-PMF stop"
    elif ! watchdog_owner_is_current; then
        pre_stop_failure="rollback watchdog is not exact before optional-PMF stop"
    fi
    if [ -n "$pre_stop_failure" ]; then
        if finish_armed_rollback; then
            die "$pre_stop_failure; optional-PMF state retained"
        fi
        die "$pre_stop_failure; rollback watchdog remains armed"
    fi

    if ! stop_configured_hostapd "$OPTIONAL_CONFIG" "$OPTIONAL_PID" ||
        ! start_configured_hostapd "$REQUIRED_CONFIG" "$REQUIRED_PID" "$REQUIRED_LOG"; then
        if finish_post_transition_rollback; then
            die "required-PMF hostapd activation failed; optional rollback verified"
        fi
        die "required-PMF hostapd activation failed; rollback watchdog remains armed"
    fi
    # wait_hostapd_active() establishes only the start helper's observation.
    # Re-attest the exact required process, AP shape, and staged configuration
    # at the state-promotion edge so a changed input cannot be published.
    if ! configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID" ||
        ! runtime_ap_is_pinned; then
        if finish_post_transition_rollback; then
            die "required-PMF hostapd post-start attestation failed; optional rollback verified"
        fi
        die "required-PMF hostapd post-start attestation failed; rollback watchdog remains armed"
    fi
    # Required-hostapd startup is itself part of the bounded host-network
    # transaction.  Do not publish required state if that process transition
    # changed the route/address/forwarding baseline after the pre-stop fence.
    if ! current_signature="$(host_network_signature)"; then
        post_start_failure="required-PMF host-network invariants are unreadable before state promotion"
    elif [ "$current_signature" != "$network_signature" ]; then
        post_start_failure="required-PMF host-network invariants changed before state promotion"
    fi
    if [ -n "$post_start_failure" ]; then
        if finish_post_transition_rollback; then
            die "$post_start_failure; optional rollback verified"
        fi
        die "$post_start_failure; rollback watchdog remains armed"
    fi
    if ! current_config_signature="$(config_pair_signature)"; then
        if finish_post_transition_rollback; then
            die "required-PMF configuration is unreadable before state promotion; optional rollback verified"
        fi
        die "required-PMF configuration is unreadable before state promotion; rollback watchdog remains armed"
    fi
    if [ "$current_config_signature" != "$config_signature" ]; then
        if finish_post_transition_rollback; then
            die "required-PMF configuration changed before state promotion; optional rollback verified"
        fi
        die "required-PMF configuration changed before state promotion; rollback watchdog remains armed"
    fi
    # The independent restoration owner must survive all post-start admission
    # work and still be exact at the required-state publication edge.
    if ! watchdog_owner_is_current; then
        if finish_post_transition_rollback; then
            die "rollback watchdog is not exact before required-PMF state promotion; optional rollback verified"
        fi
        die "rollback watchdog is not exact before required-PMF state promotion; rollback watchdog remains armed"
    fi
    # The earlier post-start observation predates network/configuration and
    # rollback-owner work.  Re-attest the required process/AP at the actual
    # state-commit edge so a later disappearance cannot publish success.
    if ! configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID" ||
        ! runtime_ap_is_pinned; then
        if finish_post_transition_rollback; then
            die "required-PMF hostapd is not exact before final state promotion; optional rollback verified"
        fi
        die "required-PMF hostapd is not exact before final state promotion; rollback watchdog remains armed"
    fi
    if ! mark_required_active; then
        if finish_post_transition_rollback; then
            die "required-PMF state promotion failed; optional rollback verified"
        fi
        die "required-PMF state promotion failed; rollback watchdog remains armed"
    fi
    printf 'PMF_AP_SWITCHOVER=REQUIRED_ACTIVE\n'
}

do_rekey() {
    local before_signature current_signature
    require_state_dir
    marker_matches_state || die "PMF-required state ownership is not current"
    [ "$(state_value state)" = required ] || die "state does not authorize a group rekey"
    rekey_request_is_fresh ||
        die "bounded group-rekey request was already recorded for this PMF-required transaction"
    before_signature="$(state_value host_network_signature_before)" ||
        die "state lacks the host network baseline"
    config_pair_matches_state ||
        die "staged PMF configuration pair changed before bounded group-rekey"
    configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID" ||
        die "required-PMF hostapd process is not exact"
    runtime_ap_is_pinned || die "the lab AP left the pinned channel/width"
    current_signature="$(host_network_signature)" ||
        die "host network invariants are unreadable before bounded group-rekey"
    [ "$current_signature" = "$before_signature" ] ||
        die "host network invariants changed before bounded group-rekey"
    # The initial process observation predates the AP-shape and network
    # admission probes.  Re-attest at the actual raw-control edge so a dead
    # or replaced daemon cannot receive a categorical rekey witness.
    configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID" ||
        die "required-PMF hostapd process is not exact before bounded group-rekey"
    # The raw control side effect also requires the independent restoration
    # owner to remain current at this command edge.
    watchdog_owner_is_current ||
        die "rollback watchdog is not exact before bounded group-rekey"
    # Record the sole allowed raw side effect before it is sent.  A later
    # acknowledgement or postcondition failure is inconclusive, not authority
    # to issue a second group-rekey request for the same transaction.
    record_rekey_request ||
        die "could not record bounded group-rekey request"
    # Use hostapd's documented raw control transport for its canonical
    # REKEY_GTK command.  A lower-case CLI alias is not consistently exposed
    # by packaged hostapd_cli builds; the daemon command drives the standard
    # group state machine that rotates both GTK and PMF IGTK slots.
    if ! sudo_cmd "$HOSTAPD_CLI" -p /run/hostapd -i "$AP_IF" raw REKEY_GTK \
        >"$STATE_DIR/rekey.stdout" 2>"$STATE_DIR/rekey.stderr"; then
        die "bounded hostapd group-rekey request failed"
    fi
    grep -Fxq OK "$STATE_DIR/rekey.stdout" ||
        die "hostapd did not acknowledge the bounded group-rekey request"
    configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID" ||
        die "required-PMF hostapd process is not exact after bounded group-rekey"
    runtime_ap_is_pinned || die "the lab AP left the pinned channel/width after rekey"
    current_signature="$(host_network_signature)" ||
        die "host network invariants are unreadable after bounded group-rekey"
    [ "$current_signature" = "$before_signature" ] ||
        die "host network invariants changed during bounded group-rekey"
    config_pair_matches_state ||
        die "staged PMF configuration pair changed before rekey success publication"
    # The post-ack process/AP observation predates the final network read.
    # Re-attest at success publication so a later disappearance is not sealed
    # as a categorical bounded-rekey result.
    configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID" ||
        die "required-PMF hostapd process is not exact before rekey success publication"
    runtime_ap_is_pinned ||
        die "the lab AP left the pinned channel/width before rekey success publication"
    watchdog_owner_is_current ||
        die "rollback watchdog is not exact before rekey success publication"
    printf 'rekey_requested=true\n' >"$STATE_DIR/rekey.status"
    chmod 600 "$STATE_DIR/rekey.status"
    printf 'PMF_AP_REKEY=REQUESTED\n'
}

do_rollback() {
    local before_signature after_signature
    require_state_dir
    rollback_receipt_is_fresh ||
        die "rollback completion receipt target is not fresh"
    marker_matches_state || die "PMF-required state ownership is not current"
    case "$(state_value state)" in
        rollback-armed|required) ;;
        *) die "state does not authorize an optional-PMF restoration";;
    esac
    before_signature="$(state_value host_network_signature_before)" ||
        die "state lacks the host network baseline"
    if configured_hostapd_active "$REQUIRED_CONFIG" "$REQUIRED_PID"; then
        stop_configured_hostapd "$REQUIRED_CONFIG" "$REQUIRED_PID" ||
            die "required-PMF hostapd did not stop for rollback"
    fi
    if ! configured_hostapd_active "$OPTIONAL_CONFIG" "$OPTIONAL_PID"; then
        config_pair_matches_state ||
            die "staged PMF configuration pair changed before optional-PMF restart"
        start_configured_hostapd "$OPTIONAL_CONFIG" "$OPTIONAL_PID" "$OPTIONAL_LOG" ||
            die "optional-PMF hostapd did not restart during rollback"
    fi
    runtime_ap_is_pinned || die "optional-PMF AP did not regain the pinned channel/width"
    after_signature="$(host_network_signature)" || die "host network invariants are unreadable after rollback"
    [ "$after_signature" = "$before_signature" ] ||
        die "host IP/default-route/forwarding invariant changed during AP switchover"
    optional_hostapd_exact_and_pinned ||
        die "optional-PMF hostapd process or AP shape is not exact before rollback verification"
    config_pair_matches_state ||
        die "staged PMF configuration pair changed before rollback verification"
    after_signature="$(host_network_signature)" ||
        die "host network invariants are unreadable before rollback verification"
    [ "$after_signature" = "$before_signature" ] ||
        die "host network invariants changed before rollback verification"
    config_pair_matches_state ||
        die "staged PMF configuration pair changed before rollback receipt"
    optional_hostapd_exact_and_pinned ||
        die "optional-PMF hostapd process or AP shape is not exact before rollback receipt"
    if [ "$FROM_WATCHDOG" -eq 0 ]; then
        cancel_watchdog || die "rollback could not safely cancel its watchdog"
    fi
    clear_marker || die "rollback could not clear the active state marker"
    # This receipt is consumed by the runtime cleanup as a completed rollback
    # witness.  Commit it only after every marker/watchdog ownership release
    # has succeeded; an earlier write could convert a failed teardown into a
    # false categorical recovery result.
    printf 'rollback_verified=true\n' >"$STATE_DIR/rollback.status"
    chmod 600 "$STATE_DIR/rollback.status"
    printf 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED\n'
}

do_watchdog() {
    local attempt
    require_state_dir
    marker_matches_state || return 1
    case "$(state_value state)" in
        rollback-armed|required) ;;
        *) return 1;;
    esac
    # The fixture can prove that a watchdog which exits before this exact
    # owner acknowledgement does not authorize the first AP transition.
    if [ "$TEST_MODE" = 1 ] &&
        [ "${AIAM_PMF_AP_TEST_WATCHDOG_EXIT_BEFORE_READY:-0}" = 1 ]; then
        return 1
    fi
    if [ -n "$WATCHDOG_READY_FD" ]; then
        # `--ready-fd` is accepted only as fd 8 above.  It is inherited from
        # start_watchdog's private FIFO and identifies this actual process,
        # after state-dir and marker ownership have both been checked.
        printf 'PMF_AP_WATCHDOG_READY:%s\n' "$$" >&8 || return 1
    fi
    sleep "$LEASE_SECONDS"
    for attempt in $(seq 1 3); do
        if "$SELF" --rollback --state-dir "$STATE_DIR" --from-watchdog; then
            return 0
        fi
        sleep 5
    done
    return 1
}

with_lock() {
    [ ! -e "$CONTROL_DIR" ] && [ ! -L "$CONTROL_DIR" ] && /bin/mkdir "$CONTROL_DIR"
    [ -d "$CONTROL_DIR" ] && [ ! -L "$CONTROL_DIR" ] ||
        die "AP control directory is unavailable or symlinked"
    chmod 700 "$CONTROL_DIR"
    exec 9>"$LOCK_PATH"
    flock -n 9 || die "another PMF-required AP switchover operation is in progress"
    "$@"
}

case "$MODE" in
    preflight) with_lock do_preflight;;
    activate) with_lock do_activate;;
    rekey) with_lock do_rekey;;
    rollback) with_lock do_rollback;;
    watchdog) do_watchdog;;
esac
