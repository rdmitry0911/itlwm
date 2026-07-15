# CR-509 — legacy AWDL AF-TX-mode blind-success shim quarantine

Date: 2026-07-15

## Scope

This correction covers only the historical `APPLE80211_IOC_AWDL_AF_TX_MODE`
SET shim (IOC 208) in `AirportVirtualIOCTL.cpp`. It preserves the existing
packed local `apple80211_awdl_af_tx_mode` carrier, the virtual-interface
dispatcher case, the paired existing legacy GET behavior, all neighboring AWDL
selectors, and Tahoe's existing absence of an IOC 208 route.

It does not alter the local carrier declaration or infer a missing/removed
field, implement AF-TX policy, change a selector number, add a route, change
a getter, invoke a private selector, alter association, scan, radio, firmware,
event, traffic, P2P, or CCA behavior, or touch Tahoe Skywalk/V2/APSTA source.

## Current reference evidence and size boundary

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery locates the public
`apple80211setAWDL_AF_TX_MODE` wrapper at `0xffffff80021c4de3`. It is not a
fixed stub:

- it passes selector `0xd0` (208) through the first owner vtable gate at
  `+0xcc8` and immediately propagates a nonzero result;
- after a recovered AWDL-protocol safe-meta-cast-style owner test, it
  tail-dispatches the original owner and typed carrier through vtable
  `+0x1128`; and
- an unsuccessful owner test returns raw `0xe082280e`.

The public wrapper only preserves and forwards the carrier; it does not
dereference it. A separate selector-named internal handler at
`0xffffff80021e5539` checks request length `0x08` and a non-null carrier
before an `IO80211AWDLPeerManager` safe-meta-cast-style cast/call through
metaclass `0xffffff80023e1460`.

The checked-in local packed carrier has a `uint32_t` version followed by a
`uint64_t` mode, hence `0x0c` bytes; the verified internal reference admission
requires `0x08`. This is an explicit local/reference carrier-size divergence.
It does not license a header change, an equivalence claim, or private
invocation. The new setter stays unread and does not forward the carrier.

The capture therefore does not establish the terminal method, carrier layout,
null-input behavior, valid-input status, AF-TX policy, state mutation, event,
or firmware semantics. It also must not be read as a claim that the reference
selector never validates the carrier. `0xe082280e` is deliberately kept
numeric in the raw record and is not `kIOReturnUnsupported`.

The exact bytes, symbol boundary, identities, size-divergence anchor, and
disassembly command are in
`docs/reference/artifacts/awdl-af-tx-mode-public-wrapper-bootkc-current/raw.txt`.

## Local divergence and correction

Before this correction, the active historical setter ignored both `object` and
the typed `data` arguments and returned `kIOReturnSuccess` unconditionally.
The legacy virtual dispatcher reaches its admitted SET half for IOC 208, while
the paired GET handler remains its separate existing version/success path.

The active legacy shim contains no implementation of the recovered conditional
gate-and-owner-dispatch path. It now explicitly leaves both arguments unread
and returns `kIOReturnUnsupported`. This is a local blind-success fail-closed
boundary; it does not claim Apple valid-input return-code parity or substitute
that generic public status for the recovered private `0xe082280e` branch.

`AirportVirtualIOCTL.cpp` is compiled by legacy target source phases and is
absent from Tahoe's source phase. This correction is consequently a historical
source-surface change, not a Tahoe runtime claim.

## Verification boundary

`scripts/legacy_awdl_af_tx_mode_blind_success_quarantine_report.py --check`
verifies the raw-artifact identity/manifest, public conditional-wrapper
anchors, separate internal admission caveat, and preserved `0x0c` versus
`0x08` size-divergence record; the retained selector, carrier, dispatcher,
header declaration, paired legacy GET, unchanged Tahoe absence, and
legacy-only source phase; and the unread, non-successful setter.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, AWDL, P2P, scan, firmware, event, traffic, CCA, or
runtime-execution claim.
