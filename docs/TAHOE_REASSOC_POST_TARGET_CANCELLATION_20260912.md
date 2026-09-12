# Post-target reassociation stranded by ordinary cancellation

Status: persistent RF failure and a production-function negative requirement
reproduced on Linux and Tahoe. A common lifecycle correction is implemented;
source checks, build and activation pass as described below. Public alpha
remains477ab0af/842B; loaded
image is nowc123d132/D636. The first AP/WPA2 regression did not exercise the
post-target edge. A subsequent controlled native leave now observes exact
phase4 retirement and a successful successor roam on radio, but its restored
traffic is19/20 forward,20/20 reverse. Its complete observer ends without
errors. Full recovery/regression qualification remains open.
Physical10.90.10.22 was not touched.

## Qualification before the failure

All following controls use UUID9B5BCA7A-32FE-393C-ABE6-AD3A83937A9E and boot
4B344697-7047-476A-9A0C-F08120C85E1E in the owned IWN/6235 guest.

- The original cancelled-scan/WPA2 overlap passes twice, including actual old
  physical terminal, replay, fresh command doorbell, DHCP and20/20 each way.
- Stable WPA2, open followed by WPA2, and native WPA3 each pass DHCP/20x20.
- Saved WPA3 off/on and the subsequent pre-S3 check pass20x20.
- Actual S3 is observed at01:44:32UTC, wake at01:44:37. Diagnostic USB network
  and tablet were removed for this gate, then restored exactly. The same boot
  returns through Wi-Fi alone, WPA3/DHCP.219 and20x20; no off/on is used.
- Post-S3 AP/WPA3 serves the real external AX211 through SAE/mandatory PMF,
  DHCP,20/20 forward,10/10 cold-ARP reverse and118-byte HTTP via USB/NAT.
  Normal AP stop restores STA and10/10 traffic.
- AP/WPA2 also passes external negotiation, DHCP and both traffic directions,
  including HTTP. Its normal stop fails the separate restored-STA gate.

These controls do not erase the initial new-image20/20 forward,19/20 reverse
failure. A read-only VNC screenshot showed a login/lock screen after S3; it
does not establish either a GUI hang or GUI qualification. Active AP through
sleep and concurrent Wi-Fi STA uplink/AP were not tested here.

## Persistent outage and exact owner

AP/WPA2 stop begins01:48:51UTC. Bridge100 disappears, but the stop helper's
IPv4 lookup at01:49:06 fails: it never reaches its STA ping test. Its failure
cleanup performs another ordinary Internet Sharing disable and restores the
host AX211's exact managed profile. Both actions are retained in the logs.
AP/open is not started; the matrix correctly exits1. Subsequent readbacks
show en1 inactive/noIPv4 on the same boot, while diagnostic USB SSH works.

Serial repeats REASSOC_SUPERSEDE_BUSY error16; airportd receives0xe00002d8.
A90-second read-only observer sees successful ordinary WCL2GHz/5GHz physical
scans, real terminals and no scan Busy. Thus this outage is not another lost
scan-census handoff. No recovery toggle/manual join/reboot is used to hide it.

A separately calibrated60-second observer reads only scalar reassoc fields
at the selected-BSS lock's unlock entry inside the synchronous cancel call.
Eight snapshots all show active1, serial17, leaf4/ROAM_STARTED, source epoch241.
Current epoch advances353->359; every cancel returnsEBUSY16. It ends with
errors0/calls8/locked_snapshots8. Offsets were recovered from disassembly of
the actual9B5B cancel and scan-completion functions; no node/key reads, retained
asynchronous pointers or kernel writes occur.

The retained serial prefix also shows the last target selection to LabAPc9,
channel153: source-DEAUTH ticket4 submits and completes successfully, target
credential generation17 is accepted and DRIVER_RESIDENT_WCL_STARTED appears.
The channel changes13->153->0 before the WPA2 AP carrier/start. This disproves
the initial hypothesis that the latest source-DEAUTH never completed. These
untimestamped serial facts alone do not establish the exact cancellation call
or a causal regression introduced by092a7479.

## Source and reference

