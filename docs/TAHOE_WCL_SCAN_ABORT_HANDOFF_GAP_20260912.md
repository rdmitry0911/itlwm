# WCL discovery lost between roam cancellation and physical scan retirement

Status: **the exact lost-census handoff now passes twice on loaded092a7479/9B5B;
stable WPA2 and open-to-WPA2 controls also pass. Native WPA3, off/on and actual
S3 recovery pass. AP/WPA3 passes; AP/WPA2 serves its client but its subsequent
STA restoration fails with a stranded post-target roam owner. AP/open and
the final roaming pair were not run; public alpha remains477ab0af/842B.**
Initial new-image steady traffic retains20/20 forward but19/20 reverse.
The baseline failure was reproduced twice on477ab0af/842B. The immediate
cause is the live lower scan lease after successful logical
roam cancellation, not an unknown generic Device Busy branch. This affects an
ordinary new-network selection while native roaming is searching. Historical
diagnosis/source/build checkpoints below are followed by actual loaded-image
receipts; they do not imply that all other reconnect failures are closed.

The later failure and exact-image read-only observations are recorded in
[post-target cancellation](TAHOE_REASSOC_POST_TARGET_CANCELLATION_20260912.md).
It is distinct from the repaired physical scan handoff. Its attribution to
092a7479 as a regression is not established. Do not publish9B5B as qualified.

## Same-image controls before provoking the overlap

Guest IWN/6235 retains UUID842B08A6-AB1B-394A-9490-C10EB1F1D9D2 and boot
1FB8BDE2-E95D-4B56-8948-A4FCE549D6BD throughout. The owned AX211 advertises
AIAM-UIF3-WPA2, channel9, while its normal managed-STA producer is disabled
for exclusive AP ownership. Wired management and guest USB SSH are separate.

- Stable-source native WPA2 selection at00:06:11UTC succeeds, obtains
  DHCP192.168.73.35 and passes20/20 packets each direction. Both public scan
  carriers enter setWCL_SCAN_REQ, reserve, stage and submit their respective
  2GHz/5GHz commands. The90s observer sees35 ingress calls, no Busy, errors0.
- A separate open-fixture selection at00:08:59UTC succeeds with DHCP.26 and
  20/20 each direction. Its ordinary teardown is followed immediately by the
  next fixture controller, without forced LabAP selection, off/on or reboot.
  WPA2 selection at00:09:56UTC also succeeds, DHCP.35,20/20 each direction.
  The180s observer sees55 calls, no Busy, errors0. One recovery-time NotReady
  before upper reservation is distinct from the original Busy failure.

These controls show that neither WPA2 cryptography nor the open->WPA2 order
alone explains the failure. They do not erase the failed first selection in
[the previous qualification](TAHOE_SAE_PEER_RESPONSE_RETRY_20260912.md).

## Overlap q1: the original public failure shape

The same fixture is started without joining it. On the existing WPA3 LabAP
source, the unchanged framework-roam helper requests the other real LabAP BSS
once, followed by one ordinary networksetup WPA2 selection. The helper uses
Apple80211Set107 with its BSSID dictionary; this is not a mouse-driven GUI
test. Its SHA256 is729509f1b5c809f24201eec897d5122bd59b13964e08a9459c3a021798daf8ca.

At00:13:39.940UTC the roam request starts generic-background physical scan467.
The new selection reaches WCL about623ms after that lower start:

1. iwn_scan_abort_command, iwn_wnm_bgscan_abort and
   ieee80211_cancel_wcl_reassoc_bgscan all return0. The old reassociation gets
   its owned failure publication; an abort submission is not a scan terminal.
2. reserveWclPhysicalScan succeeds for upper404 and exact plan staging succeeds.
3. New2GHz background admission passes AUTH-readiness, protected-port and
   pending-BTM gates. iwn_scan_lease_reserve returnsfalse; iwn_scan_start
   returnsEBUSY16 and beginWclBackgroundScan/setWCL_SCAN_REQ return0xe00002d5.
   No2GHz command is submitted for this request.
4. About3ms after that setter return, the framework's5GHz carrier gets upper405
   and submits physical468. It does not replace the rejected2GHz census.
