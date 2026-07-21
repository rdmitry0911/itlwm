#!/usr/bin/env bash
# Strict lab radio gate, with a four-cycle baseline by default.  It controls
# only the pinned QEMU guest's public Wi-Fi radio power. It makes no explicit
# state-mutating DHCP, address, or route command; its explicit `ipconfig
# getpacket` query is read-only. It captures and checks persistent network
# invariants after every completed cycle. A caller may request one to four
# cycles only for a bounded diagnostic trace window; anything other than four
# is explicitly not reported as the A2DF four-cycle baseline. It deliberately
# does not claim that radio power transitions cannot trigger transient
# OS-managed network work.
#
# The runner is limited to the QEMU lab guest and 10.77.0.1.  It uses public
# radio power controls, host-side station observations, and read-only network
# state only.  It has no raw scan path.  Secrets and arbitrary join helpers are
# never accepted or executed.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
AP_IF="${AP_IF:-wlp0s20f3}"
OUT_DIR="${OUT_DIR:?set OUT_DIR to a fresh evidence directory}"
GUEST_IF="${GUEST_IF:-en1}"
MANAGEMENT_IF="${MANAGEMENT_IF:-en0}"
LAB_SSID="${LAB_SSID:-AIAMlab6235}"
LAB_GATEWAY="${LAB_GATEWAY:-10.77.0.1}"
LAB_IPV4_PREFIX="${LAB_IPV4_PREFIX:-10.77.0.}"
ASSOC_TIMEOUT_ATTEMPTS="${ASSOC_TIMEOUT_ATTEMPTS:-60}"
OFF_STATE_TIMEOUT_ATTEMPTS="${OFF_STATE_TIMEOUT_ATTEMPTS:-30}"
CYCLE_COUNT="${AIAM_A2DF_CYCLE_COUNT:-4}"
EXPECTED_GUEST_HOSTKEY_SHA256="SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY"
EXPECTED_GUEST_HOSTKEY_LINE="[127.0.0.1]:3322 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFPrOLzo9N+8YgP4rFTWH4scBkBT8EYGNVy87QWgvdT2"
EXPECTED_GUEST_BUILD="25C56"
EXPECTED_GUEST_MODEL="MacPro7,1"
EXPECTED_MANAGEMENT_MAC="52:54:00:c9:18:28"
EXPECTED_AP_IF="wlp0s20f3"
EXPECTED_AP_MAC="80:e4:ba:20:ef:f9"
EXPECTED_AP_CHANNEL="153"
EXPECTED_AP_WIDTH_MHZ="80"
GUEST=()
KNOWN_HOSTS=""
ACTIVE_CLIENT_MAC=""
RADIO_OFF=0
DHCP_OBSERVATION_INCONCLUSIVE=0

die() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

valid_name() {
    [[ "$1" =~ ^[A-Za-z0-9_.-]+$ ]]
}

valid_mac() {
    [[ "$1" =~ ^([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}$ ]]
}

valid_name "$AP_IF" || die "AP_IF contains unsupported characters"
case "$OUT_DIR" in '') die "OUT_DIR must not be empty";; esac
case "$ASSOC_TIMEOUT_ATTEMPTS" in ''|*[!0-9]*) die "ASSOC_TIMEOUT_ATTEMPTS must be numeric";; esac
case "$OFF_STATE_TIMEOUT_ATTEMPTS" in ''|*[!0-9]*) die "OFF_STATE_TIMEOUT_ATTEMPTS must be numeric";; esac
case "$CYCLE_COUNT" in ''|*[!0-9]*) die "AIAM_A2DF_CYCLE_COUNT must be numeric";; esac
[ "$CYCLE_COUNT" -ge 1 ] && [ "$CYCLE_COUNT" -le 4 ] ||
    die "AIAM_A2DF_CYCLE_COUNT must be between 1 and 4"
[ ! -e "$OUT_DIR" ] && [ ! -L "$OUT_DIR" ] || die "OUT_DIR already exists; refuse to overwrite evidence"

# Refuse a mismatched topology before changing the guest radio.
[ "$AP_IF" = "$EXPECTED_AP_IF" ] || die "gate is restricted to AP_IF=$EXPECTED_AP_IF"
[ "$GUEST_IF" = en1 ] || die "gate is restricted to GUEST_IF=en1"
[ "$MANAGEMENT_IF" = en0 ] || die "gate is restricted to MANAGEMENT_IF=en0"
[ "$LAB_SSID" = AIAMlab6235 ] || die "gate is restricted to AIAMlab6235"
[ "$LAB_GATEWAY" = 10.77.0.1 ] || die "gate is restricted to 10.77.0.1"
[ "$LAB_IPV4_PREFIX" = 10.77.0. ] || die "gate is restricted to 10.77.0.0/24"

