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

The first canonical macOS run passed all120 cases but stopped before xcodebuild:
the source-order contract treated an inline `/* ... */` field comment as a
pointer because it rejects every asterisk in the retry-record body. The units
comment was moved into the existing description above the record; the strict
pointer check was not removed or weakened. This is a contract/comment repair,
not a changed runtime layout or a successful kext build. The failed q1 log is
retained before the q2 build attempt.

## Full build and private activation

Source correctionc9715b63 and comment repair1f14e709 were pushed before the
second build. All353 prior source-manifest entries were checked in the macOS
mirror before copying the exact changed files. The new full manifest was
verified before and after the ordinary AP-capable Tahoe build. Canonical
macOS120, both source-order contracts and xcodebuild pass. All1085 external
symbols resolve and there is no thread_call_cancel_wait import.

- source ID: `4ec402769fb5`
- source manifest SHA-256:
  `4ec402769fb5dec3146a0b26f838d0307fa0a24724179502c29878ba56fe2646`
- candidate Mach-O UUID: `BE0C8CE1-4924-39F2-BE2B-5929C1039113`
- candidate Mach-O SHA-256:
  `e3d8139f121fb138ad0a71091a4c0e3b06ec6404dde24d3ee0dc6de0e5551091`
- complete q2 build/test/reverification log SHA-256:
  `5e908f314783f437fbbc757ddbf83054e60f04f7de2e90ebd52a9e1bc9eb85d2`
- stopped-before-build q1 log SHA-256:
  `7efe18b4a22a1bffb5fb747c8620bd528ef371329100c191a3b574930018bb28`

The exact candidate passed private AuxKC admission with five members and no
canonical mutation. The two helper scripts were independently hash-compared
between source and guest. Transactional activation returned READY at13:30:37
UTC with rollback copies retained. A normal lab guest reboot was requested
at13:32:06. This checkpoint does not yet attest the loaded image or RF result.
Runtime root: `/home/dima/Projects/itlwm/aiam-comeback-deadline-runtime.P842eX`.
Public release remains the qualified52951b81 bundle.

## Loaded-image and real AP-directed retry observations

The reboot loaded the exactBE0C8CE1 candidate in boot
`2F68EDF1-FC47-4653-872C-568F99E15DAD`. Native WPA3 DHCP started at13:32:45 UTC
on LabAP BSS9a:fb:5d:97:a9:02/channel13. Independent1400-byte boot traffic
passed20/20 both ways, maximum170.066/20.279ms. Before directed testq1, the
system had automatically moved to82:c3:97:84:51:ca/channel9; the exact-source
precondition stoppedq1 before any observer or roam request. It is not an RF run.

Two subsequent ordinary Apple80211 framework requests each returned0 and
completed the requested BSS transition without radio toggling or resubmission:

| Run | Direction | Received AP comeback | Actual production retry elapsed | Traffic forward/reverse |
| --- | --- | --- | --- | --- |
| q2 | ca:9 to02:13 | status30,1000TU | 1,075,541,811ns | 242/250,241/250 |
| q3 | 02:13 toca:9 | status30,1000TU | 1,103,180,254ns | 243/250,243/250 |

Both exceed the1,024,000,000ns AP interval and reach RUN/reassociation success.
The observer binds a successful real deadline-setter call to management send
inside the actual watchdog/continuation stack. It uses monotonic timestamps,
not guessed ieee80211com offsets or kernel writes. It measures real management
submission, not a monitor-captured on-air frame timestamp; data pcaps here use
Ethernet BPF. Neither observed phase proves the new early-tick rearm branch
executed on hardware. That phase-specific regression remains established by
the unchanged/new actual-function negative/positive tests above.

Both70-second observers ended with zero DTrace errors and no early submission.
Guest/host capture counts were986/499 forq2 and989/501 forq3, all with zero
kernel capture drops. q2 has one real encap rejection; q3 has none. Forward/
reverse maximum RTTs were175.531/210.677ms and97.729/196.593ms respectively.
The losses are not an improvement or seamless-roaming claim.

- q2 terminal trace SHA-256:
  `d6cb86ad65cc389ef4ced56a489cbd1c262d591c82ea7cf8471cb941aed46b34`
- q3 terminal trace SHA-256:
  `776805b2d6478bc2fbd884b25a33422f48a6a32b1f8b73743b85f0d8bc8e9ed3`