5. The system client reports Could not find network AIAM-UIF3-WPA2 at00:13:43.
   Its process status0 is not join success. Target/security polling fails;
   the AP records no authentication or association for this attempt.

The90s observer ends errors0/calls2/busy1. Controller38662 returns1, while
its fixture restoration returns0. The experiment is a failed target join,
not a successful test merely because the diagnostic completed.

## Overlap q2: exact live lease and its later real terminal

A separately labelled second overlap adds scalar-only lower-live and terminal
probes. No object/request/node/key offsets are read and no kernel state is
written. The source remains LabAPca/channel9; the helper requests02/channel13.

At00:15:48.523UTC a new roam starts physical471. The new native WPA2 request
again cancels that roam successfully. Upper408 and its2GHz plan are admitted,
but **iwn_scan_lease_live_locked returns1 inside lower reservation**, which
returnsfalse. The5GHz request upper409 about3ms later hits the same live lease
and also returnsBusy. No replacement physical scan is submitted in this run.

The actual old471 terminal later enters iwn_scan_lease_finish_terminal, calls
iwn_scan_lease_clear_locked, and returns schedule_replay0. Its clear occurs
about112ms after the first failed setter return. This is the missing handoff:
the physical owner correctly survives until its real terminal, but no new WCL
request remains to resume afterward. Both public subsets have been discarded.

The system client reports network-not-found at00:15:49; AP authentication and
association are absent. Observererrors0/calls2/busy2; controller93516 returns1
and exact host restoration returns0. The original uninstrumented failure
cannot retroactively be assigned every private state value from these runs;
these controls independently prove a concrete cause of the same user-visible
failure, not every possible Busy or connection failure.

## Reference and correction requirements

Updated-Ghidra5995e24caa exports of25C56 startScan and the entire1092-line
startEventScan were reviewed. startScan legitimately returnsBusy while an
action frame is in progress; it otherwise forwards its request into escan.
startEventScan preserves the requested channel plan and propagates the actual
firmware submission result. These functions do not disclose Broadcom's full
firmware scheduler. Do not claim an Apple universal queue/no-Busy policy.

The existing Intel logical cancellation promises replacement of an accepted
roam census, but IWN's abort is asynchronous. setWCL_SCAN_REQ clears the old
logical owner through ieee80211_cancel_wcl_reassoc_bgscan, then immediately
attempts physical reservation. The IWN initial-discovery queue only accepts
an un-aborted generic foreground lease; it cannot preserve this new associated
background request behind the exact cancelled generic-background lease.

Required implementation is an owned deferred start, not a delay or bypass:

- Keep the exact new upper generation and requested channel plan while the
  specifically superseded physical roam scan aborts. Do not queue over active
  authentication/key installation, protected BTM priority, unrelated scans,
  AP transitions, reset or teardown merely because they returnBusy.
- Resume once from the matching old physical terminal on the existing worker.
  Retain association/selected-BSS identity through admission and the actual
  scan command doorbell. Old results and the cancelled roam's terminal are
  not results or completion of the new request.
- Publish STARTED only after the new command is submitted; preserve honest
  rejection/invalidation if it never starts. Keep real terminal ownership
  if a failure occurs after submission. Handle terminal-before-setter-return,
  cancellation, source replacement, S3, reset and detach without stranded
  tickets or a stale callback acting on a successor.
- Audit IWM/IWX's corresponding abort/completion paths too; do not transfer
  IWN radio qualification to them. Preserve the existing initial-scan handoff.
- Execute the production ownership/worker/doorbell boundaries, then repeat
  these exact overlap controls and stable/open-transition controls on the
  built and loaded candidate, followed by STA/AP/off-on/S3/roaming regression.

Reference startScan SHA256:
a7b8d62cf0eb20853c89183e9190c533919f1e22c623df4ea0a445a7dd59e81c.
The accompanying configureScans export sets passive dwell110; that setting is
not evidence for admission or cancellation policy.

## Executed source replay, with deliberately failing requirement