mkdir -p "$OUT_DIR"
exec > >(tee "$OUT_DIR/summary.log") 2>&1

guest() {
    "${GUEST[@]}" "$@"
}

cleanup() {
    local rc=$?
    trap - EXIT INT TERM
    if [ "${RADIO_OFF:-0}" -eq 1 ]; then
        printf 'recovery=attempt_radio_on_after_unsuccessful_run\n' >&2
        if guest "sudo -n networksetup -setairportpower $GUEST_IF on"; then
            if wait_guest_radio_power_on recovery; then
                printf 'recovery=radio_on_observed\n' >&2
            else
                printf 'recovery=radio_on_not_observed\n' >&2
            fi
        else
            printf 'recovery=radio_on_request_failed\n' >&2
        fi
    fi
    if [ -n "${KNOWN_HOSTS:-}" ]; then
        rm -f "$KNOWN_HOSTS"
    fi
    exit "$rc"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

prepare_guest_transport() {
    KNOWN_HOSTS="$(mktemp /tmp/aiam-qemu-lab-known-hosts.XXXXXX)"
    chmod 600 "$KNOWN_HOSTS"
    printf '%s\n' "$EXPECTED_GUEST_HOSTKEY_LINE" >"$KNOWN_HOSTS"
    GUEST=(
        ssh -F /dev/null -p 3322 -o BatchMode=yes -o ConnectTimeout=8
        -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$KNOWN_HOSTS"
        -o GlobalKnownHostsFile=/dev/null -o UpdateHostKeys=no -o LogLevel=ERROR
        devops@127.0.0.1
    )
}

assert_pinned_guest_hostkey_fingerprint() {
    local observed
    observed="$(ssh-keygen -lf "$KNOWN_HOSTS" -E sha256 2>/dev/null |
        awk 'NR == 1 { print $2; exit }')"
    printf 'pinned_guest_hostkey_sha256_observed=%s\n' "${observed:-missing}"
    [ "$observed" = "$EXPECTED_GUEST_HOSTKEY_SHA256" ] ||
        die "pinned guest host-key fingerprint mismatch"
}

assert_pinned_guest_identity() {
    local observed
    observed="$(guest 'set -u
printf "build=%s\\n" "$(sw_vers -buildVersion 2>/dev/null || true)"
printf "model=%s\\n" "$(sysctl -n hw.model 2>/dev/null || true)"
printf "management_mac=%s\\n" "$(ifconfig en0 2>/dev/null | awk '\''$1 == "ether" { print $2; exit }'\'')"')"
    printf '%s\n' "$observed"
    printf '%s\n' "$observed" | grep -Fx "build=$EXPECTED_GUEST_BUILD" >/dev/null ||
        die "pinned guest build mismatch"
    printf '%s\n' "$observed" | grep -Fx "model=$EXPECTED_GUEST_MODEL" >/dev/null ||
        die "pinned guest model mismatch"
    printf '%s\n' "$observed" | grep -Fx "management_mac=$EXPECTED_MANAGEMENT_MAC" >/dev/null ||
        die "pinned guest management MAC mismatch"
}

assert_pinned_lab_ap() {
    local observed
    observed="$(iw dev "$AP_IF" info 2>/dev/null | awk '
        $1 == "Interface" { print "interface=" $2 }
        $1 == "addr" { print "mac=" $2 }
        $1 == "ssid" { print "ssid=" $2 }
        $1 == "type" { print "type=" $2 }
        $1 == "channel" {
            print "channel=" $2
            for (i = 1; i <= NF; i++) if ($i == "width:") print "width_mhz=" $(i + 1)
        }
    ')"
    printf 'lab_ap_identity_begin\n%s\nlab_ap_identity_end\n' "$observed"
    printf '%s\n' "$observed" | grep -Fx "interface=$EXPECTED_AP_IF" >/dev/null ||
        die "pinned AP interface mismatch"
    printf '%s\n' "$observed" | grep -Fx "mac=$EXPECTED_AP_MAC" >/dev/null ||
        die "pinned AP MAC mismatch"
    printf '%s\n' "$observed" | grep -Fx "ssid=$LAB_SSID" >/dev/null ||
        die "pinned AP SSID mismatch"
    printf '%s\n' "$observed" | grep -Fx 'type=AP' >/dev/null ||
        die "pinned AP mode mismatch"
    printf '%s\n' "$observed" | grep -Fx "channel=$EXPECTED_AP_CHANNEL" >/dev/null ||
        die "pinned AP channel mismatch"
    printf '%s\n' "$observed" | grep -Fx "width_mhz=$EXPECTED_AP_WIDTH_MHZ" >/dev/null ||
        die "pinned AP width mismatch"
}

