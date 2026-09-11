# Association comeback minimum deadline — 2026-09-11

## Evidence and scope

The already published source52951b81 uses a one-second shared watchdog and
rounds the AP's status30 comeback interval up to integer ticks. Two ticks do
not imply two elapsed seconds after the received response: the first tick can
occur immediately. With1000TU (1.024seconds), the unchanged complete production
watchdog sends a direct retry, or invokes lower preparation, at1.000001seconds
when its first tick occurs1microsecond after RX. Both negative cases fail the
minimum-deadline assertion with exit134. A separate late timing control sends
at2seconds and passes. The scheduler/clock and firmware boundaries are explicit
test doubles; this proves a production-function defect, not a newly captured
on-air timing violation.

The earlier real IWN capture in
[AUTH beacon continuation](TAHOE_IWN_AUTH_BEACON_CONTINUATION_20260911.md)
received status30/1000TU and retried1.0509seconds later. That particular retry
was not early. Its other losses and unexplained first run remain separate.
The updated5995e24caa Ghidra export of25C56
AppleBCMWLANCore::handleAssocEvent atffffff800159f976 delegates extended
association data and cannot establish the firmware's exact timer behavior.
Pinned [mac80211 v6.18 handling](https://github.com/torvalds/linux/blob/v6.18/net/mac80211/mlme.c#L6005-L6022)
uses a future deadline for the AP-supplied interval. The local kernel SDK
provides clock_interval_to_deadline/clock_get_uptime and kMicrosecondScale;
the correction uses their common absolute-time domain, not wall-clock time.

## Correction

Valid status30 RX arms one bounded absolute deadline before publishing pending
retry state. When the shared watchdog expires early, it rearms one tick while
preserving that original deadline, BSS, association epoch and SAE PMK. It never
renews the deadline or busy-waits. The immutable lower retry record includes
the deadline as part of its existing epoch/BSSID/subtype/TU/retry identity.

Common completion revalidates the exact identity and elapsed deadline before
publishing a management frame. IWM and IWX additionally apply this admission
before their firmware-protection work. Exact completion, abort, association
success and state transition consume the deadline with existing retry state;
send-error recovery restores only the exact identity needed for owned abort.
Stale completion/abort cannot clear a newer deadline. Early completion returns
EAGAIN without consuming the owner; early cancellation remains permitted.

This is a minimum-delay correction. The existing one-second service cadence
can still delay retry until a later tick; precise low-latency timer parity is
not claimed. It does not fix all deferred MVM worker/lifecycle races, create
physical completion acknowledgements or widen the30-second/3-retry parser
bounds. A new ieee80211com field shifts subsequent diagnostic offsets; probes
for the new image must derive layout again from that exact Mach-O.

## Verification checkpoint

The fixture extracts the complete actual watchdog, deadline setter, identity
check, admission, completion and abort functions, plus the production retry
record. It does not implement a second watchdog. The120 cases cover six TU
values, four tick phases, direct/deferred boundaries and association/reassociation,
exact-minus-one/exact deadline, replacement and duplicate callbacks, cancellation,
send error, invalid epoch/BSSID/subtype/retry/TU/state/owner/null input, out-of-range
arm and a one-year uptime. Linux and macOS ASan/UBSan passed the prototype;
the canonical-source Linux run also passes all120. Source-order checks bind RX
publication and both actual MVM preparation paths. The normal payload aggregate
includes this fixture. Firmware/kernel submission and clock remain explicit
boundaries; no RF pass follows merely from these tests.

SHA-256 evidence:

- unchanged52951b81 source used for the watchdog substitution:
  `5dd0f3e44f1bb46fbc3efa8376f77bedc22bf26ed396e423fc18bdca98b2ebe6`
- canonical green120:
  `97dbbdb6c499d7c6de0ae5f85ce6cebde595560c17b085cd3db0a3a8d4254c77`
- unchanged direct early assertion:
  `b30b39a86d4b64d94a40e07f8f17915462d4c6b4f08a4f790c2762b68db1a2e1`
- unchanged deferred early assertion:
  `e4fe7b84b795ad0b656c5fdef07028cff747bb9adcc6a66eeb84d23c16b42945`
- unchanged late timing control:
  `796a5b9c52e39855884260424f0ad3f3950960984d9f5efa6c96240bd90ffa93`
- prototype macOS120:
  `81daca5dc20c731ad688b73991261e753033c8729629a4b9d0b74b97c973fb76`
- canonical ordinary aggregate including the new120-case fixture:
  `84c444d15f5d958ad1ba68a5580ccb353a1d142c49315972e95d68cf1c7e5626`

An initial late-control harness wrongly required the unchanged watchdog to
clear a field that it never had. That failed log is retained; the corrected,
explicit baseline-late-timing-control checks timing and old direct-send cleanup
only, while every new-source matrix case retains the new deadline assertions.
This harness correction is not counted as a production fix.

Full new-image kext build, activation and actual RF comeback/reconnect plus
STA/S3/AP regressions remain required. Public v2.4.0-alpha remains the
qualified52951b81 image; this next correction is not yet released.
