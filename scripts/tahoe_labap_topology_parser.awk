# Consume only the current host `iw` scan text shape and emit internal,
# canonical `bssid frequency` records for exact LabAP beacons on the audited
# fixed allow-list: 2.4 GHz channels 9/13 and 5 GHz channels 149/153/177.
# The switcher captures this output in memory; it must never be sent to logs
# or user-facing stdout.
#
# Required -v arguments:
#   target_ssid, freq24_ch9, freq24_ch13, freq5_ch149, freq5_ch153,
#   freq5_ch177

function reset_bss() {
    bssid = ""
    frequency = 0
}

# A BSS header is top-level only.  Reset first even for malformed headers, so
# following fields cannot inherit identity from an earlier valid BSS.
/^BSS[[:space:]]/ {
    reset_bss()
    candidate = $2
    sub(/\(.*/, "", candidate)
    if (candidate ~ /^[[:xdigit:]][[:xdigit:]](:[[:xdigit:]][[:xdigit:]]){5}$/)
        bssid = tolower(candidate)
    next
}

# Nested BSS-looking records are not a scan BSS header.  Clear the current
# context rather than allowing malformed indentation to fabricate a second
# frequency/SSID for the preceding BSS.
/^[[:space:]]+BSS[[:space:]]/ {
    reset_bss()
    next
}

/^[[:space:]]*freq:[[:space:]]/ && bssid != "" {
    value = $0
    sub(/^[[:space:]]*freq:[[:space:]]*/, "", value)
    if (value ~ /^[0-9]+(\.[0-9]+)?$/)
        frequency = int(value)
    else
        frequency = 0
    next
}

/^[[:space:]]*SSID:[[:space:]]/ && bssid != "" {
    value = $0
    sub(/^[[:space:]]*SSID:[[:space:]]*/, "", value)
    if (value == target_ssid &&
        (frequency == freq24_ch9 || frequency == freq24_ch13 ||
         frequency == freq5_ch149 || frequency == freq5_ch153 ||
         frequency == freq5_ch177))
        print bssid, frequency
}