The current epoch cancellation explicitly clears only SETUP/SCAN_STARTED/
SCAN_FAILED owners matching their original source epoch. ROAM_STARTED and
on-air phases are preserved. That preservation is necessary for a controlled
source-to-target continuation, but wrong for an ordinary cancellation of the
whole attempt: its source epoch remains historical after target replacement,
and its target continuation is already invalidated. New public joins cannot
cancel post-target owners and therefore keep returning NotReady indefinitely.

The updated-Ghidra5995e24caa exact25C56 exports were read directly:
WCLRoamManager::linkDown atffffff8002105ae4 clears pending roam state and
timer/policy bookkeeping; AppleBCMWLANCore::handleLinkDown atffffff80015be95c
calls RoamAdapter::restoreReassocParams atffffff800152f228. These establish
logical cancellation, not a fabricated physical TX/scan terminal or a fake
received authentication/reassociation result.

Required correction: ordinary association cancellation must retire the exact
current logical roam before revocation callbacks. The identity-checked legacy
source-leave continuation and controlled target replacement must retain their
owner. Preserve lower descriptor/scan lifetimes and reject old completions;
do not clear a successor or publish synthetic0x49/0xcf. All three HAL families
share this common lifecycle, but RF qualification remains IWN-only.

## Executed negative requirement

ROAM_CANCEL_REQUIRE=1 scripts/test_net80211_roam_carrier.sh executes complete
production epoch/newstate functions with an admitted post-target owner17,
source241/current242. Ordinary RUN->INIT advances to243 but leaves active1,
serial17: the required inactive-owner assertion fails with exit134 on both
Linux ASan/UBSan and Tahoe ASan/UBSan. Five further state edges are present in
the requirement matrix but not reached after this first assertion. This is
not a green aggregate gate. The unchanged130-case control passes on both OSes;
its old post-target-preservation cases need to distinguish controlled source
leave from hard cancellation instead of treating them as one operation.

The current fixture doubles crypto, hardware and controller transport. Its
node-join control executes node_join_bss but doubles the separate replacement
epoch helper; it is not full AP or controlled-replacement runtime proof.
Native isolated tree: /private/var/tmp/post-target-cancel-tests.fBnagF.
Its16-file input archive matches localSHA256:
4e9ac505788c842b590d69b50e35de656d81079fac91ccd6c0bdad9f6beb5232.

Evidence working root: /dev/shm/aiam-wcl-background-handoff-20260912.YteT0m.
Durable working copy: /home/dima/Projects/itlwm/aiam-wcl-background-handoff-runtime.lStxhX.
The16,414,777-byte serial prefixSHA256 is
8649f4c028b27a83fa61af03c7aae039bd76379a340ceec7a11e808dd97f331c.
Neither working root is frozen yet. Old completed archives remain read-only.

## Candidate and executed cancellation boundary

ieee80211_wcl_reassoc_cancel_target_epoch_locked retires only the currently
admitted serial in a known post-target phase, with expected-current-epoch
equality under the selected-BSS leaf. The ordinary epoch invalidation calls
it before advancing/revoking credentials. No historical source-epoch equality
is required after controlled target replacement. Logical request/BSSID/phase
and common background markers are cleared; monotonic serial and the real
accepted-scan receipt are retained. No firmware lease or descriptor is cleared.

The identity-checked bss_switch source-leave path explicitly skips this hard
cancellation. The separate controlled replacement helper and forward
SCAN->AUTH->ASSOC->RUN chain also preserve the admitted target. Existing
scan-only cancellation retains its exact source-epoch rules. There is no
change to authentication success, protocol status mapping or a new synthetic
0x49/0xcf publication; independent leave/link-loss still owns its notification.

New executable coverage:

- 30 post-target phase/state hard-cancellation combinations, exercising the
 complete actual epoch and newstate-boundary functions. The original negative
 now passes; replacing only the epoch function with unchangedc4fc9c40 makes
 the same assertion fail again (exit134).
