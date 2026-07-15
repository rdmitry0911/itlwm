# CR-479 — THERMAL_INDEX rejected-state record (superseded)

Superseded on 2026-07-15 by
[CR-491-thermal-index-no-producer-quarantine-20260715.md](CR-491-thermal-index-no-producer-quarantine-20260715.md).

This historical correction removed setter-derived synthetic cache state, but
retained a `getTHERMAL_INDEX` zero/version success baseline. The fresh 25C56
getter instead reads a live Core scalar at caller `+0x4` without initializing
`version`, while the recovered setter path requires feature-gated `tvpm`
transport to populate that state.

The authoritative current boundary preserves the local
`kIOReturnBadArgument` null-safety guard and fails closed for every non-null
carrier. This file remains only as a historical redirect and makes no current
success, null, version, or return-code parity claim.
