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
