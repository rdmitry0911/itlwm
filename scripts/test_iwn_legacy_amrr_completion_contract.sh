#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
iwn_h="$root/itlwm/hal_iwn/if_iwnvar.h"
iwn_cpp="$root/itlwm/hal_iwn/ItlIwn.cpp"

require() {
    local file=$1
    local text=$2
    grep -Fq "$text" "$file" || {
        printf 'missing IWN legacy AMRR completion contract: %s\n' "$text" >&2
        exit 1
    }
}

require "$iwn_h" 'int txrate;'
require "$iwn_cpp" 'data->txrate = ni->ni_txrate;'
require "$iwn_cpp" 'if (data->txrate != data->ni->ni_txrate) {'
require "$iwn_cpp" 'data->txrate = 0;'

completion=$(awk '
    /^iwn_tx_done\(struct iwn_softc/ { inside = 1 }
    inside { print }
    inside && /^}/ { exit }
' "$iwn_cpp")

if printf '%s\n' "$completion" | grep -Fq \
    'if (rate != data->ni->ni_txrate) {'; then
    printf 'firmware PLCP is still compared with the ni_rates index\n' >&2
    exit 1
fi

for required in \
    'wn->amn.amn_txcnt++;' \
    'wn->amn.amn_retrycnt++;' \
    'iwn_set_link_quality(sc, data->ni);'; do
    printf '%s\n' "$completion" | grep -Fq "$required" || {
        printf 'legacy AMRR completion lost step: %s\n' "$required" >&2
        exit 1
    }
done

printf 'iwn legacy AMRR completion contract: PASS\n'
