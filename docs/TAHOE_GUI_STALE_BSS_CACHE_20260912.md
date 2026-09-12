# GUI WPA3 discovery after a previously connected BSSID changes — 2026-09-12

Status: production correction committed/pushed as 5e98d640; exact-new-image
GUI runtime qualification PASS, alpha published/readback verified. This is a GUI-matrix P0
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

The original runtime gate required the complete precondition after the new
boot: GUI WPA2 join → GUI return to SAE LabAP → same BSSID changes SSID/security
→ GUI WPA3 discovery/password/join → DHCP and bidirectional traffic. A fresh
boot alone clears the cache and is not a passing reproduction.

## Exact new-image runtime result, 05:16–05:35 UTC

Build, private AuxKC admission and transactional activation passed. The other
four auxiliary members were preserved. A graceful owned-guest reboot loaded
UUID `AF169180-05E8-33CF-960A-166E5DB901BB`, boot
`870616E6-4A67-4106-947C-F20DEDD08A2B`. All357 production hashes verify;
manifest SHA256 `5dbe5aae76fa07ade6a22dd9c4c23e7df39b53850cc745e11f9d75d1c0280b3e`,
embedded source-id `5dbe5aae76fa`. All1088 undefined symbols resolve.

At05:20:30, the actual GUI selects saved WPA2. DHCP .27 and20/20 packets
each direction pass. At05:21:33, GUI selection returns to SAE LabAP on
9a:fb:5d:97:a9:02/ch13 with DHCP .219; source-bound router traffic is20/20.
Only afterward is the WPA2 fixture stopped. The same host BSSID begins
advertising AIAM-GUI-WPA3-0912 at05:24:22. The next screenshot shows its
correct new name and no stale WPA2 name, without a radio toggle/API join.

The exact AF16 layout was revalidated with LLDB. A60-second read-only trace
records12 advertisements with node_state0/CACHE, correct cached SSID and raw
TLV identity, zero errors. It observes the post-retirement state, not the
first old-name-to-new-name mutation or uninterrupted node allocation.
Trace SHA256 `4269e359d4865181fcfb4fcfff9b6ce7be5f5564c4e1e9787ddfae17897adc23`.

At05:24:56 GUI Connect opens the standard WPA3 Personal password dialog;
password entry05:25:37 gives DHCP ACK05:25:46, address192.168.73.33.
The external AP reports AUTH/ASSOC/AUTHORIZED/MFP, AKM00-0f-ac-8,
SAE group19 and CCMP. The first independent traffic pair passes20/20 each
direction (1400-byte payload), with no DTrace still active during traffic.

Further same-image actual-GUI controls pass:

- WPA3→LabAP→saved WPA3, no repeated password dialog,20/20 each direction.
- Explicit Wi-Fi off05:28:06: inactive/noIPv4; on05:28:35 automatically
  restores the same WPA3 profile, DHCP and20/20 each direction, AP MFP set.
- Same BSSID changes from WPA3 to open: correct unprotected GUI entry,
  selection05:31:51, DHCP .34 and20/20 each direction.
- GUI return to LabAP05:33:01 precedes open-fixture stop. Router traffic20/20;
  final peer traffic after normal host profile restoration20/20 both ways.

Initial automatic LabAP on AF16 also passes20/20 each direction. This does
not erase the retained D636 first-attempt losses or establish all roaming
policy/sleep combinations. All six old/new fixtures and all observers are
terminal; host management/profile restored; physical .22 not accessed.

Durable evidence: `/home/dima/Projects/itlwm/aiam-gui-cache-runtime-20260912.gHMjC3`.
All251 entries verify; `EVIDENCE.sha256` SHA256:
`85114d00ecc731280dea44e9216e2c4f77814bafec15fc8aedc8dd5d2ff5b129`.
The transient hostapd control socket directory is excluded; logs are included.

Installed/build/extracted bundles compare identically; no packaging rebuild.
Mach-O SHA256 `ab06be973d3ae72aa6544b28e834285d2a6e1e02e8b8633c8fb78f14d2b1bf2b`.
ZIP15695095bytes, SHA256
`b0473b4633b46c13f0e271ce1757a5f6431c5302bd51c8957aecf9837dd5048d`.

## Remaining GUI work

Publication completed05:41:52 UTC in the existing `v2.4.0-alpha` release.
Title: AirportItlwm Tahoe v2.4.0-alpha (5e98d640); asset ID558697358,
15695095bytes, ZIP SHA256 as recorded above. The downloaded public asset
compares byte-for-byte with the frozen installed/tested archive. The previous
c123d132 asset was preserved before replacement. Publication receipts:
`/home/dima/Projects/itlwm/aiam-gui-cache-release-20260912.IIURgq`;
publisher log SHA256
`33a3b71713597375e1d7fdf20cf4ed30e096dc5e760e228bacaec9a1c058e40e`.

The password-dialog screenshot labels the new WPA3 target Connected behind
the dialog before credentials are entered. Simultaneous readback still shows
LabAP BSSID02/ch13 and DHCP172.16.66.219, while the test AP has no associated
station. This is a real UI identity/status inconsistency; its ownership is
not yet established and it is not fixed by cache retirement. GUI after true
S3 still has the preserved framebuffer wake-ack prerequisite failure. AP/ad
hoc UI and the remaining profile/security/recovery combinations remain open.
The next functional GUI cell is system-UI AP enable and external-client
service. Do not turn the premature label alone into a speculative driver fix:
source inspection shows getSSID/compactSSID already read the actual RUN BSS,
not desired network intent. Additional ownership evidence would be required.
