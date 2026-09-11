# Deferred BSS switch: real lifetime failures — 2026-09-11

## Result and scope

The physical abort/SCAN-doorbell correction is pushed as `52dd156d` and builds
on Tahoe. Its built UUID is `D63CF6F8-85F5-3C98-B3E6-7289BEFD5A19`; it is not
yet loaded. The lab still runs the qualified `97fe747c` image and public asset.
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

Actual lateattach creates the permanent `ic_bss` reference. The fixture then
models one transient TX reference using the real primitive and invokes the
complete release function. Physical TX/IRQ scheduling, node lookup, the inner
node-join/cleanup machinery and the common failure-publication callback are
explicit fixture boundaries. This is not an on-air reproduction or proof that
all driver DMA/command ownership is represented by the node counter.

Ordinary non-BSS zero-reference delivery and direct valid switch-callback
controls pass. Four independent required-behavior assertions fail on both
Linux and macOS under ASan/UBSan, each exit 134:

1. **The switch never starts after transient TX references drain.** Actual
   lateattach leaves `ic_bss` at one reference. After the modeled TX reference
   is released, the observed state is `refs=1 joins=0 callback_pending=1`.
   WCL's non-pure-SAE path arms this zero-reference callback on `source=ic_bss`.
   The ordinary legacy background-roam branch does the same. The pure-SAE
   driver-resident retarget branch bypasses this callback, so this result must
   not be generalized to every SAE roam failure.
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
that the four separate failures occur; its wrapper exit zero does not qualify
the driver. The test is deliberately not put into the passing aggregate until
the real deferred lifetime is fixed.

Evidence:

- `/tmp/aiam-reassoc-deferred-bss-baseline-20260911.log`, SHA-256
  `a2ec9e5534ad909ab5f278cfe77ecd78cb106509fcd9f47396aaac7b01fd178f`.
- `/tmp/aiam-reassoc-deferred-bss-macos-20260911.log`, SHA-256
  `3da184b1e167335822085a9e439e4be71dc485ee21c85c9df1eebfcbaf616737`.

## FIX_CANDIDATE and full continuation

Retain the exact accepted roam serial, source/target BSSID, fresh-join sequence
and association epoch before any allocation/AMPDU stop/management submission
or epoch-cancellation callback can reenter. Transfer that identity only at the
matched controlled source-leave and target-replacement edges; ordinary reset,
explicit leave and a new public join must cancel it. Deferred work must never
borrow a newer owner after returning from a callback.

Replace the unusable global zero-reference wait with real source-TX/lower
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
