# Rejected management-frame ownership — 2026-09-11

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

At this source checkpoint the live guest still runs `52a0eeae`, boot
`9358FA34-B833-4F4E-9183-9B8D911F5419`, UUID
`AA1DD77D-223B-330A-9AC4-1C79FD9A8B7B`. Building, preserving that stopped
parent, isolated admission/activation and actual on-air regression are next.
Do not call this new fix loaded or runtime-qualified yet. Public release is
still the qualified `97fe747c` artifact. Physical `.22`, other agents' QEMU
instances, base/working-parent disks and user-owned local `Build/` are untouched.

The original autonomous objective remains active. Continue the full
always-asynchronous source handoff, immutable continuation through selected
node/state/AUTH/ASSOC/key completion, actual producer/management/data/BA/command/
reset retirement for all three families, followed by real GUI/sleep/AP/multi-AP
and IWM/IWX hardware gates. The freshly admitted updated Ghidra and its
unchanged 51-function reference contract are recorded in
`TAHOE_DEFERRED_BSS_IDENTITY_20260911.md`.
