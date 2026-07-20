#!/usr/bin/env bash
# Deterministic static and local-fixture guard for the pinned lab radio runner.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_lab_radio_gates_no_route_mutation.sh"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

require_literal() {
    local needle="$1" label="$2"
    grep -Fq -- "$needle" "$RUNNER" || fail "missing $label: $needle"
}

forbid_literal() {
    local needle="$1" label="$2"
    ! grep -Fq -- "$needle" "$RUNNER" || fail "forbidden $label: $needle"
}

require_ordered_on_recovery() {
    awk '
        /guest "sudo -n networksetup -setairportpower \$GUEST_IF on"/ {
            requested_on = 1
            next
        }
        requested_on && /wait_guest_radio_power_on "cycle-\$cycle-after-on"/ {
            observed_on = 1
            next
        }
        observed_on && /RADIO_OFF=0/ {
            cleared_after_observation = 1
            exit
        }
        END { exit !(requested_on && observed_on && cleared_after_observation) }
    ' "$RUNNER" || fail 'radio recovery state is not cleared only after observed On'
}

require_ordered_off_recovery() {
    awk '
        /for cycle in \$\(seq 1 4\); do/ { in_cycle = 1; next }
        in_cycle && /RADIO_OFF=1/ { recovery_pending = 1; next }
        recovery_pending && /guest "sudo -n networksetup -setairportpower \$GUEST_IF off"/ {
            requested_off = 1
            next
        }
        requested_off && /wait_guest_radio_power_off/ {
            observed_off = 1
            exit
        }
        END { exit !(recovery_pending && requested_off && observed_off) }
    ' "$RUNNER" || fail 'radio recovery is not pending before OFF or OFF is not explicitly observed'
}

require_preflight_known_on_authorized() {
    awk '
        /assert_guest_radio_power_on preflight/ { known_on = 1; next }
        known_on && /wait_active_client_mac preflight/ { current_mac = 1; next }
        current_mac && /station_authorized/ { authorized = 1; next }
        /RADIO_OFF=1/ { first_pending = 1; exit }
        END { exit !(known_on && current_mac && authorized && first_pending) }
    ' "$RUNNER" || fail 'preflight does not prove radio On and AP authorization before OFF recovery is pending'
}

require_cycle_known_on_authorized() {
    awk '
        /for cycle in \$\(seq 1 4\); do/ { in_cycle = 1; next }
        in_cycle && /station_authorized \|\|/ { authorized = 1; next }
        authorized && /assert_guest_radio_power_on "cycle-\$cycle-pre-off"/ { known_on = 1; next }
        known_on && /RADIO_OFF=1/ { recovery_pending = 1; exit }
        END { exit !(authorized && known_on && recovery_pending) }
    ' "$RUNNER" || fail 'cycle does not prove AP authorization and radio On before OFF recovery is pending'
}

require_textual_dhcp_capture_before_functional_gates() {
    awk '
        /capture_online_dhcp_samples "cycle-\$cycle-after-auth-before-invariant"/ { after_auth = 1; next }
        after_auth && /verify_preexisting_data_plane "\$cycle"/ { pre_ping_gate = 1; next }
        /capture_online_dhcp_samples "cycle-\$cycle-after-ping-before-invariant"/ { after_ping = 1; next }
        after_ping && /assert_observed_network_invariants "cycle_\$\{cycle\}_after_ping"/ { post_ping_gate = 1; next }
        /capture_online_dhcp_samples final-before-invariant/ { final_capture = 1; next }
        final_capture && /assert_observed_network_invariants final/ { final_gate = 1; exit }
        END { exit !(after_auth && pre_ping_gate && after_ping && post_ping_gate && final_capture && final_gate) }
    ' "$RUNNER" || fail 'textual ipconfig-getpacket samples are not captured before functional invariant gates'
}

require_dhcp_sample_failure_containment() {
    awk '
        /capture_dhcp_sample\(\)/ { in_sample = 1; next }
        in_sample && /capture_online_dhcp_samples\(\)/ { exit }
        in_sample && /local_evidence_error=/ { has_error_state = 1 }
        in_sample && /DHCP_OBSERVATION_INCONCLUSIVE=1/ { marks_inconclusive = 1 }
        in_sample && /status_payload_build_failed/ { handles_status_payload = 1 }
        in_sample && /status_write_failed/ { handles_status_write = 1 }
        in_sample && /network_option_parser_failed/ { handles_network_parser = 1 }
        in_sample && /transaction_timing_parser_failed/ { handles_timing_parser = 1 }
        in_sample && /return 0/ { normalizes_return = 1 }
        END {
            exit !(has_error_state && marks_inconclusive && handles_status_payload && handles_status_write &&
                handles_network_parser && handles_timing_parser && normalizes_return)
        }
    ' "$RUNNER" || fail 'DHCP textual-sample local evidence failures are not contained as INCONCLUSIVE'
}