guest_wifi_mac() {
    guest "ifconfig $GUEST_IF 2>/dev/null | awk '\$1 == \"ether\" { print \$2; exit }'"
}

refresh_active_client_mac() {
    local phase="$1" observed
    observed="$(guest_wifi_mac || true)"
    valid_mac "$observed" || return 1
    if [ "$observed" != "$ACTIVE_CLIENT_MAC" ]; then
        printf 'guest_mac_change phase=%s previous=%s current=%s\n' \
            "$phase" "${ACTIVE_CLIENT_MAC:-none}" "$observed"
    fi
    ACTIVE_CLIENT_MAC="$observed"
    printf 'guest_mac phase=%s value=%s\n' "$phase" "$ACTIVE_CLIENT_MAC"
}

wait_active_client_mac() {
    local phase="$1" attempt
    for attempt in $(seq 1 15); do
        refresh_active_client_mac "$phase-poll-$attempt" && return 0
        sleep 1
    done
    return 1
}

station_dump() {
    local observed
    if ! observed="$(sudo -n iw dev "$AP_IF" station dump 2>&1)"; then
        printf 'host_station_dump_failed output=%s\n' "${observed:-missing}" >&2
        return 2
    fi
    printf '%s\n' "$observed"
}

station_present() {
    local mac="$1" observed
    observed="$(station_dump)" || return $?
    printf '%s\n' "$observed" | awk -v wanted="$mac" '
        tolower($1) == "station" && tolower($2) == tolower(wanted) { found = 1 }
        END { exit !found }
    '
}

station_authorized() {
    local observed
    [ -n "$ACTIVE_CLIENT_MAC" ] || return 1
    observed="$(station_dump)" || return $?
    printf '%s\n' "$observed" | awk -v wanted="$ACTIVE_CLIENT_MAC" '
        tolower($1) == "station" { active = (tolower($2) == tolower(wanted)); next }
        active && $1 == "authorized:" && $2 == "yes" { authorized = 1 }
        END { exit !authorized }
    '
}

station_associated_at() {
    local observed
    [ -n "$ACTIVE_CLIENT_MAC" ] || return 1
    observed="$(station_dump)" || return $?
    printf '%s\n' "$observed" | awk -v wanted="$ACTIVE_CLIENT_MAC" '
        tolower($1) == "station" { active = (tolower($2) == tolower(wanted)); next }
        active && $1 == "associated" && $2 == "at:" { print $3; found = 1; exit }
        END { exit !found }
    '
}

guest_default_route_signature() {
    guest "route -n get default 2>/dev/null | awk '/gateway:/{gateway=\$2} /interface:/{iface=\$2} END { if (iface != \"\") printf \"gateway=%s interface=%s\\n\", gateway, iface; else exit 1 }'"
}

