# CR-506 — legacy AWDL synchronization-channel-sequence blind-success shim quarantine

Date: 2026-07-15

## Scope

This correction covers only the historical
`APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` SET shim (IOC 129) in
`AirportVirtualIOCTL.cpp`. It preserves the packed typed
`apple80211_awdl_sync_channel_sequence` carrier, the existing
virtual-interface dispatcher case, the paired existing legacy GET behavior,
the imported channel-sequence/CCA ABI declarations, all neighboring AWDL
selectors, and Tahoe's existing absence of an IOC 129 route.

It does not implement channel-sequence policy, change a selector number, add
a route, change a getter, invoke a private selector, alter association, scan,
radio, firmware, event, traffic, P2P, or CCA behavior, or touch Tahoe
Skywalk/V2/APSTA source.

## Current reference evidence

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery locates the public
`apple80211setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` wrapper at
`0xffffff80021c4520`. It is not a fixed stub:

- it passes selector `0x81` (129) through the first owner vtable gate at
  `+0xcc8` and immediately propagates a nonzero result;
- after a recovered AWDL-protocol safe-meta-cast-style owner test, it
  tail-dispatches the original owner and typed carrier through vtable
  `+0x10d0`; and
- an unsuccessful owner test returns raw `0xe082280e`.

The public wrapper only preserves and forwards the carrier; it does not
dereference it. A separate selector-named internal handler at
`0xffffff80021e47ab` checks request length `0x190` and a non-null carrier
before an `IO80211AWDLPeerManager` safe-meta-cast-style cast/call through
metaclass `0xffffff80023e1460`. Therefore the capture does not establish the
terminal method, carrier layout, null-input behavior, valid-input status,
channel-sequence policy, state mutation, event, or firmware semantics, and it
must not be read as a claim that the reference selector never validates the
carrier. `0xe082280e` is deliberately kept numeric in the raw record and is
not `kIOReturnUnsupported`.

The exact bytes, symbol boundary, identities, internal admission anchor, and
disassembly command are in
`docs/reference/artifacts/awdl-sync-channel-sequence-public-wrapper-bootkc-current/raw.txt`.

## Local divergence and correction

Before this correction, the active historical setter ignored both `object` and
the typed `data` arguments and returned `kIOReturnSuccess` unconditionally.
Its only apparent data use is inside a disabled `#if 0` diagnostic block. The
legacy virtual dispatcher reaches it for the admitted SET half of IOC 129,
while the paired GET handler remains its separate existing version/success
path.

The active legacy shim contains no implementation of the recovered conditional
gate-and-owner-dispatch path. It now explicitly leaves both arguments unread
and returns `kIOReturnUnsupported`. This is a local blind-success fail-closed
boundary; it does not claim Apple valid-input return-code parity or substitute
that generic public status for the recovered private `0xe082280e` branch.

Imported ABI headers still declare channel-sequence and CCA hooks, including
`IO80211Controller::setChannelSequenceList`. This correction neither removes
nor treats those declarations as a proven local implementation or a proven
global absence of an owner/backend.

`AirportVirtualIOCTL.cpp` is compiled by legacy target source phases and is
absent from Tahoe's source phase. This correction is consequently a historical
source-surface change, not a Tahoe runtime claim.

## Verification boundary

`scripts/legacy_awdl_sync_channel_sequence_blind_success_quarantine_report.py --check`
verifies the raw-artifact identity/manifest, public conditional-wrapper
anchors, and separate internal admission caveat; the retained selector,
carrier, dispatcher, header declaration, paired legacy GET, preserved ABI
hook declaration, unchanged Tahoe absence, and legacy-only source phase; and
the unread, non-successful active setter.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, AWDL, P2P, scan, firmware, event, traffic, CCA, or
runtime-execution claim.
