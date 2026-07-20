# Tahoe lab AP visibility readiness — 2026-07-20

## Recorded successful scenario

At `2026-07-20T20:40:12Z` through `2026-07-20T20:40:22Z`, the Tahoe 25C56
laboratory guest completed the sanitized CoreWLAN directed-scan readiness
scenario recorded in
`evidence/runtime/tahoe_lab_ap_visibility_readiness.json`.

- The guest Wi-Fi interface was present and an AirportItlwm kext was loaded.
- The operator-selected AP was visible through CoreWLAN: one directed result
  on channel 153 at -44 dBm; nine total scan results were observed.
- The versioned JSON contains only the operator environment's SHA-256 AP
  identifier (`operator-env-ssid-sha256:552cb0ce6725ac3c`), redacted BSSID
  fields, and no credential material.  Its SHA-256 is
  `0c3b3e736828cc81cc7923744225377403ea8794c27da3600a18d3d8dc239f52`.

## Exact boundary

This is a successful **scan readiness** record, not a successful association
test.  The evidence explicitly records that the loaded kext was not bound to
this checkout and that no candidate functional verdict was tested.  It does
not claim authentication response acknowledgement, association, DHCP, data
transfer, reconnect, final equivalence, or a pure-SAE/PMF implementation.

The capture made no install, load, reboot, route, or address change.  It is
kept as a committed, sanitized precondition for a later four-epoch SAE/PMF
runtime batch once the exact release kext is staged on the lab guest.
