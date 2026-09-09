#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d)
trap 'test ! -f "$test_root/test" || unlink "$test_root/test"; rmdir "$test_root"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
    -I "$root" "$root/tests/iwn_scan_dwell_budget_test.cpp" -o "$test_root/test"
"$test_root/test"