guest_target_route_signature() {
    guest "route -n get $LAB_GATEWAY 2>/dev/null | awk -v expected='$LAB_GATEWAY' '
        /^[[:space:]]*destination:/ { destination=\$2 }
        /^[[:space:]]*gateway:/ { gateway=\$2 }
        /^[[:space:]]*interface:/ { iface=\$2 }
        END {
            if (destination != expected || iface == \"\") exit 1
            if (gateway == \"\") gateway = \"direct\"
            printf \"destination=%s nexthop=%s interface=%s\\n\", destination, gateway, iface
        }
    '"
}

guest_existing_lab_ip() {
    guest "ifconfig $GUEST_IF 2>/dev/null | awk '\$1 == \"inet\" && \$2 ~ /^10\\.77\\.0\\./ { print \$2; exit }'"
}

guest_management_ipv4() {
    guest "ifconfig $MANAGEMENT_IF 2>/dev/null | awk '\$1 == \"inet\" { print \$2; exit }'"
}

capture_dhcp_sample() {
    local phase="$1" interface="$2" stdout_path stderr_path status_path
    local status stdout_bytes stderr_bytes stdout_sha256 stderr_sha256 availability
    local sample_text_format local_evidence_error status_write_error status_payload
    local network_options transaction_timing
    valid_name "$phase" || die "DHCP sample phase contains unsupported characters"
    valid_name "$interface" || die "DHCP sample interface contains unsupported characters"
    stdout_path="$OUT_DIR/dhcp-$phase-$interface.ipconfig-getpacket.stdout"
    stderr_path="$OUT_DIR/dhcp-$phase-$interface.stderr"
    status_path="$OUT_DIR/dhcp-$phase-$interface.status"
    stdout_bytes=unavailable
    stderr_bytes=unavailable
    stdout_sha256=unavailable
    stderr_sha256=unavailable
    sample_text_format=INCONCLUSIVE
    local_evidence_error=""

    if guest "ipconfig getpacket $interface" >"$stdout_path" 2>"$stderr_path"; then
        status=0
    else
        status=$?
        local_evidence_error="ipconfig_exit_$status"
    fi

    if [ -r "$stdout_path" ]; then
        if stdout_bytes="$(wc -c <"$stdout_path" | tr -d '[:space:]')" &&
            [[ "$stdout_bytes" =~ ^[0-9]+$ ]]; then
            :
        else
            local_evidence_error="${local_evidence_error:+$local_evidence_error,}stdout_size_unavailable"
        fi
        if stdout_sha256="$(sha256sum "$stdout_path" | awk '{ print $1 }')" &&
            [[ "$stdout_sha256" =~ ^[[:xdigit:]]{64}$ ]]; then
            :
        else
            local_evidence_error="${local_evidence_error:+$local_evidence_error,}stdout_hash_unavailable"
        fi
    else
        local_evidence_error="${local_evidence_error:+$local_evidence_error,}stdout_missing"
    fi

    if [ -r "$stderr_path" ]; then
        if stderr_bytes="$(wc -c <"$stderr_path" | tr -d '[:space:]')" &&
            [[ "$stderr_bytes" =~ ^[0-9]+$ ]]; then
            :
        else
            local_evidence_error="${local_evidence_error:+$local_evidence_error,}stderr_size_unavailable"
        fi
        if stderr_sha256="$(sha256sum "$stderr_path" | awk '{ print $1 }')" &&
            [[ "$stderr_sha256" =~ ^[[:xdigit:]]{64}$ ]]; then
            :
        else
            local_evidence_error="${local_evidence_error:+$local_evidence_error,}stderr_hash_unavailable"
        fi
    else
        local_evidence_error="${local_evidence_error:+$local_evidence_error,}stderr_missing"
    fi

    if [ -r "$stdout_path" ]; then
        if network_options="$(awk '/^(server_identifier|subnet_mask|broadcast_address|domain_name_server|router)/ { print }' "$stdout_path" | LC_ALL=C sort)"; then
            :
        else
            network_options=unavailable
            local_evidence_error="${local_evidence_error:+$local_evidence_error,}network_option_parser_failed"
        fi
        if transaction_timing="$(awk '/^(xid|ciaddr|yiaddr|siaddr|lease_time|renewal_t1_time_value|rebinding_t2_time_value)/ { print }' "$stdout_path" | LC_ALL=C sort)"; then
            :
        else
            transaction_timing=unavailable
            local_evidence_error="${local_evidence_error:+$local_evidence_error,}transaction_timing_parser_failed"
        fi
    else
        network_options=unavailable
        transaction_timing=unavailable
    fi

    if [ "$status" -eq 0 ] && [[ "$stdout_bytes" =~ ^[1-9][0-9]*$ ]]; then
        if grep -Fxq 'op = BOOTREPLY' "$stdout_path" &&
            grep -Eq '^yiaddr = [^[:space:]]+' "$stdout_path"; then
            sample_text_format=BOOTP_REPLY_TEXT
        else
            local_evidence_error="${local_evidence_error:+$local_evidence_error,}unexpected_ipconfig_getpacket_text"
        fi
    else
        local_evidence_error="${local_evidence_error:+$local_evidence_error,}stdout_empty_or_capture_failed"
    fi

    if [ -z "$local_evidence_error" ]; then
        availability=COMPLETE
    else
        availability=INCONCLUSIVE
        DHCP_OBSERVATION_INCONCLUSIVE=1
    fi

    if ! printf -v status_payload \
        'phase=%s\ninterface=%s\nsample_kind=ipconfig_getpacket_stdout_text\nsample_availability=%s\nsample_text_format=%s\nlocal_evidence_error=%s\nexit_status=%s\nstdout_bytes=%s\nstdout_sha256=%s\nstderr_bytes=%s\nstderr_sha256=%s\n' \
        "$phase" "$interface" "$availability" "$sample_text_format" \
        "${local_evidence_error:-none}" "$status" "$stdout_bytes" "$stdout_sha256" \
        "$stderr_bytes" "$stderr_sha256"; then
        DHCP_OBSERVATION_INCONCLUSIVE=1
        status_write_error="${local_evidence_error:+$local_evidence_error,}status_payload_build_failed"
        printf 'ipconfig_getpacket_stdout_sample phase=%s interface=%s sample_availability=INCONCLUSIVE exit_status=%s stdout=%s stderr=%s local_evidence_error=%s\n' \
            "$phase" "$interface" "$status" "$stdout_path" "$stderr_path" "$status_write_error"
        return 0
    fi
    if ! printf '%s' "$status_payload" >"$status_path"; then
        DHCP_OBSERVATION_INCONCLUSIVE=1
        status_write_error="${local_evidence_error:+$local_evidence_error,}status_write_failed"
        printf 'ipconfig_getpacket_stdout_sample phase=%s interface=%s sample_availability=INCONCLUSIVE exit_status=%s stdout=%s stderr=%s local_evidence_error=%s\n' \
            "$phase" "$interface" "$status" "$stdout_path" "$stderr_path" "$status_write_error"
        return 0
    fi

    printf 'ipconfig_getpacket_stdout_sample phase=%s interface=%s sample_availability=%s sample_text_format=%s exit_status=%s stdout=%s stdout_sha256=%s stderr=%s stderr_sha256=%s local_evidence_error=%s\n' \
        "$phase" "$interface" "$availability" "$sample_text_format" "$status" "$stdout_path" "$stdout_sha256" "$stderr_path" "$stderr_sha256" "${local_evidence_error:-none}"
    printf 'ipconfig_getpacket_network_option_observation_begin phase=%s interface=%s\n%s\nipconfig_getpacket_network_option_observation_end phase=%s interface=%s\n' \
        "$phase" "$interface" "${network_options:-none}" "$phase" "$interface"
    printf 'ipconfig_getpacket_transaction_timing_observation_begin phase=%s interface=%s\n%s\nipconfig_getpacket_transaction_timing_observation_end phase=%s interface=%s\n' \
        "$phase" "$interface" "${transaction_timing:-none}" "$phase" "$interface"
    return 0
}

capture_online_dhcp_samples() {
    local phase="$1"
    capture_dhcp_sample "$phase" "$MANAGEMENT_IF"
    capture_dhcp_sample "$phase" "$GUEST_IF"
}

capture_read_only_state() {
    local phase="$1"
    {
        printf 'phase=%s\n' "$phase"
        printf '%s\n' '__DEFAULT_ROUTE_BEST_EFFORT_BEGIN__'
        guest 'route -n get default 2>&1 || true'
        printf '%s\n' '__LAB_ROUTE_BEST_EFFORT_BEGIN__'
        guest "route -n get $LAB_GATEWAY 2>&1 || true"
        printf '%s\n' '__MANAGEMENT_IF_BEST_EFFORT_BEGIN__'
        guest "ifconfig $MANAGEMENT_IF 2>&1 || true"
        printf '%s\n' '__WIFI_IF_BEST_EFFORT_BEGIN__'
        guest "ifconfig $GUEST_IF 2>&1 || true"
        printf '%s\n' '__INET_ROUTES_BEST_EFFORT_BEGIN__'
        guest 'netstat -rn -f inet 2>&1 || true'
        printf '%s\n' '__NETWORK_INFORMATION_BEST_EFFORT_BEGIN__'
        guest 'scutil --nwi 2>&1 || true'
        printf '%s\n' '__WIFI_POWER_BEST_EFFORT_BEGIN__'
        guest "networksetup -getairportpower $GUEST_IF 2>&1 || true"
    } >"$OUT_DIR/state-$phase.txt"
}

assert_default_route_preserved() {
    local phase="$1" observed
    observed="$(guest_default_route_signature || true)"
    printf 'default_route_%s=%s\n' "$phase" "${observed:-missing}"
    [ "$observed" = "$DEFAULT_ROUTE_BASELINE" ] ||
        die "default route changed at $phase: expected '$DEFAULT_ROUTE_BASELINE', observed '${observed:-missing}'"
}

assert_management_invariants() {
    local phase="$1" current_ip
    assert_default_route_preserved "$phase"
    current_ip="$(guest_management_ipv4 || true)"
    printf 'management_ipv4_%s=%s\n' "$phase" "${current_ip:-none}"
    [ "$current_ip" = "$PREEXISTING_MANAGEMENT_IP" ] ||
        die "management IPv4 changed at $phase: expected '$PREEXISTING_MANAGEMENT_IP', observed '${current_ip:-none}'"
}

assert_observed_network_invariants() {
    local phase="$1" current_lab_ip target_signature
    assert_management_invariants "$phase"
    current_lab_ip="$(guest_existing_lab_ip || true)"
    printf 'lab_ipv4_%s=%s\n' "$phase" "${current_lab_ip:-none}"
    [ "$current_lab_ip" = "$PREEXISTING_LAB_IP" ] ||
        die "Wi-Fi IPv4 changed at $phase: expected '$PREEXISTING_LAB_IP', observed '${current_lab_ip:-none}'"
    target_signature="$(guest_target_route_signature || true)"
    printf 'lab_target_route_%s=%s\n' "$phase" "${target_signature:-none}"
    [ "$target_signature" = "$TARGET_ROUTE_BASELINE" ] ||
        die "route to $LAB_GATEWAY changed at $phase: expected '$TARGET_ROUTE_BASELINE', observed '${target_signature:-none}'"
}

wait_absent() {
    local mac="$1" attempt rc
    for attempt in $(seq 1 15); do
        if station_present "$mac"; then
            :
        else
            rc=$?
            [ "$rc" -eq 1 ] || return "$rc"
            printf 'station_absent_attempt=%s mac=%s\n' "$attempt" "$mac"
            return 0
        fi
        sleep 1
    done
    return 1
}

wait_guest_radio_power_off() {
    local attempt power
    for attempt in $(seq 1 "$OFF_STATE_TIMEOUT_ATTEMPTS"); do
        power="$(guest "networksetup -getairportpower $GUEST_IF 2>/dev/null || true" | tr '\n' ' ')"
        printf 'guest_radio_off_poll=%s power=%s\n' \
            "$attempt" "${power:-unknown}"
        if printf '%s\n' "$power" | grep -Fq ': Off'; then
            return 0
        fi
        sleep 1
    done
    return 1
}

wait_guest_radio_power_on() {
    local phase="$1" attempt power
    for attempt in $(seq 1 "$OFF_STATE_TIMEOUT_ATTEMPTS"); do
        power="$(guest "networksetup -getairportpower $GUEST_IF 2>/dev/null || true" | tr '\n' ' ')"
        printf 'guest_radio_on_poll=%s phase=%s power=%s\n' \
            "$attempt" "$phase" "${power:-unknown}"
        if printf '%s\n' "$power" | grep -Fq ': On'; then
            return 0
        fi
        sleep 1
    done
    return 1
}

assert_guest_radio_power_on() {
    local phase="$1" power
    power="$(guest "networksetup -getairportpower $GUEST_IF 2>/dev/null || true" | tr '\n' ' ')"
    printf 'guest_radio_on_assert phase=%s power=%s\n' "$phase" "${power:-unknown}"
    printf '%s\n' "$power" | grep -Fq ': On'
}

wait_authorized_stable() {
    local cycle="$1" attempt streak=0 rc
    for attempt in $(seq 1 "$ASSOC_TIMEOUT_ATTEMPTS"); do
        refresh_active_client_mac "cycle-$cycle-authorized-$attempt" || true
        if station_authorized; then
            streak=$((streak + 1))
        else
            rc=$?
            [ "$rc" -eq 1 ] || return "$rc"
            streak=0
        fi
        printf 'authorized_poll=%s streak=%s mac=%s\n' \
            "$attempt" "$streak" "${ACTIVE_CLIENT_MAC:-none}"
        [ "$streak" -ge 3 ] && return 0
        sleep 2
    done
    return 1
}

wait_fresh_association() {
    local cycle="$1" previous="$2" attempt current rc
    for attempt in $(seq 1 "$ASSOC_TIMEOUT_ATTEMPTS"); do
        refresh_active_client_mac "cycle-$cycle-fresh-assoc-$attempt" || true
        if current="$(station_associated_at)"; then
            :
        else
            rc=$?
            [ "$rc" -eq 1 ] || return "$rc"
            current=""
        fi
        printf 'fresh_assoc_poll=%s previous=%s current=%s mac=%s\n' \
            "$attempt" "${previous:-none}" "${current:-none}" "${ACTIVE_CLIENT_MAC:-none}"
        [ -n "$current" ] && [ "$current" != "${previous:-}" ] && return 0
        sleep 2
    done
    return 1
}

verify_preexisting_data_plane() {
    local cycle="$1" current_ip target_signature ping_output
    assert_observed_network_invariants "cycle_${cycle}_before_ping"
    current_ip="$(guest_existing_lab_ip || true)"
    printf 'cycle=%s preexisting_lab_ip=%s current_lab_ip=%s\n' \
        "$cycle" "$PREEXISTING_LAB_IP" "${current_ip:-none}"
    [ "$current_ip" = "$PREEXISTING_LAB_IP" ] ||
        die "cycle $cycle lost or replaced the pre-existing $GUEST_IF lab address"
    target_signature="$(guest_target_route_signature || true)"
    printf 'cycle=%s lab_target_route_signature=%s\n' "$cycle" "${target_signature:-none}"
    if ! ping_output="$(guest "ping -S $PREEXISTING_LAB_IP -c 5 -W 1000 $LAB_GATEWAY")"; then
        printf '%s\n' "${ping_output:-ping command failed before producing output}"
        die "cycle $cycle laboratory ping failed"
    fi
    printf '%s\n' "$ping_output"
    printf '%s\n' "$ping_output" |
        grep -Eq '5 packets transmitted, 5 packets received, 0\.0% packet loss'
    capture_online_dhcp_samples "cycle-$cycle-after-ping-before-invariant"
    capture_read_only_state "cycle-$cycle-after-ping-before-invariant"
    assert_observed_network_invariants "cycle_${cycle}_after_ping"
}

prepare_guest_transport
assert_pinned_guest_hostkey_fingerprint
assert_pinned_guest_identity
assert_pinned_lab_ap

guest "networksetup -listpreferredwirelessnetworks $GUEST_IF 2>/dev/null | awk 'NR > 1 {line=\$0; sub(/^[[:space:]]+/, \"\", line); sub(/[[:space:]]+$/, \"\", line); if (line == \"$LAB_SSID\") found=1} END {exit !found}'" ||
    die "saved profile for $LAB_SSID is absent"

DEFAULT_ROUTE_BASELINE="$(guest_default_route_signature || true)"
[ -n "$DEFAULT_ROUTE_BASELINE" ] || die "cannot read guest default route"
case "$DEFAULT_ROUTE_BASELINE" in *"interface=$MANAGEMENT_IF") ;; *)
    die "guest default route is not on $MANAGEMENT_IF: $DEFAULT_ROUTE_BASELINE";; esac