The first observer compile stopped on an uninitialized += counter before
enabling probes; explicit BEGIN initialization fixed it. Its source/log are
retained separately. The corrected observer compiled before either RF run.
Real S3 and remaining STA/AP/GUI regressions are the next qualification steps;
this candidate is not the public release yet.

## Same-image real S3, Wi-Fi-only recovery

The exact BE0C8CE1 image remained in boot 2F68EDF1 through real Normal Sleep
from 13:40:36 to 13:42:28 UTC (112 seconds in pmset). QEMU independently
reported suspended before the explicit wake. The diagnostic USB Ethernet and
USB tablet were removed before sleep; guest interface census before and after
the data test contained only lo0/gif0/XHC1/stf0/ap1/en1. No radio toggle,
network selection, driver reload or guest reboot was used for recovery.

Native WPA3 obtained a fresh DHCP ACK for 172.16.66.219 at 13:42:35 UTC.
Direct Wi-Fi SSH and independent 1400-byte traffic passed 20/20 in both
directions, maximum RTT 128.922/16.594 ms. The initial immediate post-wake SSH
probe failed with No route to host/255 and is retained; the successful data
test was a later, separately labelled observation, not an instantaneous
availability claim. Its state readback began at 13:43:10 UTC.

pmset also records a 30-second WindowServer sleep-notification timeout. This
is not a post-S3 GUI qualification. Diagnostic USB was re-added only after
the Wi-Fi-only traffic and same-boot readback completed, for subsequent AP
tests; its first SSH banner probe timed out and remains separate evidence.

- Wi-Fi-only terminal log SHA-256:
  `a55af09d9666265a2a193a858be2e73af327ba5b4b1dda8c7a3705846f61c20b`
- same-boot, interface-absence and pmset readback SHA-256:
  `8623121eeb937877c5261acda38c1f8cb367754ad76ca2882bd8049f5fbe8b1c`

The post-S3 AP security matrix and remaining native STA/GUI regression are
still pending; the public release remains source52951b81.

## Post-S3 native AP security matrix

All three ordinary CoreWLAN/Internet Sharing AP cycles completed on the same
BE0C8CE1 image and 2F68EDF1 boot, without another sleep or reboot. External
host AX211 independently negotiated SAE with mandatory PMF (`pmf=2`, BIP),
WPA2-PSK, and NONE respectively and obtained DHCP address 192.168.2.2.
Each passed 20/20 client-to-AP 1400-byte packets, 10/10 AP-to-client packets
after clearing the bridge ARP entry, and the exact HTTP payload through the
guest's Internet Sharing/NAT path. After ordinary sharing disable, bridge100
was absent, the retired bridge's ioref was zero, and native STA traffic to
172.16.66.1 passed 10/10. Host restoration returned zero each time; the wired
management default route was unchanged.

| Mode | Cycle UTC | Maximum RTT forward / cold reverse / restored STA |
| --- | --- | --- |
| WPA3 | 13:50:19–13:52:07 | 26.540 / 106.746 / 11.313 ms |
| WPA2 | 13:52:35–13:54:23 | 26.581 / 103.593 / 66.281 ms |
| Open | 13:54:53–13:56:41 | 20.226 / 111.591 / 74.125 ms |

Each external DHCP capture has two packets, each ARP/ICMP capture has 24,
and all six report zero kernel capture drops. The cold-traffic trace includes
real q11 data TX completions; WPA3 also retains four unacknowledged q7
management completions (status 0x83, ackfail 4). The successful payload checks
are not a claim of universally error-free management traffic or throughput
parity. This matrix starts AP after S3; it does not attest keeping an already
active AP across S3, post-wake GUI usability, or physical IWM/IWX behavior.

The 45-file external AP evidence manifest was verified in full:
`de708a96cecf5ca6cef6ecafd2486bcd7e765dcd6d91763ff4b523ff6ad80c76`.

The frozen preflight bundle was compared recursively with the installed kext,
then packaged without rebuilding. The extracted normalized archive matches
the full frozen bundle and the exact loaded Mach-O UUID/hash. The downloaded
candidate archive SHA-256 is
`feb561fc09360d9ddbea0d513f1d24b32f6108f9589b8e0a5253f0799a2f35ec`.
It remains unpublished pending the current native STA and cold GUI regressions.
