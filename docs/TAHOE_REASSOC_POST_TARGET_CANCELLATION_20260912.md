# Post-target reassociation stranded by ordinary cancellation

Status: persistent RF failure and a production-function negative requirement
reproduced on Linux and Tahoe. A common lifecycle correction is implemented;
executed source checks are described below. Build, activation and the radio
correction gate remain required. Public alpha remains477ab0af/842B; loaded
candidate is092a7479/9B5B. Physical10.90.10.22 was not touched.

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
