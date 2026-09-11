# Rejected management-frame ownership — 2026-09-11

Latest: production `20dd3d8a` is pushed, built and loaded on the disposable
IWN/6235 guest. Actual WPA3/S3/SAE-failure/WPA2/open checks below preserve
service, but include packet loss, long RTTs and a first open-selection miss.
They do not qualify the whole reconnect surface or a replacement release.

## Scope and actual correction

While following the outstanding asynchronous BSS handoff/source-drain gate,
the complete production software-queue path exposed another stranded owner.
`mq_enqueue()` frees a rejected packet under the queue lock, but cannot release
its net80211 node reference. `ieee80211_mgmt_output()` nevertheless returned
zero, invoked `if_start`, and armed the interface timer. Consequently its
callers `ieee80211_send_mgmt()` and
`ieee80211_send_bss_transition_response()` did not execute their error-path
node release. AUTH/ASSOC/probe requests also armed `ic_mgt_timer` despite having
no transmitted packet and no future TX completion.

The shared production output now returns `ENOBUFS` after the existing
drop diagnostics and before `if_timer`/`if_start`. The caller retains sole
responsibility for releasing its reference. No extra packet free or node
release occurs inside `mgmt_output`, and accepted management/BAR behavior is
unchanged. The common change covers IWN/IWM/IWX, including the actual WNM BTM
response producer; it is not a new hardware-family qualification.

This is a required software-producer edge within source retirement, **not**
the full asynchronous BSS handoff. The two deferred-liveness red cases remain
open. Generic management-queue purge paths also need node-aware cancellation
and must not synchronously reenter an obsolete state transition. No attribution
of the prior real RF packet loss/one-second stalls to queue overflow is claimed:
those observations still need a correlated radio/queue trace.

## Executed tests

`scripts/test_net80211_mgmt_queue_ownership.sh` extracts complete unchanged
production bodies for `mq_enqueue`, its list operations, `send_mgmt`,
`mgmt_output`, the BTM response producer, compressed BAR enqueue, and actual
node ref/release. Packet allocation/frame builders/physical completion and
single-thread scheduling are explicit fixture boundaries, not a hardware test.

The pre-fix AUTH case reports `result=0 refs=1 starts=1 queue=1 timer=5` and
fails the required rejection assertion with exit 134. The pre-existing queued
packet still owns its own separate reference. Its expected-defect wrapper
exits zero only after observing that failure; this is not a passing gate.

All **22 scenarios pass under ASan/UBSan on Linux and macOS** after the fix:
accepted queued/inline completion; rejected AUTH, DEAUTH, ASSOC, REASSOC,
ACTION, probe, disassociation; allocation/prepend failure; preserving two
existing references; BAR success/drop; preserving an existing management
timer for successful DEAUTH; BTM accept/reject success/drop and invalid,
allocation-failed, prepend-failed BTM. Rejections neither publish accepted
enqueue markers nor start TX/timers. Node release callbacks run after the
queue lock is released; the existing queued node remains untouched.

Linux full payload aggregate passes. macOS selected adjacent gates pass:
118 actual roam-carrier/epoch cases, 22 reassociation failure-retirement cases,
25 positive deferred-identity cases and physical TX retirement across all
three HALs. These do not turn the two separately red deferred-liveness cases
green or establish successful multi-BSS legacy roaming.

An extra post-PLTI trace contract initially selected the now-empty wrapper
using an ambiguous `ieee80211_end_scan` prefix. It now inspects the exact
production `ieee80211_end_scan_owned(` body and additionally requires trace
publication after physical-owner admission. Its management check now requires
the rejection before timer/start. The corrected contract passes on both OSes.
The first macOS fixture compile exposed the missing fixture-only `htole16`
definition; the test now uses the platform endian helper. Neither issue was
treated as a production pass before rerunning.

Evidence hashes:

- Baseline `/tmp/aiam-mgmt-queue-baseline-20260911.log`:
  `e1b5c2d7f7a93f12781e427d4d67a2a3b08787e6f32a62005119dfe7fe06583b`.
- Linux 22-case `/tmp/aiam-mgmt-queue-fixed-linux-20260911-r3.log`:
  `1dc1f430a490bee131df9919f4d38f985221543d6f7af39f213aec875f0c8a20`.
- macOS 22-case `/tmp/aiam-mgmt-queue-fixed-macos-20260911-r3.log`:
  `3c78f4dac4752e6916080c4a3909101785c5eebeee2111203ab05666539e2bf8`.
