# CR-514 — legacy AWDL BSSID blind-success cache quarantine

Date: 2026-07-15

## Scope

This correction covers only the historical SET half of
`APPLE80211_IOC_AWDL_BSSID` (IOC 125) in `AirportVirtualIOCTL.cpp`. It
preserves the existing packed local `apple80211_awdl_bssid` carrier,
bidirectional virtual-interface dispatcher case, paired legacy GET behavior,
all neighboring AWDL selectors, and Tahoe's existing absence of an IOC 125
route.

It does not alter the local carrier declaration or infer ABI/layout parity
from equal byte counts, implement BSSID policy, change a selector number, add
a route, change a getter, invoke a private selector, alter association, scan,
radio, firmware, event, traffic, P2P, or CCA behavior, or touch Tahoe
Skywalk/V2/APSTA source.

## Current reference evidence and equal-size boundary

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery locates the public
`apple80211setAWDL_BSSID` wrapper at `0xffffff80021c4460`. It is not a fixed
stub:

- it passes selector `0x7d` (125) through the first owner vtable gate at
  `+0xcc8` and immediately propagates a nonzero result;
- after a recovered AWDL-protocol safe-meta-cast-style owner test, it
  tail-dispatches the original owner and typed carrier through a dynamic
  vtable `+0x10c0` slot; and
- an unsuccessful owner test returns raw `0xe082280e`.

The public wrapper only preserves and forwards the carrier; it does not
dereference it. A separate selector-named internal handler at
`0xffffff80021e4637` checks request length `0x10` and a non-null carrier
before an `IO80211AWDLPeerManager` safe-meta-cast-style cast. It then directly
reaches the named `IO80211AWDLPeerManager::setAwdlBssid` terminal at
`0xffffff8002179d78`.

The bounded terminal entry is manifestly non-noop: it inspects two distinct
six-byte carrier regions at `+0x4` and `+0xa`, updates corresponding manager
fields, and takes a request/status path. This does not establish full
terminal policy, ABI, status, or carrier semantics. The public wrapper's
separately dynamic `+0x10c0` tail is not asserted to resolve to that terminal.

The checked-in local packed carrier and the verified internal admission both
occupy `0x10` bytes: version at `+0x0`, `bssid` at `+0x4`, and `unk_mac` at
`+0xa`. That byte-count equality is not an ABI, layout, validation, or
behavior-equivalence claim. The legacy SET copied only `bssid` and ignored
version and `unk_mac`. The new SET boundary stays unread and does not forward
the carrier.

The capture therefore does not establish terminal return behavior, carrier
layout parity, null-input behavior outside the recorded internal admission,
valid-input status, BSSID policy, state mutation, event, or firmware
semantics. It also must not be read as a claim that the reference selector
never validates the carrier. `0xe082280e` is deliberately kept numeric in the
raw record and is not `kIOReturnUnsupported`.

The exact bytes, symbol boundaries, identities, equal-size/two-region caveat,
and disassembly command are in
`docs/reference/artifacts/awdl-bssid-public-wrapper-bootkc-current/raw.txt`.

## Local divergence and correction

Before this correction, the active historical SET body copied only
`data->bssid` into the six-byte local `awdlBSSID` cache and returned
`kIOReturnSuccess`, ignoring the carrier version and second six-byte region.
The legacy virtual dispatcher admits both GET and SET for IOC 125. The paired
GET remains separate existing behavior: it emits the project version and
copies only the cached first BSSID region into its output.

Named source uses of the legacy BSSID spelling are limited to its field and
this GET/SET pair; the V2 field is separate. No named operational sink was
recovered, but this layer does not infer a global local AWDL or backend
absence, nor any semantics for the local second carrier region.

The active local SET body contains no implementation of the recovered
conditional gate-and-owner-dispatch path. It now explicitly leaves both
arguments unread and returns `kIOReturnUnsupported`. This is a local
blind-success fail-closed boundary; it does not claim Apple valid-input
return-code parity or substitute that generic public status for the recovered
private `0xe082280e` branch.

`AirportVirtualIOCTL.cpp` is compiled by legacy target source phases and is
absent from Tahoe's source phase. This correction is consequently a historical
source-surface change, not a Tahoe runtime claim.

## Verification boundary

`scripts/legacy_awdl_bssid_blind_success_quarantine_report.py --check`
verifies the raw-artifact identity/manifest, public conditional-wrapper
anchors, separate internal `0x10` admission, bounded direct terminal anchor,
and equal-size/no-layout-parity/two-region record; the retained selector,
carrier, dispatcher, header declaration, paired legacy GET, unchanged Tahoe
absence, and legacy-only source phase; and the unread, non-successful SET
body.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, AWDL, P2P, scan, firmware, event, traffic, CCA, or
runtime-execution claim.
