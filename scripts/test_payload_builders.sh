#!/bin/sh
set -eu

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/itlwm-payload-builders.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

cxx=${CXX:-c++}
cc=${CC:-cc}
compat_flags=
case "$(uname -s)" in
  Darwin) ;;
  *) compat_flags="-Itests/compat" ;;
esac

"$cxx" \
  -std=c++17 \
  -Wall \
  -Wextra \
  -Werror \
  -DTAHOE_PAYLOAD_BUILDERS_STANDALONE_TEST \
  -DITLWM_STANDALONE_REAL_APPLE80211_IOCTL \
  -D__IO80211_TARGET=260000 \
  $compat_flags \
  -I. \
  -Iinclude \
  tests/tahoe_payload_builders_test.cpp \
  -o "$tmpdir/tahoe_payload_builders_test"

"$tmpdir/tahoe_payload_builders_test"

"$cc" \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I. \
  -Iinclude \
  tests/iwx_pmf_bip_trace_contract_test.c \
  -o "$tmpdir/iwx_pmf_bip_trace_contract_test"

"$tmpdir/iwx_pmf_bip_trace_contract_test"

"$cc" \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I. \
  -Iinclude \
  tests/iwn_software_pmf_trace_contract_test.c \
  -o "$tmpdir/iwn_software_pmf_trace_contract_test"

"$tmpdir/iwn_software_pmf_trace_contract_test"

"$cc" \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I. \
  -Iinclude \
  tests/iwn_pmf_ingress_trace_contract_test.c \
  -o "$tmpdir/iwn_pmf_ingress_trace_contract_test"

"$tmpdir/iwn_pmf_ingress_trace_contract_test"

"$cc" \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I. \
  -Iinclude \
  tests/iwn_direct_sae_trace_contract_test.c \
  -o "$tmpdir/iwn_direct_sae_trace_contract_test"

"$tmpdir/iwn_direct_sae_trace_contract_test"

"$cc" \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -I. \
  -Iinclude \
  tests/wcl_physical_scan_trace_contract_test.c \
  -o "$tmpdir/wcl_physical_scan_trace_contract_test"

"$tmpdir/wcl_physical_scan_trace_contract_test"

"$(dirname "$0")/test_tahoe_wcl_auth_assoc_completion_contract.sh"
"$(dirname "$0")/test_tahoe_wcl_exact_scan_plan_contract.sh"
bash "$(dirname "$0")/test_scan_rssi_publication.sh"
bash "$(dirname "$0")/test_tahoe_lqm_cca_validity.sh"
bash "$(dirname "$0")/test_apsta_reentrant_start.sh"
bash "$(dirname "$0")/test_iwn_ap_stop_tx_retirement.sh"
bash "$(dirname "$0")/test_iwn_ap_raw_tx_owner.sh"
bash "$(dirname "$0")/test_iwn_tx_queue_topology.sh"
bash "$(dirname "$0")/test_apsta_async_tx_dequeue.sh"
bash "$(dirname "$0")/test_iwn_sta_aggregate_stop.sh"
bash "$(dirname "$0")/test_pmf_leave_tx_key.sh"
bash "$(dirname "$0")/test_net80211_mgmt_queue_ownership.sh"
bash "$(dirname "$0")/test_net80211_comeback_deadline.sh"
bash "$(dirname "$0")/test_net80211_roam_carrier.sh"
bash "$(dirname "$0")/test_wcl_reassoc_failure_retirement.sh"
BSS_SWITCH_PASSING_ONLY=1 bash "$(dirname "$0")/test_reassoc_deferred_bss.sh"
bash "$(dirname "$0")/test_reassoc_tx_retirement.sh"
bash "$(dirname "$0")/test_iwn_scan_abort_owner.sh"
bash "$(dirname "$0")/test_net80211_join_attempt.sh"
bash "$(dirname "$0")/test_iwn_sae_join_failure.sh"
bash "$(dirname "$0")/test_iwn_auth_beacon.sh"
for auth_case in roam cold incoming retry command-failure; do
    bash "$(dirname "$0")/test_iwn_auth_nonblocking.sh" "$auth_case"
done
bash "$(dirname "$0")/test_scan_command_lease.sh"
bash "$(dirname "$0")/test_scan_command_policy.sh"
bash "$(dirname "$0")/test_state_transition_request.sh"
bash "$(dirname "$0")/test_sta_command_completion.sh"
bash "$(dirname "$0")/test_iwx_tvqm_allocation.sh"
bash "$(dirname "$0")/test_primary_rx_ba_teardown.sh"
bash "$(dirname "$0")/test_firmware_context_owner.sh"
