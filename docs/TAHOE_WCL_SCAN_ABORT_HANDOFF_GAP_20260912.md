# WCL discovery lost between roam cancellation and physical scan retirement

Status: **reproduced twice on the published477ab0af/842B image; not fixed**.
The immediate cause is now the live lower scan lease after successful logical
roam cancellation, not an unknown generic Device Busy branch. This affects an
ordinary new-network selection while native roaming is searching. No new kext
is justified by this diagnostic/test commit; the public alpha remains477ab0af.

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