require_no_dhcp_sample_error_suppression() {
    awk '
        /capture_online_dhcp_samples\(\)/ { in_online = 1; next }
        in_online && /^}/ { exit }
        in_online && /capture_dhcp_sample.*\|\|[[:space:]]*true/ { suppressed = 1 }
        END { exit suppressed }
    ' "$RUNNER" || fail 'DHCP textual-sample errors are conditionally suppressed'
}

require_exact_dhcp_sample_call_shape() {
    local invocation_count
    invocation_count="$(grep -Fc 'guest "ipconfig getpacket $interface"' "$RUNNER")"
    [ "$invocation_count" -eq 1 ] ||
        fail "expected one ipconfig-getpacket invocation site, found $invocation_count"
    awk '
        /capture_online_dhcp_samples\(\)/ { in_online = 1; next }
        in_online && /^}/ { exit }
        in_online && /capture_dhcp_sample "\$phase" "\$MANAGEMENT_IF"/ { management_calls++ }
        in_online && /capture_dhcp_sample "\$phase" "\$GUEST_IF"/ { wifi_calls++ }
        END { exit !(management_calls == 1 && wifi_calls == 1) }
    ' "$RUNNER" || fail 'each named DHCP phase does not capture exactly the management/Wi-Fi pair'
}

require_dhcp_inconclusive_blocks_unqualified_pass() {
    local unqualified_pass_count
    unqualified_pass_count="$(grep -Fc "printf 'four_cycle_result=PASS\\n'" "$RUNNER")"
    [ "$unqualified_pass_count" -eq 1 ] ||
        fail "expected one unqualified four-cycle PASS marker, found $unqualified_pass_count"
    awk '
        /if \[ "\$DHCP_OBSERVATION_INCONCLUSIVE" -ne 0 \]; then/ { in_inconclusive = 1; next }
        in_inconclusive && /ipconfig_getpacket_stdout_observation_result=INCONCLUSIVE/ { names_inconclusive = 1 }
        in_inconclusive && /die "one or more ipconfig getpacket textual observations were unavailable or incomplete"/ { dies = 1 }
        in_inconclusive && /^fi$/ { after_inconclusive = 1; next }
        after_inconclusive && /four_cycle_result=PASS/ { unqualified_pass_after_die = 1; exit }
        END { exit !(names_inconclusive && dies && unqualified_pass_after_die) }
    ' "$RUNNER" || fail 'DHCP INCONCLUSIVE branch does not terminate before the sole unqualified PASS marker'
}

load_capture_dhcp_sample_fixture() {
    local sample_function
    sample_function="$(awk '
        /^capture_dhcp_sample\(\)[[:space:]]*\{/ { in_sample = 1 }
        in_sample { print }
        in_sample && /^}/ { exit }
    ' "$RUNNER")"
    [ -n "$sample_function" ] || fail 'cannot extract capture_dhcp_sample fixture'
    eval "$sample_function"
}