- Linux aggregate `/tmp/aiam-mgmt-queue-linux-full-20260911.log`:
  `c7d983e63adef87f93f52326f461f296cecc999853a5d8cac795143f7ea32bcf`.
  This ran before adding the seven BTM cases; the separate 22-case rerun
  includes those cases. The same production guard is used in both runs.
- macOS adjacent `/tmp/aiam-mgmt-queue-macos-selected-20260911-r2.log`:
  `a61a4ded0bdd89a7faead12059f77cbc74a701e52a142478803a33f4803207fc`.
- Linux corrected trace contract:
  `4584cce67a11658bea0455243dcbb0e752d8bb0488e6d4a68b860c4c3651b566`.

## Build/runtime checkpoint

Complete production source-manifest digest:
`1c211a777af676b5664e4fa2da3b4e98b92af6d327004f0d14037d5e5da831d5`;
build source ID `1c211a777af6`. New evidence root:
`/home/dima/Projects/itlwm/aiam-iwn-mgmt-queue-runtime.5zvejm`.

At the historical source checkpoint the live guest still ran `52a0eeae`, boot
`9358FA34-B833-4F4E-9183-9B8D911F5419`, UUID
`AA1DD77D-223B-330A-9AC4-1C79FD9A8B7B`. The following later admission and
runtime record supersedes that not-yet-loaded checkpoint. Public release is
still the qualified `97fe747c` artifact. Physical `.22`, other agents' QEMU
instances, base/working-parent disks and user-owned local `Build/` are untouched.

The original autonomous objective remains active. Continue the full
always-asynchronous source handoff, immutable continuation through selected
node/state/AUTH/ASSOC/key completion, actual producer/management/data/BA/command/
reset retirement for all three families, followed by real GUI/sleep/AP/multi-AP
and IWM/IWX hardware gates. The freshly admitted updated Ghidra and its
unchanged 51-function reference contract are recorded in
`TAHOE_DEFERRED_BSS_IDENTITY_20260911.md`.

## Exact build, preserved parent and activation

Production commit: `20dd3d8a6487ccf549a4e322ebc42003a4469d52`. The complete
production manifest above passed on the guest source mirror; its older Git
HEAD is not used as build identity. Tahoe build succeeded, all 1085 target
BootKC symbols resolved, with no `thread_call_cancel_wait` dependency.

- Build log `/tmp/aiam-mgmt-queue-build-20260911.log`, SHA-256
  `cbf9caf39ca5354d55f398732aba0dc63658b913151501aac809218b7ae5b7dc`.
- Mach-O SHA-256:
  `ce03f7c2d130b8ceea90491114bc031cb4b7083fa98e093df0ddca2ccf21e878`.
- Mach-O UUID: `4B2B526E-9D1D-3C51-9509-0C11C7BE30E9`.
- Loaded boot: `C7693AA2-C5CD-4515-B7AA-508BAB9344F6`.

The previous owned guest shut down normally at 07:16:04 UTC; serial reached
power-off and its exact QEMU process exited. No forced termination occurred.
Preserved read-only parent:
`/home/dima/Projects/itlwm/aiam-iwn-bss-identity-runtime.mbFA1U/tahoe-bss-identity.qcow2`,
length 1,143,734,272 bytes, SHA-256
`421daa36589fe6a27d8fe6e1a33a830dea710d18180b33913400f04e08edb79a`.
Its OVMF variables hash is
`8334cff992348f90978884a91e967bbc36ccc6481ba9030fcfd6f67b694b2b65`.
Both still match `preserved-parent.sha256` after the new runtime suite.

The new writable child is `tahoe-mgmt-queue.qcow2` in the evidence root,
with its own copied variables. Exact QEMU PID 1287162 uses that child, IWN
VFIO `0000:25:00.0`, SSH 3338, and the existing owned monitor. Base disks,
PCI bridge and unrelated VMs were not changed. Space required a backed child,
not a full byte copy of the parent.

The frozen private candidate under
`/private/var/tmp/aiam-iwn-activation-mgmt-queue-1c211a777af6-1`
passed isolated five-member AuxKC admission before canonical mutation.
Transaction `activation-20260911T072132Z` reached READY with unchanged
companion manifest SHA-256
`ad48f98c0cc7622251dc125812f6e2853e8063e61e7fc2fa6a8b966a11a1cbfa`.
One normal guest reboot at 07:22:16 loaded the UUID above. The saved WPA3
profile recovered automatically; DHCP lease began at 07:22:54. There was no
extra selection or radio toggle to obtain that initial connection.

