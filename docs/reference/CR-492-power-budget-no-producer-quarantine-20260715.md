# CR-492 — POWER_BUDGET no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[503]`,
`getPOWER_BUDGET`. It removes the local zero/version success carrier and its
default-only cache because no local writer or owner produces a power-budget
state. It does not alter the carrier declaration, the virtual setter, either
ordinary BSD GET/SET dispatcher branch, firmware commands, scan/radio state,
association, or traffic.

The paired setter remains the separately established local boundary: it keeps
its null guard, feature-bit `0x3b` and `1..100` range checks, then returns
`kIOReturnUnsupported` for an otherwise valid request before any local
completion. This correction neither expands nor reinterprets that setter.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

Infra `getPOWER_BUDGET` wrapper `0x1000175d8` dispatches through virtual
offset `0x2f8` to Core `0x10010712c`. The observed body reads
`(Core + 0x48) + 0x4`, stores that scalar at caller `+0x4`, and returns zero.
It has no observed caller-null test and does not initialize caller `version`
or the rest of the carrier.

The recovered setter path is context for that live Core scalar, not evidence
that every Apple writer has been recovered. Infra `0x1000187f8` dispatches
through `0x4f8` to Core `0x100120790`. That path checks feature `0x3b`,
accepts `1..100`, builds a 12-byte `tvpm` request, and calls
`runIOVarSet("tvpm")`. It commits the requested scalar to
`(Core + 0x48) + 0x4` only for zero or `0xe3ff8117` transport status.

The selected static disassembly capture is
`docs/reference/artifacts/power-budget-25c56/raw.txt`.

## Local correction

The Intel port has no matching power-budget Core state lifecycle, `tvpm`
payload builder, Commander owner, or transport lifecycle. The local
`kIOReturnBadArgument` null guard is retained only as a safety divergence.
Every non-null getter request now returns `kIOReturnUnsupported` before
reading or mutating the carrier. The dead `cachedPowerBudget` field and its
two reset-only assignments are removed; `cachedPowersaveLevel` remains
unrelated and unchanged.

This is a no-producer quarantine, **not Apple null-input, valid-input
return-code, full carrier-layout, version, Core-state, setter, or
runtime-selector parity**. It does not invoke a selector, IOVAR, firmware
command, scan, radio transition, deployment, association, or traffic path.

## Deterministic guard

`scripts/power_budget_quarantine_report.py --check` verifies reference
identity/raw anchors, the retained V2 slot and BSD GET/SET routes, local-null
safety, no-output non-null failure, absence of the dead cache and matching
local producer, the unchanged setter boundary, neutral local ABI wording, and
supersession of the old default-cache claim.
