# GUI WPA3 discovery after a previously connected BSSID changes — 2026-09-12

Status: production correction and source tests ready; new-binary runtime
qualification and publication pending. This is a concrete GUI-matrix P0
dependency, not an independently selected static surface item.

## Live failure on the published binary

Guest boot `702D27E1-61A2-415C-ACFC-EFB9DC9DCF50`, loaded Mach-O UUID
`D636A28B-6A9B-3CCE-AF28-5779C5F980C8`, production source c123d132.
Using only GUI selections, join AIAM-GUI-WPA2-0912 on
80:e4:ba:20:ef:f9/channel9, return to LabAP (WPA3/SAE, different BSSID),
then reconfigure that same controlled host BSSID to AIAM-GUI-WPA3-0912,
SAE group19 and PMF required. The new AP is ready at 04:47:15 UTC. Fresh
scans complete, but the GUI still shows the old WPA2 name and no new WPA3.

Read-only, 30-second DTrace at 04:55:25–04:55:56 captured six new WPA3
advertisements with zero DTrace errors. Every observation shows RUN state,
the old cached SSID, node state BSS (1), and an already-updated raw SSID TLV
containing the new WPA3 name. The unchanged production SSID refresh helper
leaves the old name in place. Offsets were resolved with LLDB against the
exact installed D636 UUID and its debug-map objects, not guessed. The trace
reads only the exact public fixture SSID/state, not credentials or packets.

Evidence root: `/dev/shm/aiam-gui-awake-baseline-20260912.ol9Hax`.
Key files: `gui-wpa3-before-selection-q1.png`,
`gui-wpa3-before-selection-q2.png`, `gui-scan-identity-d636-q1.log`,
`inspect-d636-scan-layout-q2.log`, and the fixture/AP and airportd logs.
Trace SHA-256:
`5c4144c647152805a0e481479f6133aad7fa143395689a3127b17f224bc230d3`.

## Cause and correction

The persistent `ic_bss` is a copy, distinct from the selected scan-tree node.
The previous tree entry needs to become CACHE after replacement by another
BSSID. The legacy AUTH branch calls `ieee80211_clean_sta_bss_node`, but the
driver-resident SAE auth-hold early return is above that cleanup. WPA2→SAE
therefore leaves the previous BSSID marked BSS. The protected-SSID guard
correctly refuses to mutate a BSS-owned identity even as other beacon IEs
refresh, exposing the stale ownership as an incoherent scan result.

The common `ieee80211_node_join_bss` path now calls the existing cleanup
immediately after the committed selected-BSS copy, in STA mode only. That
helper demotes BSS cache records whose BSSID differs from the newly selected
BSS. It preserves the current BSSID and AUTH/ASSOC/COLLECT peer states.
The beacon-side SSID guard is unchanged. No HAL-specific capability or
security-policy changes are made; this common correction also covers iwm/iwx
source paths, without claiming runtime coverage on untested hardware.

Reference checked: the complete 240-line
`ffffff80016afe2e___ZN23AppleBCMWLANScanAdapter18processScanResultsEPvmjyj.c`
in `aiam-passive-roam-discovery-runtime.frmZ3s/reference-q1`. It assembles
fresh validated firmware BSS/IE data into a frame and publishes 0xc9. This
supports coherent fresh scan identity/IEs, not Intel-specific node semantics
or a claim about every downstream reference cache.

## Tests and remaining runtime gate

The production-function TX teardown fixture now also compiles the complete
cleanup and SSID-refresh functions. Its 48 combinations cover STA/non-STA,
SCAN/AUTH/RUN, admitted/rejected selection, same/different BSSID, and binding
rejection after copy. It checks TX teardown before copy, cleanup after copy,
former-BSS rename and active/peer protection. On pre-fix source d6bce4f9 the
compiled test fails its event-order assertion (exit134); on changed source
it passes with ASan/UBSan. The earlier negative-q1 compile failure is not
used as regression evidence; negative-q2 is the actual executable red case.

Also passing: scan SSID refresh, initial BSSID pin, cached candidate join,
PAE epoch contract, IWN WNM BTM contract, 25 WCL reassoc retirement cases,
and 145 roam-carrier cases including 30 post-target cancellation edges.
The adjacent carrier fixture has no scan tree; it uses a documented boundary
adapter for cleanup while the dedicated test executes the actual function.

Next: commit/push; exact-manifest guest build; private AuxKC admission;
transactional install and exact UUID boot readback. A fresh boot clears the
cache, so seeing WPA3 immediately is insufficient. Repeat GUI WPA2 join →
GUI return to SAE LabAP → same BSSID changes SSID/security → fresh GUI WPA3
discovery → GUI password/join → DHCP and bidirectional traffic. Retain first
results and add open/WPA2 regressions. Publish only after qualification.