run_dhcp_sample_fixture() {
    local fixture_root="$1" fixture_case="$2" expected_error="$3"
    local fixture_dir="$fixture_root/$fixture_case"
    mkdir -p "$fixture_dir"
    (
        set -euo pipefail
        OUT_DIR="$fixture_dir"
        DHCP_OBSERVATION_INCONCLUSIVE=0
        valid_name() {
            [[ "$1" =~ ^[A-Za-z0-9_.-]+$ ]]
        }
        die() {
            printf 'fixture die: %s\n' "$*" >&2
            exit 1
        }
        guest() {
            case "$fixture_case" in
                capture_failure)
                    return 42
                    ;;
                unexpected_text)
                    printf '%s\n' 'not an ipconfig getpacket BOOTP rendering'
                    ;;
                *)
                    printf '%s\n' \
                        'op = BOOTREPLY' \
                        'xid = 0x1234' \
                        'yiaddr = 10.77.0.47' \
                        'server_identifier (ip): 10.77.0.1' \
                        'router (ip_mult): {10.77.0.1}'
                    ;;
            esac
        }
        case "$fixture_case" in
            hash_failure)
                sha256sum() {
                    return 1
                }
                ;;
            parser_failure)
                awk() {
                    case "$1" in
                        *server_identifier*) return 1 ;;
                        *) command awk "$@" ;;
                    esac
                }
                ;;
            status_payload_failure)
                printf() {
                    if [ "${1:-}" = -v ]; then
                        return 1
                    fi
                    builtin printf "$@"
                }
                ;;
            status_path_failure)
                mkdir "$OUT_DIR/dhcp-unit-en1.status"
                ;;
        esac
        load_capture_dhcp_sample_fixture
        capture_dhcp_sample unit en1 >"$fixture_dir/fixture.log" 2>&1

        if [ "$fixture_case" = success ]; then
            [ "$DHCP_OBSERVATION_INCONCLUSIVE" -eq 0 ]
            grep -Fxq 'sample_availability=COMPLETE' "$OUT_DIR/dhcp-unit-en1.status"
            grep -Fxq 'sample_text_format=BOOTP_REPLY_TEXT' "$OUT_DIR/dhcp-unit-en1.status"
            grep -Fxq 'local_evidence_error=none' "$OUT_DIR/dhcp-unit-en1.status"
        else
            [ "$DHCP_OBSERVATION_INCONCLUSIVE" -eq 1 ]
            if [ "$fixture_case" = status_payload_failure ]; then
                grep -Fq 'status_payload_build_failed' "$fixture_dir/fixture.log"
            elif [ "$fixture_case" = status_path_failure ]; then
                grep -Fq 'status_write_failed' "$fixture_dir/fixture.log"
            else
                grep -Fxq 'sample_availability=INCONCLUSIVE' "$OUT_DIR/dhcp-unit-en1.status"
                grep -Fq "$expected_error" "$OUT_DIR/dhcp-unit-en1.status"
            fi
        fi
    )
}

run_dhcp_sample_fixture_tests() (
    set -euo pipefail
    fixture_root="$(mktemp -d "${TMPDIR:-/tmp}/aiam-dhcp-sample-fixture.XXXXXX")"
    trap 'rm -rf "$fixture_root"' EXIT
    run_dhcp_sample_fixture "$fixture_root" success ''
    run_dhcp_sample_fixture "$fixture_root" capture_failure ipconfig_exit_42
    run_dhcp_sample_fixture "$fixture_root" hash_failure stdout_hash_unavailable
    run_dhcp_sample_fixture "$fixture_root" parser_failure network_option_parser_failed
    run_dhcp_sample_fixture "$fixture_root" unexpected_text unexpected_ipconfig_getpacket_text
    run_dhcp_sample_fixture "$fixture_root" status_payload_failure status_payload_build_failed
    run_dhcp_sample_fixture "$fixture_root" status_path_failure status_write_failed
)

[ -f "$RUNNER" ] || fail "runner is missing"
bash -n "$RUNNER"

