#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d)
cleanup() {
    test ! -f "$test_root/test" || unlink "$test_root/test"
    test ! -f "$test_root/header.hpp" || unlink "$test_root/header.hpp"
    rmdir "$test_root"
}
trap cleanup EXIT
extra=()
if [ -n "${LQM_HEADER_REF:-}" ]; then
    git -C "$root" show "${LQM_HEADER_REF}:AirportItlwm/TahoeLqmContracts.hpp" >"$test_root/header.hpp"
    extra=(-include "$test_root/header.hpp")
fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$root" ${extra[@]+"${extra[@]}"} "$root/tests/tahoe_lqm_cca_validity_test.cpp" \
    -o "$test_root/test"
"$test_root/test"
