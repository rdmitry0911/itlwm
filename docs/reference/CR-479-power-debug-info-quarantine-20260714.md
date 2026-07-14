# CR-479: power debug info false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only Tahoe V2/Skywalk slot `[480]`,
`getPOWER_DEBUG_INFO`. It removes a fabricated diagnostic snapshot; it does
not change V1, the ABI slot, ordinary power-save policy, or any live telemetry
source.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x100016ffc` tail-dispatches through virtual
offset `+0x358` to `AppleBCMWLANCore::getPOWER_DEBUG_INFO` at
`0x100133876`. The Core body immediately writes a zero qword at caller `+0x4`,
then uses command-rejection state, live power statistics, a copy from Core
`+0x2c50` of `0x2c0` bytes to caller `+0x2c0`, feature `0x4c`, and inactivity
power statistics. Further visible stores reach at least caller `+0x714`.

The raw disassembly is in
`docs/reference/artifacts/power-debug-info-25c56/raw.txt`.

## Local divergence

Before this correction, the local getter retained a local null guard, zeroed
only `0x580` bytes, and returned success. It has no matching power-statistics
reader, Core snapshot, feature gate, or inactivity telemetry producer.

## Local correction

The local null guard remains as a safety boundary. Every non-null request now
returns `kIOReturnUnsupported` before output mutation. No cached diagnostic
carrier, field map, or pseudo-owner is introduced.

This is a no-backend quarantine, **not Apple null, valid-input return-code,
output-layout, or feature-branch parity**. In particular, the reference
dereferences its output pointer before any recovered null guard, while its
valid path produces real data.

## Verification boundary

`scripts/power_debug_info_quarantine_report.py` verifies reference identity
and raw anchors, the active V2 slot, retained local null safety, non-null
fail-closed result, zero-fill removal, and absence of a local snapshot producer
in this getter. No private carrier is constructed or invoked at runtime;
build/load and ordinary network gates are regression checks only.
