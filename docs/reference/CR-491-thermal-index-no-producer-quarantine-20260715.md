# CR-491 — THERMAL_INDEX no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[500]`,
`getTHERMAL_INDEX`. It removes a local zero/version success carrier that had
no matching state lifecycle or writer. It does not alter the carrier layout,
the virtual setter, POWER_BUDGET, the ordinary GET dispatcher route, firmware
commands, scan/radio state, association, or traffic.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

Infra `getTHERMAL_INDEX` wrapper `0x1000174f4` dispatches through virtual
offset `0x2e8` to Core `0x100106eda`. The recovered body reads
`(Core + 0x48) + 0x0`, stores it at caller `+0x4`, and returns zero. It has no
observed caller-null test and does not initialize caller `version` or zero the
carrier.

The recovered setter path explains why that Core scalar is not a local
default: Infra `0x100018760` dispatches through `0x4f0` to Core `0x100120586`,
which checks feature `0x3b`, validates the request range, builds a 12-byte
`tvpm` payload, and calls `runIOVarSet("tvpm")`. It commits Core state only
for zero or `0xe3ff8117` transport status. This is observed setter-path
evidence, not a claim that every Apple writer has been recovered.

The selected static disassembly capture is
`docs/reference/artifacts/thermal-index-25c56/raw.txt`.

## Local correction

The port has no matching thermal Core state, `tvpm` payload builder, Commander
owner, or transport lifecycle. The local `kIOReturnBadArgument` null guard is
retained only as a safety divergence. Every non-null getter request now
returns `kIOReturnUnsupported` before reading or mutating the carrier.

This is a no-producer quarantine, **not Apple null-input, valid-input
return-code, full carrier-layout, version, Core-state, or runtime-selector
parity**. It does not invoke a selector, IOVAR, firmware command, scan,
radio transition, deployment, association, or traffic path.

## Deterministic guard

`scripts/thermal_index_rejected_state_report.py --check` verifies reference
identity/raw anchors, the retained V2 slot and GET dispatch, local-null safety,
absence of matching local producers, no-output non-null failure, and
supersession of the old zero-baseline claims.
