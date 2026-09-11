# Reassociation source-TX retirement across IWM/IWX — 2026-09-11

## Result: reproduced defects, not a production fix

The previous status turn added no functional progress. This continuation
completed [loaded `5aad4f68` SAE recovery](TAHOE_NODE_CALLBACK_RUNTIME_20260911.md)
and executed the complete IWM/IWX lower retirement functions. The latter
changes the next implementation: the IWN early-callback problem is not
IWN-only, and reset/free of ordinary STA traffic cannot be omitted from the
source-TX lifetime. No IWM/IWX hardware or physical-host `.22` test is claimed.

`scripts/test_reassoc_tx_retirement.sh` extracts without body edits:

- Common real incref/decref/ref and `ieee80211_release_node`.
- IWM `iwm_txd_done`, `iwm_ampdu_txq_advance`, `iwm_reset_tx_ring`,
  `iwm_free_tx_ring`.
- IWX `iwx_txd_done`, `iwx_ampdu_txq_advance`, `iwx_reset_tx_ring`,
  `iwx_free_tx_ring`, `iwx_clear_tx_desc`.

The selected descriptor/node fields are minimal user-space structures, not
claims about hardware offsets. Firmware input, scheduler-MMIO/DMA operations,
SAE terminal side effects, actual locking/scheduling and the BSS continuation
body are explicit boundaries. The real IWX lock/unlock calls remain in the
executed body and the lock substitute records their nesting. The callback
observes state rather than deliberately deadlocking. Both families' ordinary
two-step aggregate controls pass without a callback, with two actual node
refs/releases and fully reclaimed packets. No wrapped equal-tail SSN is
misrepresented as an owned full-ring reclaim.

## Required-behavior failures

All eight scenarios independently compile and fail their intended assertions
with exit 134 on Linux and macOS under ASan/UBSan:

| Scenario | Actual observation |
| --- | --- |
| 1, IWM aggregate | Last release invokes continuation with `queued=1`, `tail=1`, descriptor node pointer still live, length 1400 |
| 2, IWX aggregate | Same early continuation, additionally descriptor TB still live and **TX queue leaf lock held** |
| 3/4, normal STA reset | Two mbufs freed, but both descriptor node pointers and two node references remain, for IWM and IWX respectively |
| 5/6, normal STA free | Mbufs and DMA maps freed, but both node pointers and two references remain, for IWM and IWX respectively |
| 7/8, SAE-marked reset | With the explicit SAE terminal boundary, last node release invokes continuation with `queued=2`, descriptor/node still live, before final ring reset |

The ordinary reset/free omission comes from releasing `data->in` only inside
`if (data->sae_active)`. This is direct execution of those complete methods,
not proof of an unbounded leak or the cause of a particular user's crash.
Other higher-level node copies/resets can overwrite counters, which is another
reason not to make raw node reference zero the physical-drain contract.

Default mode is deliberately red and excluded from the passing aggregate.
`TX_RETIRE_EXPECT_DEFECTS=1` verifies the eight distinct assertion exits;
its wrapper exit zero is **not** a passing driver qualification.

- Linux final log `/tmp/aiam-reassoc-tx-retirement-20260911-r4.log`, SHA-256
  `e73aa7830c319a9eb8e6326bf746b4fb4ec7640909130fb15b6e8e9c8e5010d0`.
- macOS final log `/tmp/aiam-reassoc-tx-retirement-macos-20260911-r3.log`, SHA-256
  `f1d238885fdbd23d3d5ea9eecb7e0fc8989b5f604e06b0a18696ed149ec1d8c1`.

Initial fixture compiler failures (missing/then SDK-defined NBBY and an unused
local stub) happened before execution and are excluded. The unused-parameter
diagnostic is locally suppressed around unchanged DMA-sync-disabled IWX
production bodies, not removed by changing those bodies. Production digest
remains `99c9481b6c92bff2604747de2a81f4ad7518920627c6e9275c8e1dadaf9f65d7`;
these test/document edits do not require inventing another driver build.

## Full FIX_CANDIDATE, not a reference-count threshold patch

The reference remains the exact 25C56
[accepted-roam lifecycle](TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md).
The complete recovered `handleRoamPrepEvent` was re-read: its source is an
actual firmware preparation event, with reason/target/RSSI, not a local node
reference change. The reference does not dictate use of our net80211 nodes.

1. Capture immutable accepted-roam serial, source/target and join/association
   identity **before** allocation, stop-AMPDU, DEAUTH enqueue or cancellation
   callbacks can admit a successor. Install admission before the first real
   terminal can arrive; explicit new join/reset cancels only the old owner.
2. Close actual source TX constructors and account for software management
   queue, submitted data/management descriptors, aggregate queues and lower
   station/reset owners. A node-ref notification may wake a checker but cannot
   itself attest this physical retirement. Handle submission failure and a
   terminal before the sender returns without a lost wakeup.
3. Detach all retired descriptor pointers/metadata and complete scheduler,
   ring counter and cursor updates before any BSS continuation. In IWX never
   run node-copy/epoch/BA teardown under a TX leaf: it may acquire another
   queue/owner lock. Signal an always-asynchronous main-workloop continuation,
   revalidate immutable identity there, and inspect actual remaining source
   owners under their existing leaves. Do not call the continuation inline
   merely because the current thread is already inside the main gate.
4. Reset/free must release ordinary STA references too, once per descriptor,
   after preserving the values needed by terminal reporting. Global reset and
   queue-local retirement are different: IWX also resets detached/replaced
   TVQM rings, so unconditionally clearing the current common BSS owner from
   every ring reset could erase a successor. Use the original queue/attempt
   identity and audit each actual caller, including attach-unwind/detach.
5. Carry the same accepted roam through selected-BSS copy, deferred state
   workers and real AUTH/ASSOC/key outcomes. Preserve separate fresh-join and
   physical-station identities; adding roam serial to every station equality
   would incorrectly reject the still-live source's DEAUTH/TX admission.
6. Complete reference progress and terminal publications from actual edges:
   scan/start/preparation, AUTH, reassociation and overall completion are not
   interchangeable. The reproduced pure-SAE generation-zero route still
   needs its distinct owner; do not fabricate a fresh JoinAdapter request.

Known integration points are IWN's real `_iwn_start_task`/IRQ workloop and
descriptor reclaim, plus IWM/IWX `stateTransitionSource` and primary-station
use/retirement machinery. The latter protects constructors but is not by
itself proof that every pending hardware descriptor is gone. Existing WNM
management fences cover particular frames, not this full source drain.

Execute complete send/terminal/reset/deferred-delivery bodies across all
families, retain the eight negative controls until corrected, then load and
exercise successful/failed/no-target/superseded native roam and real candidate
progression, DHCP and both traffic directions. Include legacy open/WPA2 and
pure SAE, GUI profiles/on-off, actual S3, AP and release readback. Do not replace
this with a timeout, direct node copy, artificial target or event-only fix.
