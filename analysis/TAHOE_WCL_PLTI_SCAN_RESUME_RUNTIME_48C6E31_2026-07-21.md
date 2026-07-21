# Tahoe WCL PMK scan-resume candidate runtime record

Date: 2026-07-21

## Exact candidate and release verification

This record covers commit
`48c6e31a0e19c766e81db29a78f7eaabf244765e` and prerelease
`v2.4.0-alpha-48c6e31`.  The release target matched that exact commit.  Its
Tahoe archive was structurally verified before use: it contains the KEXT
bundle's `Info.plist` and Mach-O, has the expected bundle identifier, and keeps
the Tahoe controller personality `IOBuiltin = false`.

The archive SHA-256 is
`6cea29782d0409ffb59680e8677b39a03d1e4854dda76d2555e9cd3a50726cbe`.
The KEXT Mach-O SHA-256 is
`ba605bb025e3d5a264513d9b352db3b1dc73c4d7601250c6252156b8a488cfea`,
with UUID `61CA36C9-7E7B-39BD-AE53-8244BA630E87`.

The archive was admitted through a private AuxKC preflight with no canonical
mutation, then installed through the transactional activation script.  Both
the exact five-member AuxKC set and post-reboot installed/loaded KEXT identity
matched the release.  Only a disposable QEMU external overlay guest rebooted;
the physical validation host was not contacted or rebooted.

## Successful A2DF baseline-control scenario

The loaded exact candidate completed the strict four-cycle A2DF baseline:

- four radio OFF/ON cycles passed;
- each cycle reached a fresh authorized AP association;
- each cycle completed the bounded five-packet laboratory data-plane check
  without loss;
- the management default route, pre-existing Wi-Fi address, and direct
  laboratory route invariants remained unchanged;
- read-only DHCP packet observations were complete; and
- the runner issued no explicit route, address, or state-mutating DHCP
  command.  Reconnection used saved-profile autojoin only.

The guest remained reachable with the exact KEXT loaded after the fourth
cycle.  The bounded post-run kernel-panic marker count was zero.  The raw
runner transcript remains ignored and local-only; its SHA-256 is recorded in
the machine-readable evidence file solely for provenance.

## Important scope boundary

One preparatory, bounded radio cycle restored the existing Wi-Fi address and
direct route through OS-managed saved-profile reconnection.  Its safe
aggregates did **not** observe the new `PMK_READY_SCAN_RESUME` marker.  The
successful four-cycle result therefore proves that this candidate did not
regress the exercised saved-profile association and data-plane scenario; it
does not prove that the newly added WCL PMK-ready resume branch was entered.

No pure SAE/PMF claim is made.  Pure SAE remains reject-only, and this record
does not claim physical-host validation, generic Internet reachability, or a
functional WPA3-SAE implementation.  The exact aggregate evidence is in
`evidence/runtime/tahoe_wcl_plti_scan_resume_48c6e31.json`; it contains no
SSID, BSSID, address, route rendering, password, packet, or raw log.
