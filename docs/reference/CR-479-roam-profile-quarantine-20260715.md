# CR-479: ROAM_PROFILE false-success quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[485]`,
`getROAM_PROFILE`. It removes the local synthetic multi-band success carrier.
It does not change V1, the GET dispatcher route, `setROAM_PROFILE`, the
separate legacy STA cache, association state, or firmware transport.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x1000171c4` dispatches through virtual offset
`+0x768` to `AppleBCMWLANCore::getROAM_PROFILE` at `0x100140c0c`. Core loads
its RoamAdapter at `+0x15c0` and dispatches to
`AppleBCMWLANRoamAdapter::getROAM_PROFILE` at `0x10001d198`.

The adapter returns raw `0x16` if the carrier or primary interface is absent.
Otherwise it writes its metadata and queries all three bands through
`getRoamProfilePerBand` at `0x10001d216`; only a zero result sets that band’s
success flag. Per-band work rejects an unassociated interface with
`0xe0822403`, then requests `roam_prof` with `runIOVarGet` and propagates a
nonzero command result. The captured disassembly is in
`docs/reference/artifacts/roam-profile-25c56/raw.txt`.

## Local divergence

Before this correction, the local getter accepted every non-null carrier,
zeroed `0x180` bytes, wrote all three metadata values, marked all three bands
successful, and returned success. It has no RoamAdapter, primary-interface
gate, association check, or firmware `roam_prof` producer.

## Local correction

The existing local null guard remains as a safety boundary. Every non-null
request now returns `kIOReturnUnsupported` before reading or mutating the
carrier. No cache, per-band payload, pseudo-owner, or firmware request is
introduced.

This is a no-backend quarantine, **not Apple null, valid-input error-code,
output-layout, association, firmware, or runtime-selector parity**. It does
not establish the complete carrier size or field semantics and does not invoke
the private selector at runtime.

## Verification boundary

`scripts/roam_profile_quarantine_report.py` verifies reference identity and
raw anchors, the active V2 slot, preserved local null safety, removal of the
synthetic output path, and absence of a local association/firmware backend.
No private carrier is constructed or invoked at runtime; build/load and
ordinary network gates are regression checks only.