- 145 carrier/bridge/replacement cases, including nine stale/missing identity
 exclusions, real controlled replacement followed by cancellation, nested
 cancellation, successor admission during revocation and forward state-chain
 preservation. The fixture now executes the complete actual replacement
 epoch helper, replacing its former one-line double. Credential/PMF callbacks,
 controller transport and firmware remain explicit fixture boundaries.
- 25 complete common reassociation admission/abort/retirement/controller-gate
 cases. New cases compose hard cancellation with actual successor admission,
 reject the old scan/owned failure and reject an already queued old controller
 completion. The separate epoch fixture covers the full epoch implementation.

The old eight scan-only exclusion cases now call that actual narrow helper
directly under its lock. They no longer incorrectly require an ordinary hard
cancellation to preserve a post-target request. The controlled source-leave
tests explicitly assert preservation and continued identity validity instead.
This is a corrected distinction between two operations, not a relaxed
requirement that allows a stranded owner.

Linux and Tahoe final lifecycle and trace gates all pass, each including its
complete payload aggregate. Final native rerun79652 exits0, including the
three delayed-controller cases. All1022 native input hashes match before
and after those tests. Native final logSHA256:
aca75ac4d093607c13b68d14023bc07405f52559a6b5d93eb5dac46ee4c3ab1c.
Final source/test/dependency manifest (1022 inputs) SHA256:
c9360d2af789b092d2c8a98f2160425cb405bb349859a6de0675747be816fac5.
Native isolated full tree: /private/var/tmp/post-target-cancel-full.g3ve0C.
Linux final lifecycle/trace log SHA256:
6d923389a168f14f38b3b27a40f348eb5beb11f10bc1167aa075ed65e3db694c,
cab58a563d2dfa73cf8c07ecdcafc58d48a430b932f9c95e8f57d8d2e7d591b8.
Negative epoch-only replaySHA256:
26d898dd7957e44ac501fc9ac40ff45df4dbe90977f5950f091d3584fd953f14.

The live failed guest is intentionally unchanged during source tests/build
preparation. AP/open, the final9B5B roaming pair, new-image loaded recovery and
repeatable controlled post-target cancellation/AP regression are still open.
IWM/IWX share the common correction, not IWN hardware evidence; their separate
SAE failure-cleanup requirements and other deferred-BSS liveness gaps remain.

## Built candidate, before activation

Productionc123d132aad014356aa6a8864a731c58ae4f6764 is committed and pushed.
The full357-file9B5B predecessor was verified in the Tahoe build mirror before
copying the exact changed files. The old9B5B bundle is retained at
DerivedData-join-failure-20260910/post-target-prior-9b5b.kext. The new full
production manifest verifies before and after an ordinary AP-capable build.
All1088 imports resolve against the actual BootKC; no thread_call_cancel_wait.

- Source manifestSHA256:775c161be6c13198f639d9bc05c076a190fc713198d4dbc8c7a2f374b314610f.
- Embedded source ID:775c161be6c1.
- Mach-O UUID:D636A28B-6A9B-3CCE-AF28-5779C5F980C8.
- Mach-O SHA256:9e06b1aab08ff2297d2951be20cd26a8fc19f8923aef5cb4700e86576ce5c6d8.
- Build wrapper logSHA256:7b2cb1530dbc10f2268901dd44e326979f2a81827ba4d6071ed7b0541e015dc6.

Build session20649 exits0. This is not a loaded-image or RF correction claim.
New runtime working root:/dev/shm/aiam-post-target-runtime-20260912.TpUKkd;
durable working copy:/home/dima/Projects/itlwm/aiam-post-target-runtime.0ybGDl.
Private activation root:/private/var/tmp/aiam-iwn-post-target-activation.iixXcs.
The prior source/RF evidence root remains separate and not yet frozen.

## Loaded D636 and first AP control: regression pass, overlap not exercised

Private preflight35304 exits0 with five exact AuxKC members and no canonical
mutation. Transactional activation62299 exits0, preserving companion rows
and rollback copies. Transaction:activation-20260912T022509Z. A normal owned
guest reboot is requested02:26:30UTC; session41106 verifies D636 loaded at
02:27:24 with new boot900A4367-43E5-42DC-8DC4-C0662A9E954E. There is initially
no IPv4 in that first snapshot. By02:28:01 ordinary automatic recovery reaches
WPA3/DHCP172.16.66.219; no manual selection or off/on was used.

