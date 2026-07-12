# CR-479: GAS request false-success quarantine

Date: 2026-07-12

## Reference contract

The Tahoe 25C56 `AppleBCMWLANCoreMac` fileset begins at VM
`0xffffff800151d000` / file `0x0141d000`. Current raw
`AppleBCMWLANCore::setGAS_REQ` is `FUN_ffffff8001608d72` at
`0xffffff8001608d72`. It returns
`0xe00002c2` for a null request. For a non-null request it obtains the live
`AppleBCMWLANGASAdapter` from Core `+0x1560`, resets pending query state,
calls `setGASQueryParams` at `0xffffff800151dab8`, marks the query active,
and calls `startGASQuery` at `0xffffff800151dc98`. A start failure is returned
to the caller and rolls the active state back.

`startGASQuery` selects a firmware-generation-specific ANQP/GAS request
layout using the request peer count at `+0x210`. The adapter also owns GAS
fragment/completion handling and publishes the final event through the Core's
event path. Apple success thus means that a real asynchronous GAS operation
was started, not merely that a user pointer was non-null.

## Local divergence

AirportItlwm has no GAS/ANQP adapter, query state machine, peer transport,
fragment receiver, completion publisher, or abort transport. Its
`setGAS_REQ(...)` nevertheless set the private, unconsumed
`cachedGasQueryIssued` bit and returned success. Nothing was transmitted and
no completion could follow that public success.

`setGAS_ABORT(...)` is a separate selector with a different reference path.
It is deliberately not changed by this correction.

## Local correction

The null public result remains the observed Apple raw `0xe00002c2`. Every
non-null local request now returns `kIOReturnUnsupported` (`0xe00002c7`)
without recording pseudo-state. This is a local no-backend quarantine, not a
claim that an Apple Core with its allocated GAS adapter returns unsupported.
It prevents user space from interpreting a nonexistent query/transport as a
started operation while leaving the exact request ABI untouched for a future
real backend.

## Deterministic guard

`scripts/gas_request_quarantine_report.py --check` pins the current 25C56
entry and adapter/start anchors, requires the preserved null result and the
non-null unsupported result before any cache write, verifies removal of the
dead pseudo-state, distinguishes `GAS_ABORT`, and confirms the Intel source
tree has no GAS transport or completion backend.
