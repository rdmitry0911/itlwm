# CR-479: WCL real-time mode false-success quarantine

Date: 2026-07-12

## Reference contract

The Tahoe 25C56 AppleBCMWLANCoreMac fileset begins at VM
0xffffff800151d000 / file 0x0141d000. Current raw Core setter
FUN_ffffff800161508c at 0xffffff800161508c returns 0xe00002bc when its
apple80211_wcl_real_time_mode carrier is null. For a non-null carrier, a
nonzero first byte calls NetAdapter setRealTimeMode at
0xffffff8001524c5a; a zero first byte calls NetAdapter setDefaultMode at
0xffffff800152499c through the private adapter at Core +0x15e0.

The public InfraProtocol bridge enters this Core producer. Apple success
therefore records a real adapter mode transition, not merely the first byte
of a caller-owned carrier.

## Local divergence

AirportItlwm had no real-time/default NetAdapter mode backend. Its
setWCL_REAL_TIME_MODE(...) nevertheless accepted a non-null request, set the
private and otherwise unconsumed cachedRealTimeMode bit, and returned success.
No scheduler, firmware, or adapter mode changed after that success.

## Local correction

The local null result remains kIOReturnBadArgumentTahoe (0xe00002bc).
Every non-null request now returns kIOReturnUnsupported (0xe00002c7)
without recording pseudo-state. This is a no-backend quarantine, not a claim
that a valid Apple NetAdapter transition returns unsupported. It prevents WCL
from treating an unavailable real-time/default operation as complete and
leaves the carrier ABI untouched for a future backend.

## Deterministic guard

scripts/wcl_real_time_mode_quarantine_report.py --check pins the 25C56 Core
and NetAdapter anchors, requires the null result and non-null unsupported
result before cache mutation, verifies removal of the dead pseudo-state, and
confirms the Intel tree has no real-time/default-mode adapter backend.
