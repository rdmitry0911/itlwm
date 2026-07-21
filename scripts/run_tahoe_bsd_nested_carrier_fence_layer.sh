#!/bin/sh
# Run the Tahoe BSD nested-carrier static fence layer in one bounded pass.
#
# This runner intentionally executes source-only contracts.  It neither opens
# an Apple80211 BSD ioctl nor changes radio, association, address, route, or
# profile state.  The contracts cover the ingress boundary where the BSD
# callback has marshalled only apple80211req, not a nested req_data carrier.
set -eu

case "${1-}" in
    ""|--static-only)
        ;;
    *)
        echo "usage: $0 [--static-only]" >&2
        exit 2
        ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
contracts='
test_tahoe_scan_result_bsd_carrier_quarantine.sh
test_tahoe_supported_channels_bsd_carrier_fence.sh
test_tahoe_rsn_ie_bsd_carrier_fence.sh
test_tahoe_ap_ie_list_bsd_carrier_fence.sh
test_tahoe_link_changed_event_bsd_carrier_fence.sh
test_tahoe_association_status_bsd_carrier_fence.sh
test_tahoe_max_nss_for_ap_bsd_carrier_fence.sh
test_tahoe_nss_bsd_carrier_fence.sh
test_tahoe_tcpka_get_bsd_carrier_fence.sh
test_tahoe_mcs_get_bsd_carrier_fence.sh
test_tahoe_mcs_index_get_bsd_carrier_fence.sh
test_tahoe_rate_set_get_bsd_carrier_fence.sh
test_tahoe_vht_capability_bsd_carrier_fence.sh
test_tahoe_bss_blacklist_bsd_set_fence.sh
test_tahoe_ranging_set_bsd_carrier_fence.sh
test_tahoe_btcoex_profile_bsd_set_carrier_fence.sh
test_tahoe_no_backend_set_bsd_carrier_fence.sh
'

count=0
for contract in $contracts; do
    "$root/scripts/$contract"
    count=$((count + 1))
done

printf 'Tahoe BSD nested-carrier fence layer: PASS (%s static contracts)\n' \
    "$count"
