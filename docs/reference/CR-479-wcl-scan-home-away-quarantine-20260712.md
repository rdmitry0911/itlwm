# CR-479: WCL scan-home-away false-success quarantine

Date: 2026-07-12

## Reference contract

Tahoe 25C56 public bridge FUN_ffffff8001522d28 passes the
scanHomeAndAwayTime dword to scan adapter Core +0x1530. Adapter function
FUN_ffffff80016ac8a6 builds the scan_home_away_time firmware iovar and
submits it through the controller workqueue, returning that transport status.

The Core-owned route therefore performs an actual adapter operation. A copied
dword is not an equivalent user/kernel-space effect.

## Local divergence

AirportItlwm retained the caller dword in unconsumed
cachedScanHomeAwayTime and returned success, but it has no scan adapter, iovar
transport, queued callback, or firmware implementation for this selector.

## Local correction

The existing local NULL guard remains. Every non-null request now returns
kIOReturnUnsupported before pseudo-state mutation. This is a local
no-backend quarantine, not a claim that Apple rejects an available scan
adapter request.

## Deterministic guard

scripts/wcl_scan_home_away_quarantine_report.py --check requires the 25C56
bridge/adapter/iovar anchors, preserved local NULL guard, non-null unsupported
result, removal of the dead cache, and absence of a matching Intel backend.
