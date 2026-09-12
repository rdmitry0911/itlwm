# Post-target reassociation stranded by ordinary cancellation

Status: persistent RF failure and a production-function negative requirement
reproduced on Linux and Tahoe. No production correction or radio recovery is
claimed at this checkpoint. Public alpha remains477ab0af/842B; loaded candidate
is092a7479/9B5B. Physical10.90.10.22 was not touched.

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
