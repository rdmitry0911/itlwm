# CR-503 — legacy peer-cache-maximum fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the historical
`APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE` SET handler (IOC 130) in
`AirportVirtualIOCTL.cpp`. It retains the typed
`apple80211_peer_cache_maximum_size` carrier, the existing legacy
virtual-interface dispatcher case, the separate legacy GET handler, the
APSTA/Tahoe GET behavior, and Tahoe's existing SET rejection.

It does not change any getter output, APSTA owner, selector number, route,
carrier layout, AWDL election/channel/presence state, P2P behavior, scan,
association, radio, firmware, event, or traffic path.

## Current reference evidence

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery identifies
`apple80211setPEER_CACHE_MAXIMUM_SIZE` at
`0xffffff80021c4575`. The exact body is the 11-byte sequence
`55 48 89 e5 b8 0e 28 82 e0 5d c3`:

```text
push rbp
mov  rbp, rsp
mov  eax, 0xe082280e
pop  rbp
ret
```

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation. The current public setter therefore
returns fixed nonzero raw `0xe082280e`. The status has no public local
symbolic name and is not `kIOReturnUnsupported` (`0xe00002c7`). The exact
symbol boundary, identities, raw bytes, and disassembly command are retained
under
`docs/reference/artifacts/peer-cache-maximum-public-fixed-stub-bootkc-current/`.

## Local divergence and correction

Before this correction, the historical setter ignored both arguments and
returned `kIOReturnSuccess` unconditionally. It now leaves both arguments
unread and returns the exact recovered numeric `0xe082280e` fixed status.

The direct reference is current public-wrapper evidence. Applying its exact
fixed body to the otherwise blind-success historical handler reduces a false
capability claim; it does not claim Apple historical behavior, caller
population, runtime reachability, or broader peer-cache semantics are
identical.

The legacy GET path remains separate and unchanged, including its existing
local value. Tahoe's active Skywalk bridge continues to serve only its
existing GET branches for this selector and returns
`kIOReturnUnsupported` for SET. `AirportVirtualIOCTL.cpp` is absent from the
Tahoe source phase, so this change makes no Tahoe runtime claim.

## Verification boundary

`scripts/legacy_peer_cache_maximum_fixed_stub_alignment_report.py --check`
verifies raw identity/manifest/symbol/body, retention of the selector/carrier
and legacy route, exact unread legacy fixed status, unchanged legacy GET,
and the separate Tahoe GET-only/SET-rejection route and source-phase
boundary.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, or
runtime-execution claim.
