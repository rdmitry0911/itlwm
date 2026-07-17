#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"

awk '
/setOS_FEATURE_FLAGS\(apple80211_feature_flags \*data\)/ { inside = 1 }
inside && /setDHCP_RENEWAL_DATA\(apple80211_dhcp_renewal_data \*data\)/ { exit }
inside {
    if (/if \(!data\)/)
        null_guard = NR
    if (/const uint64_t flags =/)
        flags = NR
    if (/constexpr uint64_t kSlowWifiFeatureFlag = 1ULL << 2;/)
        bit = NR
    if (/if \(\(flags & kSlowWifiFeatureFlag\) != 0\)/)
        guard = NR
    if (guard && /return kIOReturnUnsupported;/ && !unsupported)
        unsupported = NR
    if (/cachedOSFeatureFlags = flags;/) {
        cache = NR
        cache_count++
    }
}
END {
    if (!inside || !null_guard || !flags || !bit || !guard || !unsupported ||
        !cache || cache_count != 1 ||
        !(null_guard < flags && flags < bit && bit < guard &&
          guard < unsupported && unsupported < cache))
        exit 1
}
' "$source" || {
    echo 'slow-WiFi OS feature flag quarantine contract regressed' >&2
    exit 1
}

section=$(sed -n '/setOS_FEATURE_FLAGS(apple80211_feature_flags \*data)/,/setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data \*data)/p' "$source")
if printf '%s\n' "$section" | grep -F -q 'isSlowWifiFeatureEnabled()'; then
    echo 'OS feature flags still map bit 2 into the slow-WiFi getter' >&2
    exit 1
fi