require_literal 'GUEST_IF="${GUEST_IF:-en1}"' 'en1 Wi-Fi default'
require_literal 'MANAGEMENT_IF="${MANAGEMENT_IF:-en0}"' 'en0 management default'
require_literal '[ "$GUEST_IF" = en1 ]' 'en1 topology guard'
require_literal '[ "$MANAGEMENT_IF" = en0 ]' 'en0 topology guard'
require_literal '[ "$LAB_GATEWAY" = 10.77.0.1 ]' 'lab-only target guard'
require_literal 'EXPECTED_AP_IF="wlp0s20f3"' 'pinned AP interface'
require_literal 'EXPECTED_AP_MAC="80:e4:ba:20:ef:f9"' 'pinned AP MAC'
require_literal 'EXPECTED_AP_CHANNEL="153"' 'pinned AP channel'
require_literal 'EXPECTED_AP_WIDTH_MHZ="80"' 'pinned AP width'
require_literal '[ "$AP_IF" = "$EXPECTED_AP_IF" ]' 'AP interface restriction'
require_literal 'assert_pinned_lab_ap' 'lab AP identity assertion'
require_literal 'lab_ap_identity_begin' 'recorded lab AP identity'
require_literal 'EXPECTED_GUEST_HOSTKEY_SHA256=' 'pinned guest host-key fingerprint'
require_literal 'EXPECTED_GUEST_HOSTKEY_LINE=' 'pinned guest host-key material'
require_literal 'assert_pinned_guest_hostkey_fingerprint' 'local host-key fingerprint assertion'
require_literal 'ssh-keygen -lf "$KNOWN_HOSTS" -E sha256' 'derived host-key fingerprint check'
require_literal 'StrictHostKeyChecking=yes' 'strict SSH host-key check'
require_literal 'UserKnownHostsFile="$KNOWN_HOSTS"' 'private pinned known-hosts file'
require_literal 'GlobalKnownHostsFile=/dev/null' 'no ambient known-hosts trust'
require_literal 'assert_pinned_guest_identity' 'guest identity check'
require_literal 'EXPECTED_GUEST_BUILD="25C56"' 'guest build pin'
require_literal 'EXPECTED_GUEST_MODEL="MacPro7,1"' 'guest model pin'
require_literal 'EXPECTED_MANAGEMENT_MAC="52:54:00:c9:18:28"' 'management MAC pin'
require_literal 'OUT_DIR already exists; refuse to overwrite evidence' 'fresh evidence directory gate'
require_literal 'trap cleanup EXIT' 'failure cleanup trap'
require_literal 'recovery=attempt_radio_on_after_unsuccessful_run' 'radio recovery evidence'
require_literal 'recovery=radio_on_observed' 'observed radio recovery success'
require_literal 'networksetup -setairportpower $GUEST_IF off' 'radio-off transition'
require_literal 'networksetup -setairportpower $GUEST_IF on' 'radio-on transition'
require_literal 'wait_guest_radio_power_off' 'bounded radio-off observation'
require_literal 'wait_guest_radio_power_on' 'bounded radio-on observation'
require_literal 'assert_guest_radio_power_on' 'explicit radio-on assertion'
require_literal 'assert_guest_radio_power_on preflight' 'preflight radio-on assertion'
require_literal 'assert_guest_radio_power_on "cycle-$cycle-pre-off"' 'per-cycle pre-OFF radio-on assertion'
require_literal 'assert_guest_radio_power_on final' 'final radio-on assertion'
require_literal 'radio_off_recovery_pending=1 cycle=%s' 'recovery pending before OFF request'
require_literal 'preflight guest Wi-Fi MAC is not authorized on the pinned AP' 'preflight AP authorization assertion'
require_literal 'pre-OFF guest Wi-Fi MAC is not authorized on the pinned AP' 'per-cycle pre-OFF AP authorization assertion'
require_literal 'station_dump' 'host AP station-dump observer'
require_literal 'sudo -n iw dev "$AP_IF" station dump' 'non-interactive host station observer'
require_literal 'host_station_dump_failed' 'host station-observer error evidence'
require_literal 'wait_authorized_stable' 'AP authorization gate'
require_literal 'wait_fresh_association' 'fresh AP epoch gate'
require_literal 'ping -S $PREEXISTING_LAB_IP -c 5 -W 1000 $LAB_GATEWAY' 'pre-existing-source ping'
require_literal 'refresh_active_client_mac "cycle-$cycle-authorized-$attempt"' 'per-poll dynamic MAC refresh'
require_literal 'saved profile for $LAB_SSID is absent' 'saved-profile preflight'
require_literal 'connection_trigger=saved_profile_autojoin_only' 'autojoin-only evidence label'
require_literal 'ipconfig getpacket' 'read-only DHCP-state capture'
require_literal 'capture_dhcp_sample' 'textual DHCP sample capture'
require_literal 'capture_online_dhcp_samples' 'paired online DHCP sample capture'
require_literal 'sample_kind=ipconfig_getpacket_stdout_text' 'textual sample kind record'
require_literal 'stdout_sha256=' 'textual DHCP sample hash record'
require_literal 'sample_availability=%s' 'DHCP sample-availability record'
require_literal 'sample_text_format=BOOTP_REPLY_TEXT' 'minimal BOOTP textual-format check'
require_literal 'op = BOOTREPLY' 'expected BOOTP reply marker'
require_literal '^yiaddr = [^[:space:]]+' 'expected BOOTP assigned-address marker'
require_literal 'local_evidence_error=' 'local-evidence error record'
require_literal 'status_payload_build_failed' 'status-payload failure containment'
require_literal 'status_write_failed' 'status-write failure containment'
require_literal 'ipconfig_getpacket_network_option_observation_begin' 'textual DHCP network-option observation'
require_literal 'ipconfig_getpacket_transaction_timing_observation_begin' 'textual DHCP transaction/timing observation'
require_literal 'DHCP_OBSERVATION_INCONCLUSIVE=1' 'inconclusive DHCP observation state'
require_literal 'four_cycle_functional_result=PASS' 'separate functional result'
require_literal 'ipconfig_getpacket_stdout_observation_result=COMPLETE' 'complete textual-observation result'
require_literal 'ipconfig_getpacket_stdout_observation_result=INCONCLUSIVE' 'inconclusive textual-observation result'
require_literal 'cycle=%s functional_result=PASS' 'qualified per-cycle functional result'
require_literal 'netstat -rn -f inet' 'full IPv4 route capture'
require_literal 'scutil --nwi' 'network-information capture'
require_literal 'assert_observed_network_invariants' 'persistent network invariant gate'
require_literal 'guest_target_route_signature' 'target route direct signature'
require_literal 'destination=%s nexthop=%s interface=%s' 'target route signature fields'
require_literal 'nexthop=direct' 'direct target route pin'
require_literal 'TARGET_ROUTE_BASELINE=' 'target route baseline'
require_literal '__LAB_ROUTE_BEST_EFFORT_BEGIN__' 'honest best-effort raw route snapshot'
require_literal 'cycle-$cycle-after-auth-before-invariant' 'post-authorization textual DHCP sample phase'
require_literal 'cycle-$cycle-after-ping-before-invariant' 'post-ping textual DHCP sample phase'
require_literal 'final-before-invariant' 'final textual DHCP sample phase'
require_literal 'capture_online_dhcp_samples preflight' 'preflight textual DHCP sample phase'
require_literal 'explicit_route_command=none' 'route-command scope label'
require_literal 'explicit_address_command=none' 'address-command scope label'
require_literal 'explicit_dhcp_state_mutating_command=none' 'DHCP state-mutation scope label'
require_literal 'read_only_dhcp_observation=ipconfig_getpacket' 'read-only DHCP-observation scope label'
require_literal 'os_managed_transient_network_activity_not_claimed=true' 'bounded transient claim'
require_literal 'for cycle in $(seq 1 4); do' 'four-cycle bound'
require_ordered_on_recovery
require_ordered_off_recovery
require_preflight_known_on_authorized
require_cycle_known_on_authorized
require_textual_dhcp_capture_before_functional_gates
require_dhcp_sample_failure_containment
require_no_dhcp_sample_error_suppression
require_exact_dhcp_sample_call_shape
require_dhcp_inconclusive_blocks_unqualified_pass
run_dhcp_sample_fixture_tests

