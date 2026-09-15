# CR-622 — THERMAL_INDEX nominal-identity прослойка (supersedes CR-491/CR-479)

Date: 2026-09-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[500]`,
`getTHERMAL_INDEX`. It supersedes the no-producer quarantine recorded in
[CR-491-thermal-index-no-producer-quarantine-20260715.md](CR-491-thermal-index-no-producer-quarantine-20260715.md)
and the earlier [CR-479-thermal-index-rejected-state-20260714.md](CR-479-thermal-index-rejected-state-20260714.md).
It does not alter the carrier layout, the virtual setter (which stays
fail-closed — Intel has no `tvpm` transport to accept a budget write), the GET
dispatcher route, firmware commands, scan/radio state, association, or traffic.

## прослойка principle

The f-macos−f-nix emulation layer must emit a **functionally-equivalent** value
at the contact surface even when the reference sources it from Broadcom-private
firmware Intel lacks. Returning `kIOReturnUnsupported` where the reference
returns a value is itself a non-identity, so this slot must produce the
reference's nominal output.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is `com.apple.DriverKit-AppleBCMWLAN`:

```text
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

Infra `getTHERMAL_INDEX` wrapper `0x1000174f4` dispatches through virtual
offset `0x2e8` to Core `0x100106eda`. The recovered body reads
`(Core + 0x48) + 0x0`, stores it at caller `+0x4`, and returns zero (success).
That Core scalar is a TVPM (Broadcom thermal/voltage/power-management) index
whose **own default after `AppleBCMWLANCore::resetTVPMIndicies` is 100**. TVPM
indices run 1–100 where **100 == full budget / no throttle**.

The selected static disassembly capture is
`docs/reference/artifacts/thermal-index-25c56/raw.txt`.

## Local correction

Intel is not TVPM-throttling, so 100 (no throttle) is Intel's actual nominal
state **and** simultaneously matches the reference's own default output — a
**functionally equivalent** identity, not a fabricated crutch. The getter now
retains the `kIOReturnBadArgument` null guard, then writes
`version = APPLE80211_VERSION`, `thermal_index = 100`, and returns
`kIOReturnSuccess`. There is still no `tvpm` Core-state lifecycle or writer;
the value is the reference default surfaced directly.

## Deterministic guard

`scripts/thermal_index_rejected_state_report.py --check` verifies the reference
identity/raw anchors (Core scalar + `tvpm` setter path), that the getter now
emits the nominal `100` success carrier (no fail-closed return), the retained V2
slot and GET dispatch, the still-fail-closed setter boundary, absence of a
matching local `tvpm` producer, and that this note cites the прослойка
principle and the `resetTVPMIndicies` default of 100.