PREEXISTING_LAB_IP="$(guest_existing_lab_ip || true)"
[ -n "$PREEXISTING_LAB_IP" ] ||
    die "no pre-existing $GUEST_IF address in 10.77.0.0/24; refusing to request a lease"
PREEXISTING_MANAGEMENT_IP="$(guest_management_ipv4 || true)"
[ -n "$PREEXISTING_MANAGEMENT_IP" ] ||
    die "no pre-existing $MANAGEMENT_IF IPv4 address"
TARGET_ROUTE_BASELINE="$(guest_target_route_signature || true)"
[ -n "$TARGET_ROUTE_BASELINE" ] ||
    die "cannot read preflight route signature for $LAB_GATEWAY"
case "$TARGET_ROUTE_BASELINE" in
    "destination=$LAB_GATEWAY nexthop=direct interface=$GUEST_IF") ;;
    *) die "preflight route for $LAB_GATEWAY is not the pinned direct $GUEST_IF route: $TARGET_ROUTE_BASELINE";;
esac
assert_guest_radio_power_on preflight ||
    die "preflight radio power is not observably on"
wait_active_client_mac preflight || die "cannot read preflight guest Wi-Fi MAC"
assert_pinned_lab_ap
station_authorized || die "preflight guest Wi-Fi MAC is not authorized on the pinned AP"
capture_online_dhcp_samples preflight

