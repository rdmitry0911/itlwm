#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
QUERY_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
QUERY_TEST="$(mktemp -d)"
trap 'rm -f "$QUERY_TEST/test"; rmdir "$QUERY_TEST"' EXIT
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$QUERY_ROOT" "$QUERY_ROOT/tests/sta_sa_query_test.cpp" \
    -o "$QUERY_TEST/test"
"$QUERY_TEST/test"
