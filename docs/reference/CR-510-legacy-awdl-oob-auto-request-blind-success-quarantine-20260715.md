# CR-510 — legacy AWDL OOB-auto-request blind-success shim quarantine

Date: 2026-07-15

## Scope

This correction covers only the historical
`APPLE80211_IOC_AWDL_OOB_AUTO_REQUEST` SET shim (IOC 225) in
`AirportVirtualIOCTL.cpp`. It preserves the existing packed local
`apple80211_awdl_oob_request` carrier, the SET-only virtual-interface
dispatcher case, its existing GET error, all neighboring AWDL selectors, and
Tahoe's existing absence of an IOC 225 route.

It does not alter the local carrier declaration, infer a carrier layout,
implement OOB policy, change a selector number, add a GET route or getter,
invoke a private selector, alter association, scan, radio, firmware, event,
traffic, P2P, or CCA behavior, or touch Tahoe Skywalk/V2/APSTA source.

## Current reference evidence and terminal boundary

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery locates the public
`apple80211setAWDL_OOB_AUTO_REQUEST` wrapper at
`0xffffff80021c50a4`. It is not a fixed stub:

- it passes selector `0xe1` (225) through the first owner vtable gate at
  `+0xcc8` and immediately propagates a nonzero result;
- after a recovered AWDL-protocol safe-meta-cast-style owner test, it
  tail-dispatches the original owner and typed carrier through a dynamic
  vtable `+0x1148` slot; and
- an unsuccessful owner test returns raw `0xe082280e`.

The public wrapper only preserves and forwards the carrier; it does not
dereference or validate it. No selector-named static OOB handler or separate
internal request-size admission was recovered. A separately named
`IO80211AWDLPeerManager::setAwdlOOBRequest` function at
`0xffffff800217a4f0` is an eight-byte raw-success stub, but its relation to
the public wrapper's dynamically resolved `+0x1148` tail is not proven.
It is therefore deliberately not called the terminal target.

The capture does not establish the terminal method, terminal status, carrier
layout, null-input behavior, valid-input status, OOB policy, state mutation,
event, or firmware semantics. It also must not be read as a claim that the
reference selector never validates the carrier. `0xe082280e` is deliberately
kept numeric in the raw record and is not `kIOReturnUnsupported`.

The exact bytes, symbol boundary, identities, terminal-linkage caveat, and
disassembly command are in
`docs/reference/artifacts/awdl-oob-auto-request-public-wrapper-bootkc-current/raw.txt`.

## Local divergence and correction

Before this correction, the active historical setter ignored both `object`
and the typed `data` arguments and returned `kIOReturnSuccess`
unconditionally. The legacy virtual dispatcher reaches this SET-only path for
IOC 225 only under `SIOCSA80211`. Under `SIOCGA80211`, the existing route
does not call a getter and leaves the dispatcher's `kIOReturnError` result.

The active legacy shim contains no implementation of the recovered conditional
gate-and-owner-dispatch path. It now explicitly leaves both arguments unread
and returns `kIOReturnUnsupported`. This is a local blind-success fail-closed
boundary; it does not claim Apple valid-input return-code parity or substitute
that generic public status for the recovered private `0xe082280e` branch.

`AirportVirtualIOCTL.cpp` is compiled by legacy target source phases and is
absent from Tahoe's source phase. This correction is consequently a historical
source-surface change, not a Tahoe runtime claim.

## Verification boundary

`scripts/legacy_awdl_oob_auto_request_blind_success_quarantine_report.py
--check` verifies the raw-artifact identity/manifest, public
conditional-wrapper anchors and terminal-linkage caveat; the retained
selector, packed carrier, SET-only dispatcher, existing GET-error behavior,
unchanged Tahoe absence, and legacy-only source phase; and the unread,
non-successful setter.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, AWDL, P2P, scan, firmware, event, traffic, CCA, or
runtime-execution claim.