printf 'gate_mode=explicit_network_command_free_observed_invariants\n'
printf 'guest_management_interface=%s\n' "$MANAGEMENT_IF"
printf 'guest_wifi_interface=%s\n' "$GUEST_IF"
printf 'lab_ssid=%s\n' "$LAB_SSID"
printf 'lab_gateway=%s\n' "$LAB_GATEWAY"
printf 'explicit_route_command=none\n'
printf 'explicit_address_command=none\n'
printf 'explicit_dhcp_state_mutating_command=none\n'
printf 'read_only_dhcp_observation=ipconfig_getpacket\n'
printf 'os_managed_transient_network_activity_not_claimed=true\n'
printf 'connection_trigger=saved_profile_autojoin_only\n'
printf 'requested_cycle_count=%s\n' "$CYCLE_COUNT"
printf 'default_route_baseline=%s\n' "$DEFAULT_ROUTE_BASELINE"
printf 'target_route_baseline=%s\n' "$TARGET_ROUTE_BASELINE"
printf 'preexisting_lab_ip=%s\n' "$PREEXISTING_LAB_IP"
printf 'preexisting_management_ip=%s\n' "$PREEXISTING_MANAGEMENT_IP"
printf 'client_mac_preflight=%s\n' "$ACTIVE_CLIENT_MAC"
capture_read_only_state preflight

