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
