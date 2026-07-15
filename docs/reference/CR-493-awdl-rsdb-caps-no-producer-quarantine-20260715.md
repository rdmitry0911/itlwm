# CR-493 — AWDL_RSDB_CAPS no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[493]`,
`getAWDL_RSDB_CAPS`. It removes the local zeroed `0x0c` success carrier and
its reset-only `cachedAwdlRsdbCaps` field because no local RSDB producer
populates that state. It does not change the opaque carrier declaration, the
virtual slot, the ordinary BSD GET route, AWDL behavior, peer-manager
declarations, scan/radio state, association, or traffic.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

Infra `getAWDL_RSDB_CAPS` wrapper `0x100017a20` dispatches through virtual
offset `0x388` to Core `0x1001328fa`. The observed Core body loads eight bytes
at `(Core + 0x48) + 0x436`, stores them at caller `+0x4`, and returns zero.
It has no observed caller-null test and does not initialize caller `version`.
That establishes an observed scalar/window transfer only; it does not recover
the full opaque carrier layout.

The capture also shows observed producer context for overlapping Core state.
`AppleBCMWLANConfigManager::querySDBPolicies` at `0x10008b716` checks SDB
support, invokes
`AppleBCMWLANCommander::runIOVarGet("rsdb")` at `0x10017b780`, validates
response fields, then calls `AppleBCMWLANCore::updateRSDBCaps` at
`0x1000d9a70`. The selected update body writes response-derived state bytes
beginning at Core `+0x438`. This is observed producer context, not a claim
that all Apple writers, all getter-window bytes, or every response mapping
have been recovered.

The selected static disassembly capture is
`docs/reference/artifacts/awdl-rsdb-caps-25c56/raw.txt`.

## Local correction

AirportItlwm has no matching ConfigManager query, `rsdb` Commander transport,
reply validation, or Core-state update lifecycle. The existing local
`0xe00002c2` null guard remains only as a safety divergence. Every non-null
getter request now returns `kIOReturnUnsupported` before reading or mutating
the opaque carrier. The dead `cachedAwdlRsdbCaps` field and its two reset-only
assignments are removed.

This is a no-producer quarantine, **not Apple null-input, valid-input
return-code, full carrier-layout, version, Core-state, AWDL-feature, or
runtime-selector parity**. It does not invoke a selector, IOVAR, firmware
command, scan, radio transition, deployment, association, or traffic path.

## Deterministic guard

`scripts/awdl_rsdb_caps_quarantine_report.py --check` verifies reference
identity/raw anchors, the retained V2 slot and GET route, local-null safety,
no-output non-null failure, absence of the dead cache and matching local RSDB
producer, the forward-declared opaque carrier boundary, and supersession of
the old telemetry/cache classification.
