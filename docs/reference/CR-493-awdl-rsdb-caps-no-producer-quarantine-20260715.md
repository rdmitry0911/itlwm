# CR-493 — AWDL_RSDB_CAPS: from no-producer quarantine to accurate single-radio caps

Date: 2026-07-15 (quarantine) / superseded 2026-09-16 (accurate all-zero caps)

## Status: SUPERSEDED

The original CR-493 quarantine kept Tahoe V2/Skywalk slot `[493]`
`getAWDL_RSDB_CAPS` fail-closed (`kIOReturnUnsupported` on every non-null
carrier) because the opaque carrier was treated as unrecoverable and there was
no local RSDB producer lifecycle. That fail-closed behavior is now
**superseded**. The carrier write window is fully recovered from the DEXT (see
below) and the value it carries on this hardware is an accurate,
hardware-derived fact — not an AWDL fabrication. `getAWDL_RSDB_CAPS` now
publishes the accurate all-zero RSDB caps qword and returns success. No test
may assert the old unsupported behavior.

### Accurate-value derivation (single radio ⇒ no RSDB)

RSDB (Real Simultaneous Dual Band) means operating two radios in two bands at
once. The reference support probe `AppleBCMWLANCore::isRSDBSupported`
(`@0xffffff800159cea0`, `findWord(caps, "rsdb")`) reports capability only for
multi-radio Broadcom parts, and the cache the getter reads is populated only
behind the dual-radio SDB feature-flag gate
(`AppleBCMWLANCore::checkForSDBSupport @0x1000fa974`, feature-flag bit `0x2e`).
Intel AX211 is a single-radio device: the SDB flag is never set,
`updateRSDBCaps` never runs, and the reference cache is itself all-zero.
Publishing the zeroed caps qword is therefore the correct value for this NIC,
directly analogous to how `getROAM_PROFILE` (CR-479) was moved from fail-closed
to a real reference-grounded carrier once its layout was recovered.

## Original scope (historical)

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
offset `0x388` to Core `0x1001328fa`. The full Core body, re-decompiled from the
`applebcm_reassoc_25C56` Ghidra project (program `AppleBCMWLAN-25C56`, image
base `0x100000000`), is exactly:

```c
/* AppleBCMWLANCore::getAWDL_RSDB_CAPS(apple80211_rsdb_capability *param_1) */
*(undefined8 *)(param_1 + 4) = *(undefined8 *)(*(long *)(this + 0x48) + 0x436);
return 0;
```

It loads eight bytes at `(Core + 0x48) + 0x436`, stores them at caller `+0x4`,
and returns zero — a single 8-byte store and nothing else. It has no caller-null
test and never writes caller `version` at `+0x0`. This IS the complete public
write surface of the getter: the recoverable public carrier is `0xc` bytes —
`version u32 @+0x0` (caller-provided, untouched) and `caps qword @+0x4`. Mirroring
that single 8-byte store is ABI-safe and reference-exact; no larger sized write
is guessed. (The `apple80211_rsdb_capability` type is opaque in the DEXT symbol
table — a Demangler 1-byte placeholder — so the byte extent above is taken from
the getter's own store, not from a recovered struct definition.)

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

## Local correction (superseding, 2026-09-16)

AirportItlwm still has no Broadcom ConfigManager query, `rsdb` Commander
transport, or Core-state update lifecycle — and it does not need one. The
reference caps qword is populated only behind the dual-radio SDB feature-flag
gate (`checkForSDBSupport`, bit `0x2e`); on a single-radio Intel AX211 that gate
is never satisfied, so the reference value the getter copies is definitionally
all-zero. `getAWDL_RSDB_CAPS` now mirrors the reference store exactly:
`memset(carrier + 0x4, 0, 8); return kIOReturnSuccess;`. The existing local
`0xe00002c2` null guard is retained as a safety boundary (the reference has no
null test). The `version` field at `+0x0` is left caller-provided, matching the
reference.

This publishes the accurate hardware-derived RSDB caps for this NIC. It is
**not** a claim of Apple null-input parity, of the full opaque struct
definition beyond the recovered 8-byte write window, of the Broadcom Core-state
producer lifecycle, or of any runtime selector. It does not invoke a selector,
IOVAR, firmware command, scan, radio transition, deployment, association, or
traffic path.

## Deterministic guard

`scripts/awdl_rsdb_caps_quarantine_report.py --check` verifies the reference
identity/raw anchors, the recovered 8-byte caps write window, the retained V2
slot and GET route, local-null safety, the accurate all-zero caps success
write, and the supersession note.
