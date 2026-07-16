#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/itlwm-iwn-ht40.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

cxx=${CXX:-c++}
"$cxx" \
  -std=c++17 \
  -Wall \
  -Wextra \
  -Werror \
  -I"$root" \
  "$root/tests/iwn_ht40_contract_test.cpp" \
  -o "$tmpdir/iwn_ht40_contract_test"
"$tmpdir/iwn_ht40_contract_test"

source="$root/itlwm/hal_iwn/ItlIwn.cpp"

require_source() {
  if ! grep -F -q -- "$1" "$source"; then
    echo "missing IWN HT40 contract: $1" >&2
    exit 1
  fi
}

require_source '#include "IwnHt40Contracts.hpp"'
require_source 'IwnHt40Contracts::kPrimarySecondaryChannelDelta'
require_source 'ic->ic_channels[chan].ic_flags |= IEEE80211_CHAN_HT40U;'
require_source 'ic->ic_channels[upper].ic_flags |= IEEE80211_CHAN_HT40D;'
require_source 'IEEE80211_CHAN_HT20;'
require_source 'IwnHt40Contracts::nvmPowerChannel('
require_source 'maxchpwr = sc->maxpwr40[ht40chan] * 2;'

if sed -n '/^iwn_read_eeprom_channels/,/^}$/p' "$source" | \
  grep -F -q 'IEEE80211_CHAN_HT;'; then
  echo 'generic HT40 capability leaked into EEPROM channel materialization' >&2
  exit 1
fi

awk '
/iwn_rxon_configure_ht40\(struct ieee80211com/ { inside = 1 }
inside && /sc->rxon.flags &= ~htole32/ { clear = NR }
inside && /if \(ni == NULL\)/ { nullGuard = NR }
inside && /ni->ni_htop/ && firstDeref == 0 { firstDeref = NR }
inside && /if \(iwn_ht40_pair_permitted\(ni, sco\)\)/ { gate = NR }
inside && /^}/ { exit }
END {
  if (!clear || !nullGuard || !firstDeref || !gate ||
      !(clear < nullGuard && nullGuard < firstDeref && firstDeref < gate))
    exit 1
}
' "$source" || {
  echo 'RXON HT40 clear/null/direction ordering regressed' >&2
  exit 1
}
