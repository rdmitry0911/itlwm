# Deferred BSS switch: real lifetime failures — 2026-09-11

## Result and scope

Correction from the complete post-copy/TX execution below: lateattach's one
reference does **not** survive the real `ieee80211_node_copy()`. That function
copies the cached node's reference counter too, usually zero. The original
pre-copy fixture is valid only at that boundary, not a model of an established
STA or proof of a universal zero-reference liveness failure. The current
production callback also runs before IWN finishes retiring its descriptor.
Neither fact permits using a reference threshold as the hardware drain fence.

The in-progress correction detaches callback and argument before release or
cleanup can reenter, and passes the argument explicitly. The successor-rearm
case now passes; old-owner identity, early physical delivery and full roaming
remain open. This source checkpoint is not yet loaded or released.

The physical abort/SCAN-doorbell correction is pushed as `52dd156d` and loaded
on Tahoe with UUID `D63CF6F8-85F5-3C98-B3E6-7289BEFD5A19`; its later
[bounded live runtime](TAHOE_ROAM_SCAN_PHYSICAL_RUNTIME_20260911.md) preserves
SAE failure recovery. Public release remains the qualified `97fe747c` image.
This next checkpoint adds executable negative evidence for the same open
reconnect/roaming layer, not a completed fix or another release.

The reference remains the exact 25C56 Core/WCL lifetime documented in
[the 51-function reconstruction](TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md):
one admitted roam retains source/target identity across real lower acceptance,
scan/preparation, actual AUTH/ASSOC results and overall completion. It is not
a fresh JoinAdapter request and does not complete via a generic `0xcf` error.
The new finding is in our local lower-to-common BSS-switch bridge; no claim is
made that Apple uses our node/refcount implementation.

## Executed production, not an inferred zero-reference constant

`scripts/test_reassoc_deferred_bss.sh` extracts the complete, unchanged
`ieee80211_node_lateattach`, `ieee80211_node_switch_bss`, and
`ieee80211_release_node` bodies from the current source, plus the complete
production incref/decref/ref helpers and deferred-argument declaration.

Actual lateattach creates one `ic_bss` reference. The original fixture then
models one transient TX reference using the real primitive and invokes the
complete release function. Physical TX/IRQ scheduling, node lookup, the inner
node-join/cleanup machinery and the common failure-publication callback are
explicit fixture boundaries. This is not an on-air reproduction or proof that
all driver DMA/command ownership is represented by the node counter.

Ordinary non-BSS zero-reference delivery and direct valid switch-callback
controls pass. Four independent required-behavior assertions fail on both
Linux and macOS under ASan/UBSan, each exit 134:

1. **Before a selected-node copy, the callback waits at one reference.** Actual
   lateattach leaves `ic_bss` at one reference. After the modeled TX reference
   is released, the observed state is `refs=1 joins=0 callback_pending=1`.
   WCL's non-pure-SAE path and legacy background roam arm this callback on
   `source=ic_bss`, but a real preceding node copy changes its reference count
   (see the correction below). This pre-copy result must not be generalized
   to established-link liveness. Pure-SAE retarget bypasses this callback.
2. **An old callback retires a later accepted roam.** The argument retains only
   source/target MAC addresses, not the admitted serial or epoch. After a
   replacement with serial 32 and cancellation of the old BGSCAN flag, direct
   delayed delivery calls failure against the current owner: `active=0`, one
   failure, current serial still 32.
3. **An old failure continuation requests SCAN after a successor is admitted.**
   A missing target invokes common failure; the explicit callback boundary
   admits serial 32. On return, the unchanged deferred-switch function still
   calls `ieee80211_new_state(SCAN)`: the new request's state becomes SCAN.
4. **Release erases a callback installed during callback delivery.** The complete
   release function invokes its callback and then unconditionally nulls
   `ni_unref_cb` and argument fields. A reentrant successor callback installed
   at that boundary is lost. This uses an explicit callback substitution to
   isolate the actual release ordering, not a claim of an observed RF race.

Default test mode is intentionally red. `BSS_SWITCH_EXPECT_DEFECTS=1` checks
the remaining required-behavior failures; its wrapper exit zero does not
qualify the driver. `BSS_SWITCH_PASSING_ONLY=1` includes only ordinary controls
and corrected callback detach/rearm/cleanup checks in the aggregate. The full
deferred lifetime remains a separate red gate.

Evidence:

- `/tmp/aiam-reassoc-deferred-bss-baseline-20260911.log`, SHA-256
  `a2ec9e5534ad909ab5f278cfe77ecd78cb106509fcd9f47396aaac7b01fd178f`.
- `/tmp/aiam-reassoc-deferred-bss-macos-20260911.log`, SHA-256
  `3da184b1e167335822085a9e439e4be71dc485ee21c85c9df1eebfcbaf616737`.

## Complete post-copy and physical TX boundary

The fixture now also extracts complete, unchanged production `node_copy`,
`node_cleanup_internal`, `iwn_tx_done` and `iwn_tx_done_free_txdata` bodies.
Allocation/key/BA cleanup and AP/rate-control helpers are explicit boundaries;
firmware notification delivery and inner node-join effects remain modeled.
This is executable production evidence, not an on-air failure assertion.

