# CR-479: WCL ULOFDMA false-success quarantine

Date: 2026-07-12

## Reference contract

The Tahoe 25C56 AppleBCMWLANCoreMac fileset begins at VM
0xffffff800151d000 / file 0x0141d000. Current raw Core setter
FUN_ffffff8001616876 at 0xffffff8001616876 returns 0xe00002bc for a null
carrier. A non-null apple80211_wcl_ulofdma_state forwards its first dword to
the private 11ax adapter at Core +0x15c8 through
FUN_ffffff80016247b2 at 0xffffff80016247b2.

The adapter builds a firmware-generation-specific request and schedules it
through the controller workqueue or immediate iovar route. Apple success
therefore means a real ULOFDMA adapter operation, not merely retention of a
caller dword.

## Local divergence

AirportItlwm has no 11ax ULOFDMA adapter, firmware command, workqueue owner,
or completion path. Its setter accepted a non-null request, retained the
unconsumed cachedUlofdmaState dword, and returned success.

## Local correction

The null result remains kIOReturnBadArgumentTahoe (0xe00002bc). Every
non-null request now returns kIOReturnUnsupported (0xe00002c7) without
recording pseudo-state. This is a no-backend quarantine, not a claim that
Apple rejects a request when its 11ax adapter exists.

## Deterministic guard

scripts/wcl_ulofdma_quarantine_report.py --check pins the current Core and
adapter anchors, preserves the null result, requires non-null unsupported
before mutation, removes the dead carrier, and proves no matching Intel
adapter transport exists.