scripts/test_iwn_wcl_scan_abort_gap.sh extracts the complete production
beginWclBackgroundScan and physical lease reserve/abort/clear/live/finish
functions, with the real lease declarations. Three baseline controls pass
ASan/UBSan/Werror on Linux and Tahoe: idle start, rejection of an unrelated
live roam, and the abort-gap failure followed by a different later request.

The command-construction/firmware boundary and channel selection are explicit
doubles. This does not execute the complete iwn_scan_start, terminal receipt
handler or replay worker. The radio traces above independently establish that
the preceding real lower gates passed and the same reservation failed.

The require-handoff mode returns1 on both OSes with:
ABORT_GAP result=0xe00002d5 backend=0 old_serial=471 live=1 submissions=0.
It requires successful deferred ingress with no premature physical submission;
it is a red requirement, not an expected-failure wrapper counted as PASS.
Future ingress success alone will not qualify the deferred worker/doorbell/
terminal path: those full execution gates and on-air controls remain required.
The known-red requirement is not silently included as a green payload test.

Tahoe test tree: /private/var/tmp/scan-abort-gap-tests.s7i1FP. Its five input
hashes match the local tree exactly. The build mirror, loaded bundle and
published ZIP were not modified by this source replay.

## Restoration and evidence

The independent final-restored-q1 check passes20/20 packets each direction
on WPA3 LabAP172.16.66.219. Exact host profile and ordinary power saving are
restored, guest diagnostic USB devices remain present, and no dtrace/tcpdump
remains. No physical10.90.10.22, unrelated QEMU, backing disk or PCI bridge
was changed. The previous sleep/roaming losses remain open.

Working record: /dev/shm/aiam-standard-scan-busy-20260912.YEu9MQ.
Durable archive: /home/dima/Projects/itlwm/aiam-wcl-scan-abort-gap-runtime.ZrwwTt.
All175 entries of EVIDENCE.sha256 verify in both the durable copy and the RAM
original. Manifest SHA256:
8c0610c6fe8ecb79491b1c33afe0711202bc291a356c3cc839e0e85647daa1c1.
Both records are now read/copy-only; the durable directory is read-only.
Next implementation root: /dev/shm/aiam-wcl-background-handoff-20260912.YteT0m.

Overlap q1/q2 trace SHA256:
e681b7667a445732d33256d74afa8c949fa2674d9fa662262e4e931944196f22,
a3dd2c1e992e37fcedd7eedc57c062f3eb85d2531a056dc20cd5ead205e7c32e.
Correlated native airportd log SHA256:
737d7a6f4ef07faa224294bbcf17fe9ab2f7a1bb49defe081e98351cadc078ae.
Passing stable WPA2/open-transition WPA2 controller SHA256:
c73f8c5f1ae18801b2a068d9c5f96278e624876f0b06f63af3133cdf347901ad,
fd960f4e0a3b6b2f15828d923636d7c56a6c64e337fe94edf1455a03c9306690.

## Production deferred-start candidate and executed source gates

The public setter now copies the cancelled roam serial/source epoch before
logical cancellation. An out-of-vtable HAL bridge sends this identity only to
IWN; IWM/IWX retain ordinary admission after their existing exact synchronous
abort wait. The new IWN path queues only behind that aborted generic-background
physical lease, preserving the new upper generation and staged channel plan.
An unrelated scan, AUTH/key gate, protected BTM, AP transition or closed worker
is not converted from Busy into accepted work.

The existing value-only initial handoff slot now also carries background mode
and source epoch. Its matching real terminal, including admission after the
early STOP_SCAN handoff check, or a never-doorbelled predecessor's rollback,
makes the worker runnable. The worker selects the requested eligible band,
reserves a new physical serial and submits an associated WCL scan. Selected-BSS
and physical ownership are revalidated before background flag preparation and
again across the actual command doorbell. STARTED is published after WRPTR;
old scan frames remain excluded by the existing physical-start observation floor.

Public cancellation can revoke a queued IWN token without fabricating a scan
terminal. A racing real STARTED attaches its backend while retaining Aborting;
then the actual command must abort/retire normally. A lower backend that refuses
queued cancellation restores Queued, not Active with backend0. IWM/IWX queued
initial cancellation is not implemented by this IWN correction.

