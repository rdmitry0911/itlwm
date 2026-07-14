# CR-479: MIMO status false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only Tahoe V2/Skywalk slot `[516]`,
`getMIMO_STATUS`. It removes the local fabricated status carrier and does not
change the separate `setMIMO_CONFIG` control surface.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x100017870` tail-dispatches to
`AppleBCMWLANCore::getMIMO_STATUS` at `0x10011989c`. Core calls
`featureFlagIsBitSet(0x2c)` and initializes its return to `0xe00002c7`.
Only when that feature is enabled does it invoke Commander
`runIOVarGet("mimo_ps_status")`; a transport failure is returned. On success,
the reference writes its actual response fields at caller offsets `+0` through
`+9`. It does not provide the former local versioned 0x21-byte zero carrier.

The disassembly is captured in
`docs/reference/artifacts/mimo-status-25c56/raw.txt`.

## Local divergence

Before this correction, the local getter rejected null with its pre-existing
local `0xe00002c2` guard, then used `TahoeMimoContracts` to build a 0x21-byte
carrier with version `1` and zeros before returning success. That helper and
its standalone test had no MIMO owner, feature gate, `mimo_ps_status` IOVAR,
or transport consumer behind them.

## Local correction

The local null guard remains unchanged. Every non-null carrier now returns
`kIOReturnUnsupported` before output mutation. The dead helper, include, and
test are removed; the payload-contract summary is corrected from 30 to 29
contracts so it no longer claims MIMO status coverage.

This is a no-backend quarantine, **not Apple null or valid-input return-code
parity**. In particular, the reference feature-disabled status is evidence for
a real gate, not proof that the local null guard matches it.

## Verification boundary

`scripts/mimo_status_quarantine_report.py` checks the reference identity/raw
anchors, active V2 slot, retained local null guard, non-null fail-closed
result, removal of the synthetic helper/test, and absence of the local IOVAR
backend. No private carrier is constructed or called at runtime; build/load
and normal network gates are regression checks only.
