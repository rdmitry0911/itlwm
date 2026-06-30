#!/bin/sh
set -eu

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/itlwm-payload-builders.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

cxx=${CXX:-c++}
"$cxx" \
  -std=c++17 \
  -Wall \
  -Wextra \
  -Werror \
  -DTAHOE_PAYLOAD_BUILDERS_STANDALONE_TEST \
  -I. \
  tests/tahoe_payload_builders_test.cpp \
  -o "$tmpdir/tahoe_payload_builders_test"

"$tmpdir/tahoe_payload_builders_test"