forbid_literal 'JOIN_HELPER' 'arbitrary guest helper'
forbid_literal 'CLIENT_MAC_HINT' 'caller-supplied MAC control'
forbid_literal 'StrictHostKeyChecking=no' 'permissive SSH host-key check'
forbid_literal 'ipconfig set' 'address-client mutation'
forbid_literal 'ipconfig renew' 'DHCP renewal invocation'
forbid_literal 'explicit_dhcp_command=none' 'contradictory DHCP-command scope label'
forbid_literal 'route add' 'route-add invocation'
forbid_literal 'route delete' 'route-delete invocation'
forbid_literal '-setairportnetwork' 'password-bearing association request'
forbid_literal 'airport -s' 'raw airport scan'
forbid_literal 'Apple80211' 'raw driver probe'
forbid_literal 'sudo iw dev' 'interactive host station observer'
forbid_literal 'station get' 'ambiguous single-station probe'
forbid_literal 'guest_target_route_interface' 'interface-only target-route gate'
forbid_literal 'wait_guest_radio_inactive' 'link-state proxy for radio Off'
forbid_literal '[ "${state:-}" = inactive ]' 'inactive link accepted as radio Off'
forbid_literal 'dhcp_configuration_' 'overbroad DHCP-configuration claim'
forbid_literal 'guest_dhcp_config_signature' 'mixed stable and volatile DHCP signature'
forbid_literal 'guest_dhcp_stable_signature' 'unproven stable DHCP classifier'
forbid_literal 'guest_dhcp_transaction_timing_signature' 'separate DHCP timing fetch'
forbid_literal 'observe_dhcp_signatures' 'unbound DHCP signature observer'
forbid_literal 'dhcp_raw_sample' 'misleading raw-DHCP-packet label'
forbid_literal 'raw DHCP' 'misleading raw-DHCP-packet claim'
forbid_literal 'raw packet' 'misleading raw-packet claim'
forbid_literal 'cycle=%s result=PASS' 'unqualified per-cycle result'
forbid_literal 'capture_dhcp_sample "$phase" "$MANAGEMENT_IF" || true' 'suppressed management DHCP sample failure'
forbid_literal 'capture_dhcp_sample "$phase" "$GUEST_IF" || true' 'suppressed Wi-Fi DHCP sample failure'
forbid_literal 'dhcp_stable_signature_' 'DHCP equality-gate claim'
forbid_literal 'management_dhcp_stable_signature_' 'DHCP equality-gate claim'
forbid_literal 'dhcp_selected_signature_' 'ambiguous DHCP signature claim'
forbid_literal 'routing_mutation=none' 'overbroad route-mutation claim'
forbid_literal 'ip_address_mutation=none' 'overbroad address-mutation claim'
forbid_literal '10.90.10.22' 'external validation host reference'

printf 'PASS: pinned observed-invariants radio-gate static and DHCP-fixture contract\n'