loaded-baseline-q1 fails its strict data gate:20/20 forward,19/20 reverse.
Its controller returns1. This remains a separate data-loss limitation and is
not relabeled by the later AP pass.

Observed cold AP/WPA2 q1 runs02:30:49->02:32:33UTC. Real external AX211 gets
WPA2, DHCP192.168.2.2,20/20 forward,10/10 cold-ARP reverse and exact118-byte
HTTP through USB/NAT. Normal AP stop removes bridge100, STA recovers, and
10/10 guest-to-gateway packets pass. Exact host profile and wired management
are restored. The controller and owning wrapper99855 both return0.

The full180-second read-only observer ends errors0/calls13/**retired0**.
All observed hard-cancel entries have no active roam. A later real target
join starts after the AP stop, not during cancellation. Therefore this is a
valid AP/service/restoration regression pass, **not an on-air proof that the
specific stranded post-target owner has been repaired**. A controlled native
roam/cancellation overlap remains the immediate next gate, followed by the
remaining security/scan/off-on/S3/AP/roam matrix. Nothing was published yet.

The current-image target-cancel/clear functions independently confirm the
scalar observer offsets. The initial llvm-objdump invocation ignored its
symbol-only filter and retained69,801,026 bytes of full disassembly; the exact
two relevant bodies were subsequently located and inspected. The observer
reads only synchronous, lock-held scalar owner fields, never nodes or keys.
Do not mistake its zero errors for coverage: retired0 explicitly marks the
unexercised edge.

Trace/controller/initial-loss logSHA256:
18e377c7b4b4b538bbf8b31bb8c7d0fc62e73f2acac68c2fd42de0057df5a6d0,
460dbdee89a6b3d100ec878157d1aebb9667325152fd766463f0a607276fd61a,
eabf2d8fb65b9687279335f156a114e896e407652c96fcd0036d89fe0d8c344f.

## Controlled held-peer cancellation: AP noncoverage and native-leave evidence

The unchanged D636 image and boot900A are retained. The owned AX211 advertises
LabAP/channel9, SAE/group19/CCMP/PMF-required with the existing LabAP credential.
Its external-management controller omits received Commits only from the exact
guest MAC4e:bc:8d:ff:50:23, forwarding other management frames and actual TX
statuses. It does not manufacture auth bodies, driver owners or terminals.
The existing framework-roam helper requests its real BSSID80:e4:ba:20:ef:f9.
Wired management and guest USB diagnostics remain independent of both radios.

AP overlap q2 starts the roam02:47:32.805UTC. The driver admits its target;
hostapd receives and holds the first Commit02:47:38.640. Ordinary Internet
Sharing enable is requested02:47:38.723; bridge100/ap1 are active02:47:42.
Two identical Commits are captured on the monitor and reported by hostapd.
Nevertheless the full240-second observer ends errors0/calls19/retired0:
no active post-target owner reaches the new hard-cancel helper. Therefore the
controller returns1 at its exact-coverage assertion before any AP client test.
Failure cleanup disables Sharing, restores the host profile and leaves guest
WiFi recovered. This is a missed cancellation edge, not an AP service pass
or a demonstrated hard-cancellation failure. The trace does not locate the
other retirement path and cannot establish why AP enable omitted this edge.

Native-leave q3 replaces that ambiguous AP action with the ordinary public
CoreWLAN disassociate method on en1. The small userland helper links the installed
Foundation/CoreWLAN SDK; it does not call private driver functions or report
its void return as successful recovery. SourceSHA256:
e0b89854a631c5c9e035404049f06a497eef006b91f11b735ab9b16295b70bc3.

- Target start returns1 at02:52:04.236UTC; hostapd holds the actual guest
  Commit02:52:04.393. The controller requests native leave02:52:04.401.
- At02:52:04.661 the actual setWCL_LEAVE_NETWORK enters; the selected-lock
  snapshot reads active1/serial5/phase4/source51/current52/expected52.
  clear_locked is called by cancel_target_epoch_locked, and its return reads
  active0/serial0/phase0. Thus historical-source/current-target cancellation
  is now observed on the real loaded driver.
- Without off/on or reboot, native recovery obtains WPA3/DHCP172.16.66.212
  at02:52:30 using a different private MAC. The later real roam serial6
  completes successfully at02:52:53.551; readback identifies BSSID
  50:4f:3b:cd:dd:66/channel5. This is a successor-admission success, not proof
  that the original LabAP profile was recovered. The check helper only gates
  security/subnet, so its labap argument must not be treated as an SSID check.
- Restored1400-byte traffic is19/20 forward and20/20 reverse; forward seq0
  is missing. The strict data gate fails. This loss is retained independently
  of the exact logical-retirement success.
- The monitor ends with0 captured,1 received-by-filter,0 dropped: stopping
  it immediately after the short exchange did not drain the buffered record.
  Hostapd's received-management event is real RF evidence, but q3 has no
  independent captured Authentication body. A later separate control must
  allow the monitor buffer to drain; q3 is not relabelled.

Q2 complete traceSHA256:
d5143e8ef383ab4503944bd57ba2c3728cc0687033e4f68e4ace3eb02bd3d7e2.
Q3 peer log/data-gateSHA256:
aa20831deab735718016b58aad0d3de3a863c2eb2ba2c47678cf5ebfd79cb967,
38431dffdc71fbac2b5ab3ed0f776237b6af6779f7586e33162d3ecb065c9144.
Q3 observer/controller66226 is now terminal: observer0, errors0/calls16/
retired1; overall controller1 preserves the failed strict data gate. No new
stimulus was issued during the remainder of its240-second observation.
Complete traceSHA256:
eb420ddcc4a3498fa660c3d5496a2a1e5671572b1fbc59f4100dd6fcf09244c3.
A separately labelled ordinary LabAP request at02:56:14->02:56:23 restores
DHCP.219 and02/channel13 before the next control; it is not retroactively
part of q3's automatic recovery. The next separately labelled control tests
an explicit successor LabAP selection after exact cancellation and verifies
its BSSID/address as well as traffic.

Q4 also reaches an actual held target Commit and native leave: at
02:56:52.548UTC the helper clears active1/serial7/phase4/source78/current79.
The complete120-second observer ends0, errors0/calls18/retired1. A subsequent
ordinary roam serial8 completes successfully at02:57:38.218. However the
initial framework-roam process reports16/Busy: the actual target attempt was
already admitted independently. Its nonzero wait triggers controller cleanup
before the planned successor networksetup request. The overall controller is1,
not a passing explicit-roam-request or successor-selection test. The proposed
two-second monitor drain is also not reached on that failure path, so the
empty capture is retained. Q4 supplies another actual hard-cancel/successor
observation, but cannot substitute for the separately planned explicit request.
Trace/request logSHA256:
53ff942a8e33d79b987f840db6a163f4c44bd8b325f2f2dccecc1454f45406da,
82c156887e47df2232c37e7be983a4263b073053dfd54fa901a1d59fa4172a1e.

After q4's terminal, the separately labelled explicit-successor-q1 issues one
ordinary networksetup LabAP request02:58:59->02:59:07. No off/on, second request
or reboot is used. WPA3/DHCP.219 returns; the post-data readback confirms
02/channel13 on the same boot/image. Traffic is20/20 forward with one packet
outside the one-second wait threshold (maximum1130.395ms),19/20 reverse.
Thus the target-selection/DHCP result succeeds but the strict data controller
returns1. Its later in-script BSSID assertion was not reached after that
failure; a separate read-only02:59:51 snapshot establishes the actual BSSID.
Neither this follow-up nor q3/q4 qualifies lossless reconnect or all saved
profile policy. They do establish real post-target logical retirement and
successful subsequent native admissions; broader D636 regression continues.
Controller/readbackSHA256:
f0487952a01a8a749c5c357b9b25ea47fe5fb363a3401b3452a5363e4e8b011a,
56bba1348c5d93e24db1a1b99688763cc3ee9c01b1c4cc4f7ee63ecbfb1a82c4.