- After actual lateattach then actual node_copy from a zero-ref cached BSS,
  the destination has `refs=0`. One real ref/release invokes the switch:
  `actual post-copy release: refs=0 joins=1`. This positive control changes
  the diagnosis from the earlier pre-copy-only test.
- With that same real copy and a submitted source descriptor, the complete
  IWN terminal reaches the switch with `queued=1 node_live=1 length=1400`.
  Only after the switch returns do the descriptor and queue finish retiring.
  The required post-retirement-delivery assertion independently fails (134).
- Detaching the notification before delivery preserves a rearmed successor's
  function, argument and size; a second actual release invokes it once.
  Complete node cleanup similarly detaches before epoch cancellation can
  reenter and install the successor. A callback which reacquires a COLLECT
  node also prevents the preceding release from freeing its new reference.
  All three required behaviors now pass.

The last two fixes do not move delivery after physical retirement. An obsolete
callback still lacks immutable attempt identity, and its failure continuation
can still request SCAN after replacement. Those failing tests remain enabled
in the full default/expected-defect modes, not deleted to make an aggregate green.

## Built callback-argument checkpoint

The complete Linux aggregate, five selected positive production scenarios
(0/4/5/7/8), and the full expected-defect audit pass in their respective scopes.
macOS executes the same production bodies under ASan/UBSan and retains the
four distinct red requirements (1/2/3/6); this is not a full green roam gate.
The complete Tahoe build passes, all 1085 external symbols resolve, and there
is no thread_call_cancel_wait dependency.

- Identical local/guest production digest:
  `99c9481b6c92bff2604747de2a81f4ad7518920627c6e9275c8e1dadaf9f65d7`.
- Built UUID: `42D77FC7-BF18-3C78-A999-9276B718BF31`.
- Mach-O SHA-256:
  `5c1671cc3f1c2b2b18e89bba559ff5cd33e4f8fa985eef3357df25b7c233bf29`.
- Linux `/tmp/aiam-node-callback-linux-20260911-r3.log`:
  `7f38338006df4fe4beb770b51f61840ba916f1fbd681b8d2e0f61700fbfb5d72`.
- macOS `/tmp/aiam-node-callback-macos-20260911-r4.log`:
  `f018e7970b2839c3ab9537145d14517cfd55fde7c348e5a6c68a7870f7059e9f`.
- Full post-copy/TX pre-fix execution
  `/tmp/aiam-reassoc-deferred-full-tx-baseline-20260911.log`:
  `491b159006456341a00d4661e0aacd6a9421558a7e445057d1467ba48030b10c`.
- Current expected-defect execution
  `/tmp/aiam-reassoc-deferred-argument-20260911-r3.log`:
  `104d2b6b81451de3f8ad907bcde080f4076a66ce2c5be95aa0512ed6380368d0`.

The first macOS compile rejected a user-SDK macro redefinition and absent
explicit_bzero; the fixture now uses the existing portable memory support and
guards the platform macro. A later additional static contract still expected
the pre-52dd156d abort signatures; its updated assertions require actual
reassocSerial forwarding and the leaf-locked identity check, not just new names.
Those failed setup runs are excluded from passing build evidence. The first
built intermediate `7eaeeda04905`/`B380B03B` was not installed; the final build
also preserves a callback-reacquired COLLECT node. At this source checkpoint
the lab still runs `D63CF6F8`; admission and loaded runtime are next.

## FIX_CANDIDATE and full continuation

Retain the exact accepted roam serial, source/target BSSID, fresh-join sequence
and association epoch before any allocation/AMPDU stop/management submission
or epoch-cancellation callback can reenter. Transfer that identity only at the
matched controlled source-leave and target-replacement edges; ordinary reset,
explicit leave and a new public join must cancel it. Deferred work must never
borrow a newer owner after returning from a callback.

Replace the insufficient global zero-reference notification with real source-TX/lower
retirement ordering for IWN/IWM/IWX, including management submission failure,
terminal-before-arming, queued data/aggregation and reset. Merely changing
`refcount==0` to `refcount==1` does not prove that every physical command/DMA
owner is retired. The existing IWN WNM descriptor fence and IWM/IWX primary
station-use/retirement machinery are relevant integration points, not proof
that they already implement this accepted-roam bridge. Do not replace it with
a sleep, a shortened timeout, an unconditional node copy, or a new forced BSSID.

Detach callback/argument ownership before reentrant delivery; old cleanup
must not free or clear a successor's argument. Carry the same immutable identity
through selected-BSS copy, actual state workers and AUTH/ASSOC/key results.
Correct the reference's separate progress and overall completion producers
as part of this same layer, not an unrelated synthetic notification patch.

Required runtime remains loaded-image successful/failed/no-target/superseded
roam and native same-SSID candidate progression, DHCP and both traffic
directions, then GUI/open/WPA2/WPA3/S3/AP regression and qualified release.
The current session did not run an RF fixture or alter physical host 10.90.10.22,
the live VM image, base disks, or unrelated QEMU processes. Local pool space
is about 1.2 GiB; recoverable archive/admission checks remain necessary before
the next image activation, not a reason to stop source work.
