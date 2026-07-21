#!/bin/sh
set -eu

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/itlwm-payload-builders.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

cxx=${CXX:-c++}
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
