# CR-479: SIB coex status false-success quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[532]`,
`getSIB_COEX_STATUS`. It removes the local synthetic version/zero success
carrier. It does not change V1, the GET dispatcher route, legacy BTCOEX state,
Core state, or any coexistence transport.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x100017b28` dispatches through virtual offset
`+0x458` to `AppleBCMWLANCore::getSIB_COEX_STATUS` at `0x100119a7a`. For a
non-null carrier, Core copies the dwords at Core state `+0x8b10` and `+0x8b14`
to caller offsets `+0` and `+4`, respectively, then returns zero. Its cold
logging string names them `sib coex mode` and `timeToTST`. The Core null path
returns `0xe00002c2`.

The recovered body does not establish a complete public structure, a caller
`+0x8` field, or equivalence to the local legacy BTCOEX fields. It also does not
justify an inference about hidden owners or IOVARs outside this getter. The
captured disassembly is in
`docs/reference/artifacts/sib-coex-status-25c56/raw.txt`.

## Local divergence

Before this correction, the local getter kept raw null `0x16`, zeroed 12
bytes, stored `APPLE80211_VERSION` at caller `+0`, and returned success. It
read neither of the referenced Core-state values.

## Local correction

The existing local null guard is retained as a safety boundary. Every non-null
request now returns `kIOReturnUnsupported` before reading or mutating the
carrier. No synthetic version, local BTCOEX substitution, Core cache, or
coexistence request is introduced.

This is a no-producer quarantine, **not Apple null, valid-input return-code,
struct-layout, Core-state, BTCOEX-equivalence, or runtime-selector parity**.
It does not invoke the private selector at runtime.

## Verification boundary

`scripts/sib_coex_status_quarantine_report.py` verifies reference identity and
raw anchors, the active V2 slot, preserved local null safety, removal of the
synthetic output path, and absence of local Core/BTCOEX substitution in this
getter. No private carrier is constructed or invoked at runtime; build/load
and ordinary network gates are regression checks only.