Source replacement cannot restore the old connection's background flags over
its successor. A further negative test caught the old no-doorbell error path
scheduling a redundant fatal reset after hardware reset had already retired
the worker. Deferred background rollback now leaves reset/successor ownership
alone; an error after a real doorbell still requests normal fatal recovery.

Executed on both Linux and Tahoe:

- 46 scenarios extract complete production ingress, pending queue/replay worker,
  scan_start, lease reserve/rollback/terminal/reset, pre/post-doorbell and abort
  bodies, and use the real upper reducer. They cover exact 2GHz/5GHz plans,
  old terminal before/after admission, terminal before setter return, source
  replacement, cancellation around STARTED, reset, stopped worker and an obsolete
  worker returning after a successor starts. Command construction, hardware,
  scheduler and unrelated net80211 calls remain explicit boundary doubles.
- 31 scenarios execute the complete actual command sender and abort/scan hooks;
  12 newly added cases verify deferred source/lease fencing through transport
  wake and actual WRPTR ordering. No handoff-validator double remains there.
- 65,563 actual passive-retry/receipt/doorbell cases and queued-band/RSSI census
  regressions pass. Both full lifecycle and trace scripts, including their full
  payload aggregates and IWM/IWX command/ownership tests, pass on both OSes.
  Existing separate IWM/IWX SAE failure-retirement red requirements are not
  repaired or relabelled by these aggregate results.

The first native package omitted AirportItlwmAgent's test header and stopped
before the aggregate; adding the missing test dependencies fixed packaging,
not production. Static scripts also had pre-existing obsolete IWM/IWX bgscan
signatures; these now match the unchanged two-argument functions. Initial-only
assertions were updated to test both modes and retained cancellation identity.
All intermediate failures are retained, including the negative stale-worker
assertion and its later positive result.

Working evidence remains separate from the frozen RF baseline:
`/dev/shm/aiam-wcl-background-handoff-20260912.YteT0m`.
Native source-only tree: `/private/var/tmp/wcl-background-handoff-tests.OlJ3OE`.
All1022 source/test/dependency inputs verify before and after the final native
run. Their manifest SHA256:
`4caee6318e6e9684042fef3449e76f9795354ba4a3d2ff7c4a3ec0515ae020c1`.
Linux lifecycle/trace log SHA256:
`e6ec2ccb3b386f104484bafc4d20fe138b1bad69b636717ddb0d9b7e99c8f994`,
`6ffcad2f5ff4643e0e6ea415b8c4b7c87e5f5a4d6e7ec9d87ad567368cdaddf5`.
Tahoe final log SHA256:
`19c9a90d5fcda768e2c5c458433650ad773969841851219f211e3e11fbaefe7b`.

The build mirror and loaded842B image were not changed by these checks.
Next: commit/push candidate, build and activate the exact image in the owned
guest, repeat both overlap failures and stable/open-to-WPA2 controls, then
STA/AP/off-on/real-S3/roaming regression. The scan-abort gap is not counted
as a radio-qualified closure until those controls execute.

## Exact candidate build; activation remains next

Source092a7479da8193b40158340f86f2ce76064f7145 was committed and pushed
before build. The unchanged357-file production input set was hashed again;
manifest/source identity:
`410a9b148ba58a872a07b118c148d22a50d1c1546e48ca2a133f680c977f29ea`.
All inputs match before and after xcodebuild in the existing guest mirror.
The ordinary AP-capable Tahoe build succeeds; all1088 imports resolve against
the actual BootKC, with no thread_call_cancel_wait dependency.

Candidate Mach-O UUID: `9B5BCA7A-32FE-393C-ABE6-AD3A83937A9E`.
SHA256: `0a30b252e3536e25b078e83fcab84f9e34a33ca75012a71564d3009e02d1de37`.
Build log SHA256:
`b1b7f855b99136023b95f4c1ddb793cb48557e06c607bf63c316eceb2b684289`.

