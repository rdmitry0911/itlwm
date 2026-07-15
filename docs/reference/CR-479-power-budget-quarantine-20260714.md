# CR-479 — POWER_BUDGET setter quarantine (superseded scope)

Superseded on 2026-07-15 by
[CR-492-power-budget-no-producer-quarantine-20260715.md](CR-492-power-budget-no-producer-quarantine-20260715.md).

This historical record correctly narrowed `setPOWER_BUDGET` to its local
null/feature/range boundary and removed a synthetic setter acknowledgement.
It explicitly left the getter's default-only cache outside its scope. Fresh
25C56 recovery shows that getter reads live Core state populated by an
unimplemented `tvpm` lifecycle, so that remaining false-success surface is
now removed by CR-492.

This file is retained only as a historical redirect. It makes no current
getter success, null-input, version, carrier-layout, Core-state, or
valid-input return-code parity claim.
