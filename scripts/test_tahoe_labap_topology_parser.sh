#!/usr/bin/env bash
# Fixture contract for the fail-closed LabAP topology parser.  It prints only
# aggregate counts; fixture MAC values are synthetic and never leave this test.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PARSER="$ROOT/scripts/tahoe_labap_topology_parser.awk"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

summary() {
    awk '
        $2 == 2452 || $2 == 2472 { band24[$1] = 1; all[$1] = 1 }
        $2 == 5745 || $2 == 5765 || $2 == 5885 { band5[$1] = 1; all[$1] = 1 }
        END {
            for (mac in all) ++total
            for (mac in band24) ++count24
            for (mac in band5) ++count5
            for (mac in band24) if (mac in band5) ++dual
            printf "%d/%d/%d/%d\n", total + 0, count24 + 0, count5 + 0, dual + 0
        }
    '
}

assert_fixture() {
    local name="$1" expected="$2" fixture="$3" actual
    actual="$(printf '%s\n' "$fixture" |
        awk -v target_ssid='LabAP' \
            -v freq24_ch9=2452 -v freq24_ch13=2472 \
            -v freq5_ch149=5745 -v freq5_ch153=5765 -v freq5_ch177=5885 \
            -f "$PARSER" |
        summary)"
    [ "$actual" = "$expected" ] ||
        fail "$name expected=$expected actual=$actual"
}

test -f "$PARSER" || fail "parser missing"

assert_fixture valid_pair '2/1/1/0' '
BSS 02:00:00:00:00:24(on sta0)
        freq: 2452
        SSID: LabAP
        BSS Load:
BSS 02:00:00:00:00:50(on sta0)
        freq: 5765
        SSID: LabAP'

# Every audited alternate channel remains admitted, but no same-band result
# can satisfy the switcher's later cross-band pair requirement by itself.
assert_fixture alternate_allowlist_channels '5/2/3/0' '
BSS 02:00:00:00:00:09(on sta0)
        freq: 2452
        SSID: LabAP
BSS 02:00:00:00:00:13(on sta0)
        freq: 2472
        SSID: LabAP
BSS 02:00:00:00:00:49(on sta0)
        freq: 5745
        SSID: LabAP
BSS 02:00:00:00:00:53(on sta0)
        freq: 5765
        SSID: LabAP
BSS 02:00:00:00:00:77(on sta0)
        freq: 5885
        SSID: LabAP'

assert_fixture one_bss '1/1/0/0' '
BSS 02:00:00:00:00:24(on sta0)
        freq: 2452
        SSID: LabAP'

assert_fixture same_band '2/2/0/0' '
BSS 02:00:00:00:00:24(on sta0)
        freq: 2452
        SSID: LabAP
BSS 02:00:00:00:00:25(on sta0)
        freq: 2452
        SSID: LabAP'

assert_fixture wrong_frequency '0/0/0/0' '
BSS 02:00:00:00:00:64(on sta0)
        freq: 5180
        SSID: LabAP'

# The audited sets are exact channels, not a broad 2.4/5 GHz admission rule.
assert_fixture unlisted_in_band_channels '0/0/0/0' '
BSS 02:00:00:00:00:11(on sta0)
        freq: 2462
        SSID: LabAP
BSS 02:00:00:00:00:61(on sta0)
        freq: 5805
        SSID: LabAP'

# A malformed top-level BSS header must clear, not inherit, the preceding MAC.
assert_fixture malformed_header_carryover '2/2/0/0' '
BSS 02:00:00:00:00:24(on sta0)
        freq: 2452
        SSID: LabAP
BSS not-a-mac(on sta0)
        freq: 5765
        SSID: LabAP
BSS 02:00:00:00:00:25(on sta0)
        freq: 2452
        SSID: LabAP'

# An indented BSS-looking record is not a new scan header and must instead
# invalidate the preceding context so its fields cannot fabricate a 5 GHz BSS.
assert_fixture indented_fake_header '1/1/0/0' '
BSS 02:00:00:00:00:24(on sta0)
        freq: 2452
        SSID: LabAP
        BSS 02:00:00:00:00:50(on sta0)
        freq: 5765
        SSID: LabAP'

assert_fixture ssid_suffix_rejected '0/0/0/0' '
BSS 02:00:00:00:00:24(on sta0)
        freq: 2452
        SSID: LabAP suffix
BSS 02:00:00:00:00:50(on sta0)
        freq: 5765
        SSID: LabAP suffix'

# Canonicalisation makes case-only spellings one identity; the switcher then
# rejects such a dual-band identity instead of treating it as two radios.
assert_fixture mixed_case_same_mac '1/1/1/1' '
BSS 02:00:00:00:00:aa(on sta0)
        freq: 2452
        SSID: LabAP
BSS 02:00:00:00:00:AA(on sta0)
        freq: 5765
        SSID: LabAP'

printf 'PASS: Tahoe LabAP topology parser fixtures\n'
