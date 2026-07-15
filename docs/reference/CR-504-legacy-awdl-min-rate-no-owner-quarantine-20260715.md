# CR-504 — legacy AWDL min-rate no-owner quarantine

Date: 2026-07-15

## Scope

This correction covers only the historical `APPLE80211_IOC_AWDL_MIN_RATE` SET
handler (IOC 213) in `AirportVirtualIOCTL.cpp`. It preserves the typed
`apple80211_awdl_min_rate` carrier, the existing legacy virtual-interface
dispatcher case, the separate existing legacy GET behavior, all neighboring
AWDL selectors, and Tahoe's existing absence of a min-rate route.

It does not implement AWDL rate policy, change a selector number, add a
route, change a getter, add a state cache, invoke a private selector, alter
P2P, association, scan, radio, firmware, event, or traffic behavior, or touch
Tahoe Skywalk/V2/APSTA source.

## Current reference evidence

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery locates the public
`apple80211setAWDL_MIN_RATE` wrapper at `0xffffff80021c4e64`. It is not a
fixed stub:

- it passes selector `0xd5` (213) through the first owner vtable gate at
  `+0xcc8` and immediately propagates a nonzero result;
- after a recovered AWDL-protocol safe-meta-cast-style owner test, it
  tail-dispatches the original owner and typed carrier through vtable
  `+0x1130`; and
- an unsuccessful owner test returns raw `0xe082280e`.

The wrapper itself only preserves and forwards the carrier; it does not
dereference it. The capture therefore does not establish the terminal method,
carrier layout, null-input behavior, valid-input status, rate policy, state
mutation, event, or firmware semantics. `0xe082280e` is deliberately kept
numeric in the raw record and is not `kIOReturnUnsupported`.

The exact bytes, symbol boundary, identities, and disassembly command are in
`docs/reference/artifacts/awdl-min-rate-public-wrapper-bootkc-current/raw.txt`.

## Local divergence and correction

Before this correction, the historical setter ignored both `object` and typed
`data` arguments and returned `kIOReturnSuccess` unconditionally. The legacy
virtual dispatcher reaches it for the admitted SET half of IOC 213.

There is no local AWDL min-rate state, protocol owner, or named min-rate
backend that could perform the recovered conditional dispatch. The setter now
explicitly leaves both arguments unread and returns
`kIOReturnUnsupported`. This is an Intel no-owner fail-closed boundary; it
does not claim Apple valid-input return-code parity or substitute that generic
public status for the recovered private `0xe082280e` branch.

`AirportVirtualIOCTL.cpp` is compiled by legacy target source phases and is
absent from Tahoe's source phase. This correction is consequently a
historical source-surface change, not a Tahoe runtime claim.

## Verification boundary

`scripts/legacy_awdl_min_rate_quarantine_report.py --check` verifies the
raw-artifact identity/manifest and conditional wrapper anchors; the retained
selector, carrier, dispatcher, header declaration, separate legacy GET,
unchanged Tahoe absence, and legacy-only source phase; the unread,
non-successful setter; and absence of named local min-rate state/backend.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, AWDL, P2P, scan, firmware, event, traffic, or
runtime-execution claim.
