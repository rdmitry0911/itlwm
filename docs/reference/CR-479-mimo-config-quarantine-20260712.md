# CR-479: MIMO configuration false-success quarantine

Date: 2026-07-12

## Reference contract

Tahoe 25C56 AppleBCMWLANCore function FUN_ffffff8001617250 returns
0xe00002bc for NULL. A non-null carrier enters the MIMO power configuration
path through FUN_ffffff800160d426. That helper checks feature 0x2c and routes
through the Core +0x128 power owner at +0x1590 into
FUN_ffffff8001551e88.

This is an owner-backed configuration lifecycle. It is not equivalent to an
unconsumed request that merely reports success.

## Local divergence

AirportItlwm has no MIMO power owner, feature-gated owner path, or matching
configuration transport. Its setMIMO_CONFIG only checked NULL and returned
success. getMIMO_STATUS is a separate output contract and is unchanged here.

## Local correction

The existing local NULL guard remains. Every non-null request now returns
kIOReturnUnsupported before a false successful completion. This is a local
no-backend quarantine; it does not claim that Apple returns unsupported when
its MIMO power owner is available.

## Deterministic guard

scripts/mimo_config_quarantine_report.py --check requires the 25C56
Core/owner anchors, the preserved local NULL guard, a non-null unsupported
result, and absence of a matching Intel MIMO power backend.
