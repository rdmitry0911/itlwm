#!/usr/bin/env bash
# Identity-free LabAP passive visibility observation.
#
# This program never starts, stops, reconfigures, or signals hostapd; it never
# joins, creates a profile, touches a guest, or writes a state file.  It runs
# only the same bounded `iw scan flush passive` observation used by the AP
# switcher's admission gate and emits aggregate counts/booleans.  `flush`
# necessarily refreshes the host scan cache and may refresh LAR observations,
# so this is read-only with respect to AP/profile/network configuration, not a
# claim of zero transient driver state.
set -euo pipefail
umask 077

SELF_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PARSER="$SELF_DIR/tahoe_labap_topology_parser.awk"
IW="/usr/sbin/iw"
SUDO="/usr/bin/sudo"
STA_IF="sta0"
TEST_SSID="LabAP"
# The observer uses the same audited fixed allow-list as the switcher:
# 2.4 GHz channels 9/13 and 5 GHz channels 149/153/177.
LABAP_FREQ_24_CH9=2452
LABAP_FREQ_24_CH13=2472
LABAP_FREQ_5_CH149=5745
LABAP_FREQ_5_CH153=5765
LABAP_FREQ_5_CH177=5885
EXPECTED_IW_VERSION="iw version 6.7"
SAMPLES=3

usage() {
    printf 'usage: %s [--samples 3..5]\n' "${0##*/}" >&2
    exit 2
}

fail() {
    printf 'LABAP_PASSIVE_OBSERVE_FAIL:%s\n' "$*" >&2
    exit 1
}

sudo_cmd() {
    "$SUDO" -n "$@"
}

nonempty_line_count() {
    awk 'NF { count++ } END { print count + 0 }'
}

pair_intersects() {
    local left="$1" right="$2" pair
    while IFS= read -r pair; do
        [ -n "$pair" ] || continue
        grep -Fxq -- "$pair" <<<"$right" && return 0
    done <<<"$left"
    return 1
}

pair_intersection() {
    local left="$1" right="$2" pair
    while IFS= read -r pair; do
        [ -n "$pair" ] || continue
        grep -Fxq -- "$pair" <<<"$right" && printf '%s\n' "$pair"
    done <<<"$left"
}

scan_once() {
    local scan_text records bssid frequency extra bssid24 bssid5
    local elapsed=0
    declare -A seen_all=()
    declare -A seen24=()
    declare -A seen5=()

    OBS_IW_RC=0
    OBS_DURATION_SECONDS=0
    OBS_BSS_COUNT=0
    OBS_BAND_COUNT=0
    OBS_PAIRS=""
    SECONDS=0
    if scan_text="$(sudo_cmd "$IW" dev "$STA_IF" scan flush passive 2>/dev/null)"; then
        OBS_IW_RC=0
    else
        OBS_IW_RC=$?
        scan_text=""
    fi
    elapsed=$SECONDS
    OBS_DURATION_SECONDS="$elapsed"

    [ "$OBS_IW_RC" -eq 0 ] || return 0
    records="$(printf '%s\n' "$scan_text" | awk \
        -v target_ssid="$TEST_SSID" \
        -v freq24_ch9="$LABAP_FREQ_24_CH9" \
        -v freq24_ch13="$LABAP_FREQ_24_CH13" \
        -v freq5_ch149="$LABAP_FREQ_5_CH149" \
        -v freq5_ch153="$LABAP_FREQ_5_CH153" \
        -v freq5_ch177="$LABAP_FREQ_5_CH177" \
        -f "$PARSER")"

    while IFS=' ' read -r bssid frequency extra; do
        [ -n "$bssid" ] && [ -n "$frequency" ] && [ -z "${extra:-}" ] || continue
        case "$frequency" in
            "$LABAP_FREQ_24_CH9"|"$LABAP_FREQ_24_CH13") seen_all["$bssid"]=1; seen24["$bssid"]=1;;
            "$LABAP_FREQ_5_CH149"|"$LABAP_FREQ_5_CH153"|"$LABAP_FREQ_5_CH177") seen_all["$bssid"]=1; seen5["$bssid"]=1;;
            *) continue;;
        esac
    done <<<"$records"

    OBS_BSS_COUNT="${#seen_all[@]}"
    [ "${#seen24[@]}" -eq 0 ] || OBS_BAND_COUNT=$((OBS_BAND_COUNT + 1))
    [ "${#seen5[@]}" -eq 0 ] || OBS_BAND_COUNT=$((OBS_BAND_COUNT + 1))
    for bssid24 in "${!seen24[@]}"; do
        for bssid5 in "${!seen5[@]}"; do
            [ "$bssid24" = "$bssid5" ] && continue
            OBS_PAIRS+="${bssid24}|${bssid5}"$'\n'
        done
    done
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --samples)
            [ "$#" -ge 2 ] || usage
            SAMPLES="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            usage
            ;;
    esac
done

case "$SAMPLES" in ''|*[!0-9]*) usage;; esac
[ "$SAMPLES" -ge 3 ] && [ "$SAMPLES" -le 5 ] || usage
[ -f "$PARSER" ] && [ ! -L "$PARSER" ] || fail "topology parser is missing or symlinked"
[ -x "$IW" ] && [ -x "$SUDO" ] || fail "pinned iw or sudo binary is unavailable"
[ "$("$IW" --version 2>/dev/null)" = "$EXPECTED_IW_VERSION" ] ||
    fail "iw version does not match the reviewed passive-scan syntax"
sudo_cmd true || fail "noninteractive sudo is unavailable"

previous_pairs=""
stable_pairs=""
all_iw_success=1
for sample in $(seq 1 "$SAMPLES"); do
    scan_once
    if [ "$OBS_IW_RC" -ne 0 ]; then
        all_iw_success=0
    fi

    if [ "$sample" -eq 1 ]; then
        intersects_previous="na"
        stable_pairs="$OBS_PAIRS"
    elif pair_intersects "$previous_pairs" "$OBS_PAIRS"; then
        intersects_previous="yes"
        stable_pairs="$(pair_intersection "$stable_pairs" "$OBS_PAIRS")"
    else
        intersects_previous="no"
        stable_pairs=""
    fi

    printf 'LABAP_PASSIVE_OBSERVE sample=%s iw_rc=%s duration_seconds=%s external_bss_count=%s external_band_count=%s cross_band_pair_count=%s intersects_previous=%s\n' \
        "$sample" "$OBS_IW_RC" "$OBS_DURATION_SECONDS" "$OBS_BSS_COUNT" \
        "$OBS_BAND_COUNT" "$(nonempty_line_count <<<"$OBS_PAIRS")" "$intersects_previous"
    previous_pairs="$OBS_PAIRS"
done

if [ "$all_iw_success" -eq 1 ] && [ -n "$stable_pairs" ]; then
    stable_pair_across_all="yes"
else
    stable_pair_across_all="no"
fi
printf 'LABAP_PASSIVE_OBSERVE_END samples=%s all_iw_success=%s stable_pair_across_all=%s\n' \
    "$SAMPLES" "$([ "$all_iw_success" -eq 1 ] && printf yes || printf no)" \
    "$stable_pair_across_all"
