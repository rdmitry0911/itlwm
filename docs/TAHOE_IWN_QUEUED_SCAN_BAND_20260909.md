# IWN queued WCL scan band — 2026-09-09

## Reproduction and scope

Released `96eaf2e9` was loaded on the disposable IWN/6235 guest (UUID
`0FE73C63-480B-39C3-A43E-53DB3D225366`). A native CoreWLAN private
single-channel scan while associated reached a real one-channel 5-GHz
firmware command; its zero-result reply was not merely a cache lookup.

The same request immediately following ordinary CoreWLAN disassociation
overlapped a generic foreground scan. The generic 13+24-channel scan
completed, followed by a 2.4-GHz constructor with no physical command.
The public one-channel request returned after 20 seconds. Airportd recorded
a timed-out scan even though this private wrapper returned error zero and
an empty set. Later unassociated one-channel requests reached 5 GHz. One
delivered the target beacon to net80211 and returned that target through
CoreWLAN, proving that reception is intermittent rather than wholly absent.
The requested connection subsequently reported `-3912`, but recovered the
WPA3 data path and passed 5/5 packets without a reboot or radio toggle.

These observations do not establish that every missing 5-GHz candidate or
every reconnect error shares this cause. A second, more instrumented
disassociation run lost network management; its results must be recovered
before using it to make a more specific runtime claim.

## Reference and correction

The complete saved 25C56 DriverKit decompile of
`AppleBCMWLANScanAdapter::startEventScan` at `0x10018a026` was inspected.
It passes the caller's `apple80211ScanRequest` into the selected
`fillScanParams*` builder before submitting `escan`. This supports retaining
the admitted channel plan when implementing Intel's per-band commands; it
does not imply Broadcom uses the IWN-specific lease/handoff mechanism.

The earlier exact-band correction covered `beginWclInitialScan()` and
`beginWclBackgroundScan()`, but not the queued initial handoff in
`iwn_scan_lease_replay_task()`. That path still selected 2.4 GHz unconditionally.
An exact 5-GHz-only plan therefore produced no eligible channel after its
preceding generic scan relinquished the radio.

The queued path now uses the same eligible-band selector as immediate
admission. An unsupported plan is rejected before a physical submission.
Generation and predecessor-serial validation, cancellation, and the
post-doorbell STARTED/terminal ownership are unchanged. No regulatory
restriction, channel list, dwell setting, or result filtering is relaxed.

## Verification boundary

`scripts/test_iwn_queued_scan_band.sh` compiles the production band selectors
and complete replay function under UBSan. The previous code fails its first
queued 5-GHz assertion; the corrected code passes. Cases include a mixed
plan, no supported band, absent/unfiltered plans, a still-live predecessor,
a missing terminal handoff, cancellation, pre-doorbell error, and an error
after STARTED. Existing exact-plan, scan-lifecycle, physical-trace,
dwell-budget and APSTA scheduler contracts also pass.

Build, activation and matching-image hardware regression are still pending.
The published release remains `96eaf2e9`; this source correction is not yet
represented as a qualified new release.
