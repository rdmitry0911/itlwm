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
every reconnect error shares this cause. The second instrumented run's logs
were recovered after guest-only management recovery: the queued replay stack
selected 2.4 GHz, returned EINVAL without a firmware command, and rejected
the initial upper scan. Its bounded observer and test script both completed;
loss of network management was not evidence of a hung observer or a panic.

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

## Matching-image runtime qualification

Source `549114c2` built successfully with all 1083 BootKC symbols resolved.
Private AuxKC admission and transactional activation passed. The guest loaded
UUID `13B4F696-F1E4-3FCC-82E4-3834DE4B4E76`, matching the frozen candidate's
Mach-O SHA-256
`351b094168d86f2c922cf59841f722ce9837f9f7309c35738fd837a0b9d0226c`.
Its saved WPA3 STA recovered DHCP and passed 5/5 packets after loading.

The same post-disassociation single-channel request was repeated. At
16:11:22 UTC, the queued replay selected 5 GHz and submitted a real
one-channel firmware command; the driver returned zero and received a
successful scan terminal. There was no initial-scan rejection. The caller
returned in 3 seconds instead of the previous 20-second timeout.

That result set was still empty. A subsequent public network selection
reported `-3912`; another selection also reported the error before DHCP/data
recovered and passed 10/10 packets without off/on. The queued-band defect
is closed, not the missing-candidate or overall reconnect surface.

Concurrent WPA3 STA plus role-7 SAE/required-PMF AP passed 20/20
client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway packets. The guest
entered actual S3 after `pmset sleepnow` at 16:19:11 UTC; the owned QEMU
reported suspended and the serial console recorded `ACPI SLEEP`.
Owned-monitor wake at 16:20:03 produced `ACPI S3 WAKE`. Boot epoch and loaded
UUID were unchanged. Explicit external-client reselection completed SAE
group 19, required PMF and BIP; all three traffic checks passed again.
These AP checks use static addressing and establish service recovery, not
automatic client continuity or a new Internet Sharing DHCP matrix.

Normal AP stop reached both the lower firmware and AP owner zero-result
terminals, then primary traffic passed 10/10. The temporary AP address and
external-client profile were removed; the original managed host profile was
restored without changing the Ethernet management default route.

Frozen release ZIP SHA-256:
`d9c6e543c08e93ef20610a66dccbd6013184a8169fb246faa98e1481ec3e34db`.
The archive's extracted Mach-O hash matches the loaded candidate identity.