for cycle in $(seq 1 "$CYCLE_COUNT"); do
    printf '\ncycle=%s begin\n' "$cycle"
    refresh_active_client_mac "cycle-$cycle-before-off" ||
        die "cycle $cycle cannot read guest Wi-Fi MAC before radio off"
    mac_before_off="$ACTIVE_CLIENT_MAC"
    assert_pinned_lab_ap
    station_authorized ||
        die "cycle $cycle pre-OFF guest Wi-Fi MAC is not authorized on the pinned AP"
    if previous_assoc="$(station_associated_at)"; then
        :
    else
        station_rc=$?
        [ "$station_rc" -eq 1 ] ||
            die "cycle $cycle cannot query pre-OFF AP association epoch"
        previous_assoc=""
    fi
    assert_guest_radio_power_on "cycle-$cycle-pre-off" ||
        die "cycle $cycle radio power is not observably on before OFF"
    printf 'cycle=%s guest_mac_before_off=%s previous_assoc=%s\n' \
        "$cycle" "$mac_before_off" "${previous_assoc:-none}"

    RADIO_OFF=1
    printf 'radio_off_recovery_pending=1 cycle=%s\n' "$cycle"
    guest "sudo -n networksetup -setairportpower $GUEST_IF off"
    wait_guest_radio_power_off ||
        die "cycle $cycle radio power did not become observably off"
    assert_management_invariants "cycle_${cycle}_after_off"
    assert_pinned_lab_ap
    wait_absent "$mac_before_off" || die "cycle $cycle station remained present after radio off"
    capture_read_only_state "cycle-$cycle-radio-off"

    guest "sudo -n networksetup -setairportpower $GUEST_IF on"
    wait_guest_radio_power_on "cycle-$cycle-after-on" ||
        die "cycle $cycle radio power did not become observably on"
    RADIO_OFF=0
    wait_active_client_mac "cycle-$cycle-after-on" ||
        die "cycle $cycle cannot read guest Wi-Fi MAC after radio on"
    printf 'cycle=%s guest_mac_after_on=%s\n' "$cycle" "$ACTIVE_CLIENT_MAC"
    printf 'lab_join_request=saved_profile_autojoin_only\n'
    assert_pinned_lab_ap
    wait_authorized_stable "$cycle" ||
        die "cycle $cycle did not reach stable AP authorization"
    wait_fresh_association "$cycle" "$previous_assoc" ||
        die "cycle $cycle did not produce a fresh AP association epoch"
    capture_online_dhcp_samples "cycle-$cycle-after-auth-before-invariant"
    capture_read_only_state "cycle-$cycle-after-auth-before-invariant"
    verify_preexisting_data_plane "$cycle"

    assert_pinned_lab_ap
    station_authorized || die "cycle $cycle authorization disappeared after ping"
    station_dump | awk -v wanted="$ACTIVE_CLIENT_MAC" '
        tolower($1) == "station" { active = (tolower($2) == tolower(wanted)); next }
        active && ($1 == "authorized:" || $1 == "connected" ||
            ($1 == "rx" && $2 == "bytes:") || ($1 == "tx" && $2 == "bytes:")) { print }
    '
    capture_read_only_state "cycle-$cycle-post"
    if ! assert_guest_radio_power_on "cycle-$cycle-post"; then
        RADIO_OFF=1
        die "cycle $cycle ended with radio power not on"
    fi
    printf 'cycle=%s functional_result=PASS\n' "$cycle"
done

capture_online_dhcp_samples final-before-invariant
capture_read_only_state final-before-invariant
assert_observed_network_invariants final
if ! assert_guest_radio_power_on final; then
    RADIO_OFF=1
    die "final radio power is not on"
fi
assert_pinned_lab_ap
if [ "$DHCP_OBSERVATION_INCONCLUSIVE" -ne 0 ]; then
    if [ "$CYCLE_COUNT" -eq 4 ]; then
        printf 'four_cycle_functional_result=PASS\n'
    else
        printf 'bounded_trace_window_functional_result=PASS\n'
    fi
    printf 'ipconfig_getpacket_stdout_observation_result=INCONCLUSIVE\n'
    die "one or more ipconfig getpacket textual observations were unavailable or incomplete"
fi
printf 'ipconfig_getpacket_stdout_observation_result=COMPLETE\n'
if [ "$CYCLE_COUNT" -eq 4 ]; then
    printf 'four_cycle_functional_result=PASS\n'
    printf 'four_cycle_result=PASS\n'
else
    printf 'bounded_trace_window_cycle_count=%s\n' "$CYCLE_COUNT"
    printf 'bounded_trace_window_functional_result=PASS\n'
    printf 'bounded_trace_window_result=PASS\n'
fi