## Actual on-air regression, same loaded image

All traffic probes below have 1400-byte ICMP payloads. Forward is guest to
the router/AP; reverse is the physical host AX211 to guest. Profile selections
and off/on use native networksetup/CoreWLAN, **not GUI automation**.

| Case | Forward / reverse | Maximum RTT, ms | Qualification boundary |
| --- | --- | --- | --- |
| Initial saved WPA3 | 20/20, 20/20 | 23.105 / 17.502 | Automatic after candidate reboot |
| Actual S3, saved WPA3 | 20/20, **19/20** | 32.344 / 20.566 | No Ethernet, no join or toggle after wake |
| After controlled SAE failure | 20/20, 20/20 | 23.539 / 17.095 | Valid BSS restored; bad BSS was reselected during observation |
| Post-S3 WPA2 selection | **19/20**, 20/20 | 804.760 / 1003.690 | Actual RSN handshake and DHCP |
| One WPA2 off/on | **19/20**, **19/20** | 800.030 / 212.644 | Same profile/private MAC/address restored |
| Open after separate scan and retry | 20/20, 20/20 | 769.256 / 1004.025 | First selection failed; not a first-attempt pass |
| Final saved WPA3 return | 10/10, 10/10 | 33.919 / 24.769 | Automatic after fixture stop |

Later success does not erase the losses or first-selection miss. The prior
`52a0eeae` suite also had loss and long RTTs; this suite does not attribute
those observations to the corrected queue overflow. No actual on-air queue
overflow was forced or observed. The ASan/UBSan production fixture proves
that failure branch; these radio checks cover ordinary-path regressions.

### S3 admission and real Wi-Fi-only recovery

The first helper (PID 522) terminated at its Ethernet-presence guard because
an attempted non-sudo monitor connection was denied. No sleep was requested
by that helper; it is not an S3 failure or pass. After verifying its terminal
state, only the exact temporary management device `bss94_diag_usb` was
removed with the owned privileged monitor. Direct Wi-Fi SSH confirmed that
en0/en2 and the USB tablet were absent; only the boot keyboard remained.

The separately logged second helper requested sleep at 07:31:12 UTC.
The initial status query still showed running; a later query in
`s3-status-r3.log` showed `paused (suspended)` and serial reached `ACPI SLEEP`.
One `system_wakeup` at 07:32:14 produced `ACPI S3 WAKE`; native pmset records
one Normal Sleep/Wake and a 30-second WindowServer sleep-ack timeout.
DHCP lease publication followed at 07:32:24. Direct Wi-Fi SSH confirmed the
same boot/image, WPA3_SAE and no Ethernet. The S3 traffic row completed before
management USB was restored. This is actual network-service recovery through
S3, not proof of post-S3 GUI responsiveness or active-AP continuity.

### Actual received SAE rejection and repeated bad-BSS selection

The wrong-password pure-SAE/required-PMF LabAP on the host was ready at
07:33:56. One directed native request was accepted at 07:34:11, with no
resubmission. Observer `mgmt20dd-posts3-auth1-observer.log` ended normally
at 07:35:22 with errors=0. It recorded four real empty-body Confirm status-1
rejections from `80:e4:ba:20:ef:fa`:

- 07:34:16, epoch 31/relay 5: fresh-join claim generation 0, generic scan.
- 07:34:26, epoch 46/relay 6: generation 3, cleanup participants 1/2/4,
  exactly one failure publication returning zero.
- 07:34:58, epoch 52/relay 8: generation 0 again, generic scan.
- 07:35:07, epoch 69/relay 9: generation 5, cleanup participants 1/2/4,
  exactly one failure publication returning zero.

The valid BSS `9a:fb:5d:97:a9:02` completed SAE at epochs 51 and 74.
The second bad-BSS cycle occurred without another manual directed request.
Thus fresh-AUTH error cleanup regresses correctly, but failed-BSS selection
and the distinct non-fresh roaming terminal remain open. A generation-zero
case is not fixed by inventing a JoinAdapter owner or fabricating reassoc
`0x49`; updated-Ghidra `handleAuthEvent` independently confirms separate
`0x4a` AUTH observation before JoinAdapter dispatch.

Observer SHA-256:
`62771c38a941399c0d9a5e8ff86d2dc25eeb25a3348520fc680b6f81d2394582`.
Normal fixture cleanup at 07:35:28 restored host managed Wi-Fi.

### WPA2, off/on and first open-selection miss

