# CR-502 — legacy AWDL election-metric no-owner quarantine

Date: 2026-07-15

## Scope

This correction covers only the historical
`APPLE80211_IOC_AWDL_ELECTION_METRIC` SET handler (IOC 126) in
`AirportVirtualIOCTL.cpp`. It preserves the typed eight-byte
`apple80211_awdl_election_metric` declaration, the existing virtual-interface
dispatcher case, the paired GET route and its existing error behavior, all
nearby AWDL selectors, and the historical target build phases.

It does not implement AWDL election metrics, change a selector number, add a
GET/SET route, add a cache, invoke a private selector, change P2P, alter
association or scan behavior, or touch Tahoe Skywalk/V2/APSTA source.

## Current reference evidence

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery locates the public
`apple80211setAWDL_ELECTION_METRIC` wrapper at
`0xffffff80021c44b5`. It is not a fixed-success or fixed-error stub:

- it passes selector `0x7e` (126) through the first owner vtable gate at
  `+0xcc8` and immediately propagates a nonzero result;
- after a recovered AWDL-protocol safe-meta-cast-style owner test, it
  tail-dispatches the original owner and typed carrier through vtable
  `+0x10c8`; and
- an unsuccessful owner test returns raw `0xe082280e`.

The wrapper itself only preserves and forwards the carrier; it does not
dereference it. Therefore the capture does not establish the terminal method,
carrier layout, null-input behavior, valid-input status, AWDL election
algorithm, state mutation, event, or firmware semantics. `0xe082280e` is
deliberately kept numeric in the raw record and is not
`kIOReturnUnsupported`.

The exact bytes, symbol boundary, identities, and disassembly command are in
`docs/reference/artifacts/awdl-election-metric-public-wrapper-bootkc-current/raw.txt`.

## Local divergence and correction

Before this correction, the historical setter ignored both its `object` and
typed `data` arguments and returned `kIOReturnSuccess` unconditionally. The
historical virtual dispatcher reaches it for the admitted SET half of IOC 126,
while the paired GET handler remains its separate existing error path.

There is no local election-metric state, AWDL-protocol owner, or named metric
backend that could perform the recovered conditional dispatch. The setter now
explicitly leaves both arguments unread and returns
`kIOReturnUnsupported`. This is an Intel no-owner fail-closed boundary; it
does not claim Apple valid-input return-code parity or substitute that generic
public status for the recovered private `0xe082280e` branch.

`AirportVirtualIOCTL.cpp` is compiled by the legacy target source phases and
is absent from Tahoe's source phase. This correction is consequently a
historical source-surface change, not a Tahoe runtime claim.

## Verification boundary

`scripts/legacy_awdl_election_metric_quarantine_report.py --check` verifies
the raw-artifact identity/manifest and conditional wrapper anchors; the
retained selector, carrier, dispatcher, header declaration, paired GET
boundary, and legacy-only source-phase membership; the unread,
non-successful setter; and absence of a named local metric owner/backend.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, AWDL, P2P, scan, firmware, event, traffic, or
runtime-execution claim.