Before replacing the mirror's staged build output, its verified842B bundle
was preserved as DerivedData-join-failure-20260910/handoff-prior-842b.kext.
Its unchanged binary digest0e0aa25caca2f9ce9b630ad37aa9d9c53730ee939128d4fe30031b86927fcf80
was rechecked afterward. Only build-mirror source and generated artifacts
changed: the guest still loads842B in boot1FB8BDE2-E95D-4B56-8948-A4FCE549D6BD.
No activation, reboot, public release replacement or new radio pass is claimed.
The immediate next gate is private AuxKC admission, then exact-image activation
and the retained overlap/stable/S3/AP regression plan above.

## Loaded candidate and exact two-control radio correction

Private admission completed without canonical mutation. Transaction
activation-20260912T012750Z then preserved the exact five-member AuxKC set,
including unchanged companion identities and timestamped rollback copies.
The owned guest rebooted normally at01:29:00UTC; by01:29:55 it loaded9B5B
in new boot4B344697-7047-476A-9A0C-F08120C85E1E. Installed Mach-O retains
the build's0a30b252 digest above. Physical22, unrelated QEMU and backing
images were not changed.

Native saved-WPA3/DHCP recovery was automatic. The independent first link
check passes20/20 guest-to-host but only19/20 reverse, maximum126.935/25.922ms.
Its strict data gate returns1. This retained failure is not hidden by later
successful fixture traffic and does not establish a new cause of steady loss.

The original two overlap controls are repeated on this image, with one
framework roam to the opposite LabAP BSS and one ordinary WPA2 selection
100ms after the helper returns. No second join request or remedial guest
radio toggle is used. The host's same AX211 fixture stays on channel9 and
restores its exact normal profile after each run.

| Control | Queued upper / old physical | Fresh physical | Requested WPA2 result |
| --- | --- | --- | --- |
| q1, request01:32:31UTC |19 /21|22,2.4GHz|DHCP192.168.73.35;20/20 each way|
| q2, request01:34:53UTC |53 /62|63,2.4GHz|DHCP192.168.73.35;20/20 each way|

Both traces show successful admission while the matching cancelled generic
scan is still live; upper queue acknowledgement returns success. The real
old physical terminal later clears that lease and schedules replay1. Only
then does the worker submit the fresh2.4GHz command. Post-WRPTR STARTED
activates the same queued upper generation with the new backend serial;
its own successful terminal precedes the separate5GHz carrier. The old
terminal is never reported as completion of the new request. Waiting from
setter return to old terminal is about25.48ms and35.57ms respectively.
Each90-second observer records35 ingresses, zeroBusy and zeroDTrace errors.
Both controllers and exact host-profile restorations return0.

The new observer reads scalar API arguments/returns only, not private object
or pending-record offsets. Q1 printed upper uint8 enum returns asuint32;
only their low byte is authoritative. Q2 corrects those casts and narrows
the replay-worker wildcard. Actual queue/doorbell/terminal identities and
all bool results are unaffected; neither observer reads credentials or keys.

Stable native WPA2q1 separately passes DHCP.35 and20/20 each way, without
the injected framework roam. Openq1 -> normal fixture teardown -> WPA2q2
also passes DHCP.26/.35 and20/20 each way, with no extra recovery selection,
radio toggle or reboot. Its90-second observer covers both requested scan
sequences with errors0/calls25/busy0, but ends before final traffic/teardown;
those terminal checks are collected independently by the controller. The
historical baseline used a180-second observer, so full observation windows
are not represented as identical. All control and restoration results are0.

Working evidence remains unfrozen while broader regression proceeds:
`/dev/shm/aiam-wcl-background-handoff-20260912.YteT0m`.
Q1/Q2 trace SHA256:
`04ea4c6e07f9fb17db3cc6e27577025712eedbcb761961c32a5b266b95f903fe`,
`15f38258267e53f0a915085b4556fdc976d31d79569c0d0c8a2f1b13454f2646`.
Q1/Q2 controller SHA256:
`520005e0cdde9b50cfff55b033409def86d5dbd0c4d4ddef641befb70a35e90d`,
`b9d0d203e32b33fc955a8b129b847d5415201245151d4bf90b63bca2ac6c91a4`.
This closes the reproduced lost-census handoff, not all scan Busy responses,
native profile policy, steady losses, seamless roaming or IWM/IWX radio parity.
Off/on, realS3, AP/security and post-S3 roam controls remain before publication.