Native WPA2 selection at 07:37:03 obtained external four-way completion and
DHCPACK at 07:37:18 for private MAC `5e:bf:a9:68:58:f9`, address
`192.168.73.35`. One guarded off/on at 07:38:14/15 explicitly observed
Off/inactive/no IPv4, then recovered that same WPA2 profile/address at
07:38:17 without another join request.
The corresponding rows retain all loss and latency.

The open AP was ready at 07:39:25 on channel 9 with the same fixture BSSID.
The first native selection at 07:39:37 returned
`Could not find network AIAM-UIF3-OPEN.` The guest remained/recovered on WPA3;
no open DHCPACK occurred. Airportd's original log shows the 13-channel 2-GHz
subset, **including channel 9**, returning zero matching results in 0.4708 s,
then the 24-channel 5-GHz subset returning zero in 3.5185 s. Consequently
this is not evidence that native userspace requested only 5-GHz channels.
That log alone does not prove every requested channel was visited physically
or distinguish missing RX, stale cache and premature completion.

At 07:41:09 a separate directed public CoreWLAN scan returned one open result
on channel 9. Only after that completed diagnostic was one new native
selection made at 07:41:48. It obtained external association and DHCPACK at 07:42:00
for private MAC `52:a4:38:c8:f4:62`, address `192.168.73.26`, security NONE.
The later successful probes are explicitly the scan-assisted retry, not
retroactive qualification of the first attempt.

- Original airportd log `open-first-attempt-airportd.log`, SHA-256
  `66dd7985c02d8c73da3e64871d8ace8e049c72e0da92cd3d198ecb0b9d2cc7a9`.
- Directed scan `open-directed-scan.log`, SHA-256
  `92a107fe08c92fa9c9aa838f0f83b72e7b5b3ab2e19da4ef78f507914dacf8d8`.

The identical first-open omission already appears, explicitly unassigned,
in `TAHOE_SCAN_SSID_REFRESH_20260909.md`; do not conflate it with that
separately demonstrated and corrected first-seen-only SSID bug. The next
high-user-frequency runtime gate is to correlate a first native selection
with exact physical scan ownership, channel construction, candidate RX and
public result completion, before applying a source correction. The full
asynchronous source-handoff/command-drain work remains required as well.

## Final state and retained evidence

All three host RF fixture controllers reached their normal cleanup paths;
the explicitly stopped WPA2/open wrappers returned 130 as designed. No
uif3ap/uif3mon or observer remains. Host managed LabAP and wired default via
172.16.16.1 on `enx1cbfce6c92ea` are restored. The guest returned automatically
to saved WPA3 at 172.16.66.219 with the same boot/image and management en2.
No new guest-AP, GUI, IBSS, concurrent STA/AP or IWM/IWX hardware pass is claimed.

The 9,554-line serial snapshot `serial-through-sta-regressions.log` has SHA-256
`e6730365b10564575a5382c5e58461c15a55e1aced5e99353139a5292e738808`.
A bounded text scan found no matching kernel-panic, fatal-firmware,
device-timeout or `dropped=1` markers; this is not proof of absent latent
faults. The immutable per-log manifest `sta-runtime-evidence.sha256` hashes to
`7005285f8ca695d51825d2d231c7f47b6ca2e2c074bbd5928c13bad4b4d70128`.
The public release remains `97fe747c`; mixed and incomplete results do not
justify silently replacing the previously qualified artifact.

## Recoverable space reclamation

Two old offline leaf overlays were byte-copied with rsync -aSz to
`dima@10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/mgmt-queue-space-20260911.vFllOp/`.
For each, source/remote lengths and SHA-256 matched independently, a fresh
owned-image backing census found no child, and a final fuser/source-hash check
preceded deletion of only that exact local file:

| Original local file under `/home/dima/Projects/itlwm/` | Bytes | SHA-256 |
| --- | ---: | --- |
| `overlay-bssmgr-20260711T092837Z.qcow2` | 245170176 | `1469a1c5222d56a33b221752c116e96df8603f3d41bcf3a188460e95723a0b0e` |
| `overlays/overlay-live-0b90ed7-20260721T023034Z.qcow2` | 423690240 | `c58b408fa8e38f6759dc03a61c5a311c31f1ec3641ba964ed20ca8584ffc846b` |

The similarly named `fixture` overlay was not removed. Restore an archived
file to its original absent/unused path with rsync -aS, retaining the original
backing chain, and recheck its hash before use. The archive directory is not
an independent boot kit. Working parent/base disks and user-owned `Build/`
were not removed or modified.
