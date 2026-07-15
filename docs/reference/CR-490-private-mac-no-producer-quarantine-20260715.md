# CR-490 — PRIVATE_MAC no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[490]`,
`getPRIVATE_MAC`. It removes the local zero/version/enabled success carrier
that had no corresponding local owner. It does not change the carrier ABI,
V1, the ordinary GET dispatcher route, scan identity, association state, or
any scan transport.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

Infra wrapper `0x1000172f4` dispatches through virtual offset `0x6e8` to
`AppleBCMWLANCore::getPRIVATE_MAC` at `0x100119538`. A null carrier returns
raw `0x16`. For a non-null carrier, Core obtains BGScanAdapter through
`(Core + 0x48) + 0x1578`, stores `isPrivateMacEnabled()` at caller `+0x4` and
`getPrivateMacTimeout()` at `+0xc`, then calls
`AppleBCMWLANCommander::runIOVarGet("scanmac")`. A nonzero command status is
propagated; only a successful command fills opaque reply portions at `+0x8`
and `+0x10..+0x1b`. Core does not manufacture a zero fallback or write
caller `version`.

The static read-only disassembly capture is
`docs/reference/artifacts/private-mac-25c56/raw.txt`.

## Local correction

The port has no BGScanAdapter owner, private-MAC state producer, or `scanmac`
transport. The existing local null boundary is retained and returns raw
`0x16`. Every non-null getter request now returns `kIOReturnUnsupported` before
reading or mutating the carrier. The paired virtual setter already has the
same no-owner boundary; the normal BSD GET dispatch stays present for ABI
routing.

This is a no-producer quarantine, **not Apple valid-input return-code, full
carrier-layout, BGScan-owner, IOVAR-result, or runtime-selector parity**. It
does not invoke a private selector, IOVAR, scan operation, radio transition,
deployment, association, or traffic path.

## Deterministic guard

`scripts/private_mac_rejected_state_report.py --check` validates the
reference identity/raw anchors, active slot and dispatch, preserved null
boundary, absence of a local producer, and the fail-closed non-null getter and
setter paths. It also verifies that the former zero-baseline claims are marked
superseded.
