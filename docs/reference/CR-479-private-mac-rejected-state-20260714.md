# CR-479 — PRIVATE_MAC rejected-state cache record (superseded)

Superseded on 2026-07-15 by
[CR-490-private-mac-no-producer-quarantine-20260715.md](CR-490-private-mac-no-producer-quarantine-20260715.md).

This historical correction removed setter-derived synthetic cache state, but
its remaining statement that `getPRIVATE_MAC` could return a zero local
baseline with success was false. The recovered 25C56 getter requires
BGScanAdapter state and `runIOVarGet("scanmac")`; it has no ownerless zero
success path.

The authoritative current boundary is therefore the CR-490 no-producer
quarantine: preserve the raw `0x16` null guard and fail closed for every
non-null local carrier until a matching owner and transport exist. This file
is retained only to preserve the historical reference path; it makes no
current parity or success claim.
