# CR-479: WCL BCN mute false-success quarantine

Date: 2026-07-12

## Reference contract

Tahoe 25C56 Core function FUN_ffffff80016176b0 returns 0xe00002bc for NULL.
For non-null input it sends the four-byte carrier to the NetAdapter at Core +0x128, +0x15e0 through FUN_ffffff8001528c38. That adapter checks feature 0x52, builds firmware iovar bcn_mute_miti_config, uses the controller
workqueue, and returns its transport status.

The reference route therefore performs a hardware-backed beacon mitigation
configuration. A copied four-byte value is not an equivalent effect.

## Local divergence

AirportItlwm retained the four-byte request in unconsumed
cachedBcnMuteConfig and returned success. It has no beacon-mitigation adapter,
firmware iovar, queued callback, or matching transport backend.

## Local correction

The existing local NULL guard remains. Every non-null request now returns
kIOReturnUnsupported before pseudo-state mutation. This is a local no-backend
quarantine; it does not claim that Apple rejects a request when its adapter is
available.

## Deterministic guard

scripts/wcl_bcn_mute_quarantine_report.py --check requires the current
Core/NetAdapter/iovar anchors, preserved local NULL guard, non-null
unsupported result, removal of dead cache, and absence of an Intel backend.
