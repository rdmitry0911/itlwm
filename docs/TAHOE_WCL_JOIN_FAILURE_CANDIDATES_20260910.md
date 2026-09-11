# WCL failed-join candidate progression — 2026-09-10

## Physical roam-scan abort checkpoint — 2026-09-11 03:36 UTC

PROGRESS: the accepted roam serial now reaches real IWN/IWM/IWX abort
reservation; all six IWN abort callers retain the exact physical serial
through the actual command doorbell. New IWN scan doorbell and IWM/IWX policy
checks reject changed source epochs. The full Linux aggregate, affected
macOS suites and Tahoe build pass; UUID `D63CF6F8-85F5-3C98-B3E6-7289BEFD5A19`.
An unchanged old IWN caller independently reproduces aborting its successor.
This is built WIP, not loaded/released full-roam qualification. Deferred
BSS/state/AUTH/key identity and real reference progress/completion remain
the same active layer; the qualified release remains `97fe747c`.
See [physical-command implementation and evidence](TAHOE_REASSOC_OWNER_IMPLEMENTATION_20260911.md).

## Accepted-roam identity implementation — 2026-09-11 03:03 UTC

PROGRESS: common admission and retirement now use a distinct 64-bit roam
serial, retained by actual IWN/IWM/IWX scan commands and their terminals.
Old cancellation no longer erases a successor or publishes twice; detached
controller-gate results reject replacement and duplicate delivery. Early real
scan completion cannot be rearmed by the later sender return. The complete
Linux aggregate passes; new macOS tests and Tahoe build pass (UUID
`72E678CD-2AB3-3F64-BF6B-12790E86CCFC`, all 1085 symbols resolved).

This is built WIP, not a loaded/released roaming fix. Exact physical abort,
deferred BSS/state/AUTH ownership and the reference progress/overall completion
carriers remain part of the same active layer. No new on-air result is claimed.
Loaded and released production remains qualified `97fe747c`.
See [implementation, evidence and required continuation](TAHOE_REASSOC_OWNER_IMPLEMENTATION_20260911.md).

## Reassociation reconstruction checkpoint — 2026-09-11 02:24 UTC

PROGRESS, not a new functional closure: exact raw 25C56 data now distinguishes
command-start failure `0xcf` from actual reassociation status `0x49`, AUTH
status `0x4a` and overall roam completion `0x50`. All 60 roam-FSM cells and
31 subscriptions were independently read from the fileset. A corrected
51-function batch completed with 40 actual decompiler interfaces; separate
raw-byte disassembly verifies every selected function interval. Current local
accepted-roam progress/completion producers are incomplete, so changing the
SAE rejection's selector alone would not implement the reference lifecycle.

An extracted complete production failure helper reproduces two additional
retirement defects: cancellation callbacks can admit a new owner which the old
helper then erases, or reenter retirement and publish twice. Ordinary controls
pass; both desired-behavior assertions fail with exit 134. The new pending
regression is intentionally red by default, separate from the passing aggregate.
The explicit expected-defect mode is negative evidence, not a fix/pass.

See [the exact contract and implementation obligations](TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md).
Production, loaded IWN image and verified release remain `97fe747c` / `5e7a6735`.
No RF fixture, install/reboot or physical-host operation accompanied this
reconstruction. The next step is immutable accepted-roam ownership and the real
lower-to-WCL result lifecycle, keeping fresh JoinAdapter failure separate.

## Published checkpoint — 2026-09-11 02:03 UTC

PROGRESS: `v2.4.0-alpha` now serves the qualified `5e7a6735` / production
`97fe747c` image, UUID `69D766FF-86F9-3337-ADE0-48361B68A59C`. Asset
`556261698` was independently downloaded and byte-compared after publication;
ZIP SHA-256 is `c2dd6a085d4929d733fa77f385bc0a460160caaba6f885b5405f916b368ba622`.
Release notes match readback and explicitly retain hardware/GUI/roam limits.
The source, build, loaded runtime, regressions and release loop for the fresh
SAE peer-failure producer is complete, not the full functional objective.
Next priority is the separately owned failed reassociation/roam attempt and
native candidate progression; no new fresh-join ledger may be invented for it.
The live lab remains on the same boot/image with recovered STA and no active
RF fixture or test HTTP server. Host 10.90.10.22 remains untouched.

## Qualified IWN artifact checkpoint — 2026-09-11 01:59 UTC

PROGRESS: the same loaded `69D766FF` / boot `02DED0EA` candidate now passes
actual GUI saved WPA3-to-WPA2 and reverse selections, one GUI off/on, and a
GUI-selected open network, each with real DHCP and independent 20/20 forward
and reverse traffic. Actual Ethernet/tablet-free S3 restores that open network
without another selection; DHCP and both 20/20 checks pass before USB
management returns. Native post-S3 WPA3/WPA2/open Internet Sharing each passes
external DHCP, 20/20 forward, cold-ARP reverse 10/10, routed HTTP and a normal
stop/dwell with zero bridge I/O references plus current STA 10/10.

The complete serial audit has no matching panic/fatal/device timeout. The
same-image archive is prepared and independently extracted/compared; public
publication/readback is next. This remains IWN hardware qualification, not
IWM/IWX hardware closure. Post-S3 GUI is still blocked: a bounded WindowServer
sample now localizes its main thread to framebuffer acknowledgement on wake,
without yet identifying the kernel-side cause. Bad-BSS reselection, separate
reassociation failure, other real failure/timeout producers, remaining HAL
lifetimes and the complete GUI/roam/AP matrix stay in the full goal.
See [the completed runtime report](TAHOE_SAE_PEER_FAILURE_20260911.md).

## Loaded peer-failure checkpoint — 2026-09-11 01:33 UTC

PROGRESS: `97fe747c` plus test-only `5e7a6735` is built and loaded on real
IWN/6235, UUID `69D766FF-86F9-3337-ADE0-48361B68A59C`, boot
`02DED0EA-E840-4DAE-BED2-1067761106C1`. Private admission and activation passed.
Two fresh AUTH generations (2 and 4) consumed actual empty-body status-1 SAE
Confirm rejections, completed producer/lower/SAE cleanup (1/2/4), published
one failure each and proceeded to successful SAE on another BSS of the same
SSID. Native DHCP publication and subsequent independent 20/20 forward and
reverse traffic checks confirm service recovery without another join, toggle
or reboot. The complete observer and watcher both terminated successfully.

This is not full candidate-policy closure: the same run revisited the bad
AP, and reassociation's separate 0x49 failure owner remains outstanding.
Exact-image GUI/S3/AP regressions are next, then qualified release promotion.
The public artifact remains `b5c6cfd8`. AUTH/ASSOC/key timeout producers and
remaining IWM/IWX lifetimes/hardware qualification stay in the full objective.
See [the exact-image report](TAHOE_SAE_PEER_FAILURE_20260911.md) for identities,
actual probe scope, setup failures excluded from RF counts and evidence hashes.

## FIX_CANDIDATE — real SAE peer rejection, 2026-09-11 00:56 UTC

The correctly timed `night4d-auth5` observer ran to its real 90-second terminal
with zero diagnostic errors on loaded `4d979f3b`, boot
`D946CBDB-621A-4524-9D0C-4E3FE52C1E88`. Three actual Confirm rejection frames
reached the in-kext engine: epochs 32, 54 and 69, status 1, zero-length body,
and return `PEER_AP_REJECT` (4). Each was followed by worker retirement with
request_scan=1, not an AUTH join-failure producer. A distinct discovery failure
did call the common producer with NO_NETWORKS (5). An intervening valid AP
completed SAE at epoch 53. This rules out the proposed short-frame/6235 RX
limitation for this reproduced rejection; it does not qualify all PMF paths.

The preceding auth3 request was explicitly EBUSY. Auth4 was accepted but its
scan ended NO_ELIGIBLE_TARGET and no authentication occurred. Neither is a
negative SAE-delivery test. Auth5 kept the actual AP beaconing for 15 seconds
before the accepted request and observed the whole exchange; no synthetic
driver event or retry of an accepted request was used. All fixtures terminated
and restored host managed Wi-Fi; the wired management route was preserved.

Reference route: re-express the recovered 25C56 JoinAdapter handleAuth and
sendConnectComplete contract using the existing local request/cleanup ledger.
Retain the real peer status for an exactly matching fresh AUTH candidate.
Claim it under selected-BSS then engine leaves, retain its join generation in
the cancelled engine owner, and request the existing generation-owned lower
cleanup instead of starting an autonomous replacement scan. Acknowledge the
producer only after local peer/PMK/terminal values are scrubbed; lower and SAE
owners keep their independent actual retirement acknowledgements. Test the
complete production worker/retirement path, stale/replaced owners, malformed
local crypto outcomes, successful traffic and deferred cleanup. Do not invent
an AUTH failure for an ownerless reassociation: its separate 0x49 lifetime,
actual timeouts, other AUTH/ASSOC/key producers and IWM/IWX qualification remain
part of the full objective after this candidate-specific producer is tested.

The implementation captures the distinct common join generation when the SAE
producer is admitted, then rechecks it when consuming the real engine result.
The complete production worker, claim, cancellation and retirement functions
pass 19 ASan/UBSan execution scenarios on Linux: received statuses 1/77,
local crypto/method failure without invented peer status, stale peer/BSS/SSID,
closed admission, replacement during crypto and during credential cleanup,
delayed TX terminal retirement, DROP/TX_READY controls and duplicate completion.
The exact pre-fix worker from `4d979f3b` independently compiles and fails the
missing-producer assertion with exit 134. Lower firmware completion, crypto
outcomes and task scheduling are explicit fixture boundaries, not simulated RF
qualification. The existing join bridge/physical cleanup/controller dispatch
tests and complete Linux payload aggregate also pass. macOS build, admission,
loaded-image test and publication remain pending at this source checkpoint.
The first macOS fixture compile exposed its user-space SDK's missing
`explicit_bzero` declaration, before the driver build was invoked. The fixture
now uses the existing portable kernel-memory test support; production scrubbing
is unchanged. That failed compiler run is not counted as a passing macOS test.

## Loaded runtime checkpoint — 2026-09-11 00:37 UTC

PROGRESS: source `4d979f3b` is now loaded on the real IWN/6235 in a new
recoverable child overlay, UUID `F23158D2-32F8-39A1-87CC-83E310634012`, boot
`6CCFA730-B7E2-4E23-A4B5-CCA40BA6B447`. Private admission and transactional
activation passed. The earlier built-only/loaded-b5 statements below describe
their historical checkpoints, not current lab state.

Actual wrong-password SAE target failures retain prompt WCL link teardown
(14 ms) and automatic fallback to saved WPA2, with 20/20 subsequent packets.
Native recovery still takes about 24.9 seconds and revisits the bad WPA3 BSS;
full failed-candidate progression is NOT closed. Real S3 preserves the boot
and image; automatic WPA3/DHCP restoration passes separately awaited 20/20
forward and reverse packets. Full GUI/AP and IWM/IWX hardware coverage are
not inferred from those results. The initial post-boot check lost 2/10 packets.

Full identities, timing, rollback parent, evidence hashes and excluded FBT/
command-line selection attempts are in
[the night runtime report](TAHOE_NIGHT_CANDIDATE_RUNTIME_20260911.md).
The next most-used functional boundary remains actual AUTH failure/timeout
publication and native next-candidate progression. Do not revert to indefinite
IWN load deferral while accumulating unrelated IWX initialization work.
Keep all remaining IWM/IWX lifetime and qualification work in the full goal.
The public release remains `b5c6cfd8`; this is not a qualified replacement.

## Checkpoint — 2026-09-11 00:03 UTC

PROGRESS: the previous user-status turn was read-only. This continuation
implemented the physical IWX TX-queue transaction, including fixed management
and AUX queues, dynamic TVQM, the actual q0 sender, station/AP retirement and
the hardware-reset release edge. It is a built source checkpoint, not a newly
qualified reconnect layer or a replacement public release.

The pending transaction owns a full-width serial, hardware/reset lifecycle,
station/TID and task admission. Primary allocation additionally retains its
station incarnation. Size fallback is local-DMA-only; once submitted, a lost,
failed or malformed reply quarantines the carrier and schedules recovery.
It cannot reset/free DMA or submit another smaller queue. A returned occupied
queue cannot replace a live carrier, while unused attach-preallocated q1/q2
memory remains replaceable. Response flags and queue/write-pointer constraints
are validated. Primary and AP TX check the published firmware queue owner;
primary constructor readers and AP queue-leaf exclusion fence retirement.

The old primary ENABLE_QUEUE=0 removal was not the TVQM legacy ABI. Intel
[v6.12 station removal](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/sta.c)
distinguishes explicit new-API removal from older TVQM; the unrelated old DQA
disable command is not interchangeable. Here v3 removes retained station/TID
queues, while legacy queues remain owned until checked REMOVE_STA. Flush,
individual removals, station completion and host reclamation share one physical
transaction. Successful per-queue removals are retained. A missing/failed
station reply cannot silently clear the owner or allow retry/reuse before reset.
DMA reset/free operates on detached storage outside interrupt-side leaves;
fixed primary storage is reinstalled for the next connection. The actual
stop_device ring-reset loop now follows SW_RESET, not precedes it.

AP teardown no longer removes parent resources or discards the client/runtime
after a failed child removal. The outer start/stop workers were also corrected:
they had unconditionally erased the retained runtime. A stopping runtime now
blocks a new start and survives until the existing hardware recovery clears it.
A quarantined queue prevents AP parent teardown. This does not claim complete
AP key/BA/timer or real firmware-notification ordering qualification.

Verification on Linux and macOS: the complete payload aggregate passes,
including 87 TVQM allocation/retirement executions (versions 0, 3 and UNKNOWN),
62 actual q0 sender cases, 35 AP/scan groups including the real outer AP workers,
581 station-command cases, 254 state-worker groups, 81 RX-BA scenarios and
118 physical-context scenarios. Existing management and three-family AP RX/TX
contracts pass. The baseline 58d19493 retry implementation independently
compiles and fails its executable assertion (exit 134): five submissions,
five unsafe resets, ten freed firmware-referenced DMA regions. The corrected
case records one submission and zero unsafe reset/free operations.

DMA allocation, firmware replies, interrupt scheduling and actual reset remain
explicit fixture boundaries. The station suite uses a value carrier/task
boundary, while the TVQM suite separately executes the full allocation,
retirement, exchange and physical-lease methods with allocation tracking.
The earlier 581-case assertion requiring immediate retry of a lost IWX
REMOVE_STA was replaced by retention/no-resubmission and fresh-epoch reconnect;
IWM's acknowledged partial-removal retry checks remain unchanged.

Equal production fingerprint on source and guest mirror:
81687f45ebd7c0b86708046af2d0b72e8f44b405b04ff0fc0bc17b2dd83b5ce5.
Built UUID: F23158D2-32F8-39A1-87CC-83E310634012.
Mach-O SHA-256:
94cfee4fd9d7742156722154c346da4d635773861ca979244ad0a510a1a36c59.
Guest regression/build log:
/Volumes/AIAMBuild/itlwm-88c0d3f9/DerivedData-join-failure-20260910/tvqm-final-20260911.1V6hhg,
SHA-256 2f73c22ccbc5ae29fe576e8dbf54f0d36775ec77a6d7d948db7b930e2fa95bd5.
Linux log: /tmp/aiam-tvqm-owner-20260911.5Z9n64,
SHA-256 4723779b5951979c0742ad6dda544eb2d92b8289bfcaf84958b9da15455cd37f.
All 1085 external symbols resolve; no _thread_call_cancel_wait dependency.
The earlier d77aa14d6e17 / 6B555F9C build is superseded.

Read-only guest identity remains boot 9C8DDC74 / loaded UUID 9D5A9332,
source b5c6cfd8. No install/reboot, radio mutation, public asset change,
physical host-22 access or unrelated-QEMU action occurred in this continuation.
User-owned local Build/ remains untouched. No build/test process is still live.

The next runtime cycle will exercise the accumulated common/IWN changes in the
isolated 6235 guest, with a recoverable last-working image and exact loaded
identity. That is controlled testing of a WIP candidate, not permission to
promote it as a finished all-family layer. The prior blanket wait-for-all-HALs
load gate must not indefinitely defer real IWN testing: IWM/IWX hardware paths
are not executed by this device. The complete objective remains unchanged.

Still required: copied TX-BA workers/main-workloop completion, the actual IWM
producer/drain reset barrier, immutable AUTH/time-event/session-protection
inputs, real AUTH/ASSOC/key failure producers and exact received-frame CB/D5
publication. The adjacent AUX ADD helper also still ignores a transport error
before queue enable; reproduce/fix that real initialization edge. Qualify
candidate failure/progression, saved GUI profiles, open/WPA2/SAE, sleep and AP
on the exact candidate, and continue IWM/IWX hardware qualification. Do not
count the current IWN guest as evidence for IWX queue firmware behavior.

## FIX_CANDIDATE: confirmed TVQM retirement, not invented legacy removal

The September 11 status turn was read-only (no progress). The next source
audit verified Intel v6.12 mvm/sta.c, iwl_mvm_disable_txq: new-TX firmware
without the explicit SCD_QUEUE_CONFIG removal API does not receive a legacy
ENABLE_QUEUE=0 command. The current local primary removal invented that
operation; the AP path instead frees transport DMA before REMOVE_STA without
proving this port's firmware/TX-producer retirement boundary.

Use the physical queue transaction for allocation and retirement. Close packet
admission under the actual queue leaf before removal. The explicit v3 API
removes each retained station/TID queue; the legacy API retains the queue until
the station-removal terminal. Check the returned command header, retain partial
removal progress and ambiguous DMA ownership, and exclude allocation until all
host reclamation is finished. AP failure must not forget a still-owned client
or reclaim its DMA. Actual SW_RESET remains the other physical release edge.
Test both wire versions, fixed/bootstrap queues, repeated station removal,
failed/missing responses, replacement/reset during waits and adjacent owners.
This is a continuation of the complete queue/TX-BA/reconnect layer, not runtime
qualification or permission to load an incomplete lifetime conversion.

## Next-gate evidence — 2026-09-10 23:16 UTC

The RX commit 58d19493 is confirmed on origin. Its full guest build and loaded
image distinction above is unchanged. Work immediately continued into the
actual TVQM allocation prerequisite, not another status-only goal turn.

scripts/test_iwx_tvqm_allocation.sh now extracts the complete production
iwx_tvqm_alloc_txq, both enable wrappers, iwx_alloc_tx_ring,
iwx_tx_ring_init and iwx_ap_exchange_tx_ring_carrier. It compiles the real
firmware structures, tracks each allocated DMA address/map, and models an
accepted command whose response is lost or malformed. This is a software
reproducer, not observed firmware behavior or a new on-air test.

The four controls (control, local-dma, preallocated, ap-control) pass on Linux
and macOS. Named transport and short cases independently fail because the
allocator resets firmware-owned descriptor storage; collision fails because
the full exchange replaces an already-live primary carrier with an AP one.
Each is a compiled executable failure with exit 134, not a compilation failure.
The retry case records five submitted commands, five unsafe carrier resets
and ten freed firmware-referenced DMA regions before its assertion fails
(exit 134). The actual driver retry loop, not the fixture, chooses those five
sizes. TVQM_NEGATIVE_REF=58d19493 retains the exact baseline extraction route.
These four negative cases are deliberately outside the passing aggregate
until the complete lifetime correction lands; the control default is not a
claim that TVQM is fixed.

Important implementation constraint discovered during this audit: attach
preallocates q0/q1/q2 storage (larger qids return an empty carrier from
iwx_alloc_tx_ring). A nonzero ring_count therefore cannot prove firmware
ownership. The preallocated control must continue to allow replacement of
unused local q1 memory; collision rejection needs a real firmware queue owner,
including the fixed iwx_enable_txq/disable path, not just an allocation flag.
The shared AP removal path also detaches/reclaims a carrier before REMOVE_STA;
audit its flush/removal terminal and readers together with the dynamic path.
Do not implement a blanket nonempty-ring refusal or merely limit the retry
count while leaving submitted DMA reclamation unsafe.

The next production step remains the whole allocation/q0/physical-reset
contract described below, followed by copied TX BA completion and IWM reset
drain. No new driver changes, installation, reboot or release promotion are
claimed for this reproducer checkpoint.

## FIX_CANDIDATE: actual IWX TVQM allocation and reset ownership

58d19493 is built and pushed, not loaded. Continue the physical TX queue
transaction before moving its upper BA completion. The current allocator uses
one shared sc_tvqm_ring, overwrites it at entry, frees it after any response
error, and retries the whole firmware command at smaller sizes. A command can
already have transferred its DMA addresses to firmware before such an error.
The current carrier exchange also replaces an occupied queue instead of proving
that the returned firmware ID is available for this allocation.

Intel v6.12 pcie/tx-gen2.c, iwl_txq_dyn_alloc, retries only the local DMA
allocation before submitting SCD_QUEUE_CFG; iwl_pcie_txq_alloc_response rejects
an already-used queue before publication. Linux's transport failure/free paths
are not copied as a claim that this port has the same stop/error synchronization.
The exact local q0 doorbell and iwx_stop_device software-reset edge must own
submitted-but-unknown memory here. The Apple 25C56 retained join-abort consumer
remains the upper lifecycle reference, not an Intel queue ABI source.

Give the complete pending allocation an exact serial, hardware/reset lifecycle,
station/TID and task admission. Fence its actual q0 doorbell. Permit size
fallback only for a local allocation failure before submission. Publish a
confirmed queue only into an unused carrier. Retain an ambiguous submitted
carrier, prevent its reuse and request recovery; free it only after an actual
hardware reset, outside the leaf lock. Cancellation before submission may
free immediately, while an active allocator survives reset until its own exit.
Cover the full allocator and q0/reset integration, including AP callers, before
claiming this prerequisite complete. Then finish TX BA request/host publication
and IWM's actual producer/reset barrier; do not promote RX-only work as a
completed failure/reconnect layer.

## Checkpoint — 2026-09-10 23:03 UTC (September 11 locally)

PROGRESS: the preceding status response was read-only. The complete primary
RX BA ingress/worker/mailbox/host-retirement path is now implemented in IWM
and IWX, including old non-MQ IWM sessions. ADDBA captures station incarnation,
TID, serial, SSN/window, timeout and token before queuing. A separate stop slot
cannot be overwritten by a successor start. Hardware work carries values,
not a borrowed node; a real created resource survives logical replacement or
callback-source shutdown. Main-workloop publication validates the accepted
request and owns reorder/timer and generic callbacks. IWX releases the inner
hardware task admission before notification; the old outer TX worker is still
a separate prerequisite, not implicitly made safe by this RX split.

RUN-stop and station removal preserve completed firmware removals while
deferring the exact state transition for host cleanup. EINPROGRESS is not a
failed join. Closed-admission host-retirement readers prevent early station
reuse. Actual reset clears resources and pending mailboxes but does not erase
a running worker/publishing reader. Primary deauth/removal no longer zeros
adjacent AP session accounting; all shared count accesses are atomic.

The adjacent timer audit below found and corrected missing timer recreation
after DELBA/reset in primary and AP starts. The pre-correction production RX
completion compiled, then failed the repeated-start timer assertion (exit
134). The unchanged c15a4977 state workers independently compile and fail the
EINPROGRESS-as-normal-completion assertion for IWM and IWX (each exit 134).
Old complete RX ingress negative controls similarly fail copied-request
admission, not compilation.

Linux and macOS complete aggregates pass: 81 RX lifecycle scenarios, 254
state-worker groups, 581 actual station/BA command cases, 118 context cases,
management and three-family AP RX/TX contracts. RX tests include cancellation
before/after submission, source closure, ambiguous start/stop, AP BAID collision,
stop-before-successor-start during a command, partial cleanup/retry, reader
and IWX task balance, reset/reentrant callbacks, timer reuse and serial limits.
Three concurrent counter users also retain the adjacent count. Firmware wire
helpers are executed separately in the station suite; timers, scheduling,
packet storage and real reset/DMA remain explicit fixture boundaries. The AP
timer callsites have source/build coverage, not new AP hardware qualification.

Production fingerprint, equal source and guest mirror:
d24d995c59628618e349ba7aff96bfd00231138bd825fef8f79daaec421f65c8.
Built UUID: 2CEF0F69-B628-3295-B863-0A1050B56DD3.
Mach-O SHA-256:
0c721ffd051e792d8cba1df616a5793b7ff129dfd9a57b19b3fbd3df8e2a3166.
Guest regression/build log:
/Volumes/AIAMBuild/itlwm-88c0d3f9/DerivedData-join-failure-20260910/rx-ba-async-20260911.k1hoB7,
SHA-256 6d0bacf77f4893a3df22645313ea942658f856ae96d19dfb15270ec91ba1993f.
Linux log: /tmp/aiam-rx-ba-async-20260911.HrHOrm,
SHA-256 74f2d9d2d83d7b8a8e37c979d36cb60a2995330beb88b155706c78d5920c8919.
All 1085 undefined symbols resolve; no _thread_call_cancel_wait dependency.
This is built WIP, not a runtime-qualified reconnect layer. Read-only lab
verification retained boot 9C8DDC74, loaded UUID 9D5A9332 and STA
172.16.66.212. No installation/reboot, physical user-host change or replacement
release occurred. User-owned local Build/ remains untouched.

Required continuation: complete copied TX BA operations and actual queue
ownership, drain IWM hardware constructors before ring reset, retain uncertain
IWX TVQM allocations, and finish AUTH/time-event/failure-producer lifetimes.
Then qualify the exact image through real candidate failure/progression,
GUI/open/WPA2/SAE/S3/AP and update the release. RX tests do not close those
remaining full-lifetime boundaries.

## FIX_CANDIDATE: reusable RX timers and shared AP/STA accounting

The RX transaction review found a concrete repeated-session defect in both
families: clear_reorder_buffer calls timeout_free for both timers, whereas
the next STA/AP start only initializes reorder values. CTimeout::timeout_free
nulls the pointer and timeout_add_msec rejects a null object. The original
timeout_set calls run only at attach, not at every reconnect or hardware reset.
Recreate missing session/reorder timers on the main-workloop RX publication
and in the adjacent AP start, before any timer can be armed. Keep timeout
callbacks tied to the persistent RXBA table, not a borrowed node. Exercise a
stop/start reuse with timer storage actually cleared at retirement; this is
timer-lifecycle coverage, not real IOTimerEventSource scheduling qualification.

The new primary worker and existing AP path also update the shared RX session
count in different contexts. Use common atomic load/add/drop/reset operations
for every production access. This prevents lost accounting updates but does
not make a check-then-submit a firmware resource reservation, replace the
pending IWM reset barrier, or qualify AP firmware ownership after an ambiguous
command response. Firmware resource refusal remains an explicit result.

## FIX_CANDIDATE: immutable RX BA work and asynchronous host retirement

The previous goal turn is PROGRESS (076a5674/c15a4977, tests and complete
builds). Continue the actual RX transaction, not another result-only guard:
copy the ADDBA ingress values and station incarnation, keep stop-before-start
ordering per TID, execute only wire operations in the worker, and publish the
copied result on the main workloop. Firmware-created resources survive stale
upper requests. Keep a station reader through host publication but release
the separate IWX hardware task pin before notifying the workloop.

Use the same asynchronous host-retirement boundary for RUN-stop/deauth.
Successful firmware removals must survive state-worker deferral and must not
be reissued; the main callback owns reorder purge/timers and exact accounting.
Include legacy non-MQ IWM TIDs without BAIDs and preserve adjacent AP counts.
Actual reset, logical cancellation and source shutdown must balance pending
readers without forgetting a still-owned firmware resource. TX BA/TVQM and
the actual IWM hardware-producer reset barrier remain part of the whole layer,
not claims implied by completion of this RX transaction.

## Checkpoint — 2026-09-10 22:20 UTC (September 11 locally)

PROGRESS continues after 076a5674: both RUN-stop bodies now select only the
primary station's RX BA entries and return firmware-stop errors. IWM performs
one host retirement/count decrement, not two, and its TID lookup cannot select
an adjacent AP client. An absent primary TID is idempotent even when an AP
still owns that TID. IWX's RX helper returns the actual error to RUN-stop.

The new 17-scenario executable test compiles both complete RUN-stop and RX
aggregation bodies plus the actual IWX station/TID lookup. It covers AP entries
on either side, equal TIDs, first/second command failure, partial progress and
retry, repeated/absent stop, RX-start error/success and initial IWX flush error.
Both unchanged 076a5674 implementations compile in that fixture: IWM fails
the duplicate-host-retirement assertion and IWX fails error propagation, each
with terminal exit 134. Firmware, timer/purge and the later MAC/PHY/TX helpers
are explicit boundaries; the TX-aggregation branch is not exercised here.
This is hardware-BAID cleanup coverage, not old non-MQ IWM RX-session tracking
or a reset/main-workloop proof.

Full Linux and macOS aggregates pass, including 581 station command cases,
the new 17 cleanup cases, 206 state-worker groups, 118 context cases and
adjacent management/three-family AP RX/TX aggregation contracts.
Equal production fingerprint:
36aaf163edd9cd56b32fa17228f08d5cfa46f820299395dfce49eb8ac59571ef.
Built UUID: 8BB37D90-3245-38D9-B29C-89D0EBD18A18.
Mach-O SHA-256:
53fad540dd49a3ccb104682387913cf549892dd29415db007c2d4966fb4ca9ee.
Guest full regression/build log:
/Volumes/AIAMBuild/itlwm-88c0d3f9/DerivedData-join-failure-20260910/ba-teardown-20260911.FYZ0hX,
SHA-256 28a97cca5876a047c232c60e2ce5f8a8296b052d5e9337c0d5666349a308d4fb.
All 1085 symbols resolve; no _thread_call_cancel_wait dependency. This remains
built WIP, not a loaded/released reconnect or APSTA qualification.

### Next whole BA lifetime boundary

Capture request values at ampdu ingress, not from ic_bss inside the worker:
station ADD-incarnation, common attempt, TID/operation serial, direction,
SSN/window, timeout and token. A pending request must not pin a hardware task;
an admitted worker must retain hardware access until its last command/ring use.
Record the actual BAID/legacy-TID resource before asynchronous host publication,
even if the common attempt has changed. The main-workloop completion validates
the copied request before touching generic BA state, timers or node callbacks.
Pending host publication can retain a station reader, but not an IWX hardware
task reference that stop drains while holding the main gate. Newstate/task
cancellation and actual reset must retire every pending/running/completed owner
without either dropping an uncertain firmware resource or leaking that reader.

RUN-stop still reaches synchronous reorder timers; IWM also calls ba_del on
the replacement node. Move these into that same asynchronous transaction,
with the exact state transition deferred until host retirement. Preserve the
confirmed command result across the resumed state worker; do not reissue the
already retired BA command. Primary-resource accounting must cover non-MQ IWM
without a BAID as well. IWM deauth and IWX rm_sta still zero the shared RX BA
count; their later primary teardown must not erase AP accounting. The passing
RUN-stop tests do not claim to fix those later functions. Then finish actual
IWM hardware-producer drain, IWX TVQM uncertain allocation and the remaining
AUTH/failure producer prerequisites before exact-image runtime promotion.

## FIX_CANDIDATE: preserve exact RX BA teardown and adjacent AP owners

The completed 076a5674 command split exposed a separate caller defect:
IWM RUN-stop ignores RX BA command failure, then clears the reorder owner
and decrements the session count again after the helper already did so.
Both families walk the combined primary/AP reorder table without filtering
the station; IWM's helper also selects a stop entry by TID alone. A failed
primary cleanup can therefore lose its physical resource record or damage an
AP client sharing the same TID. IWX's void helper cannot report the failure.

Make the caller visit only primary-station entries, retain them and return
the error on failed firmware stop, and account successful retirement exactly
once. An already-absent primary TID must not consume an AP session/count.
Test the complete RUN-stop and RX-aggregation bodies with AP entries before
and after the primary entries, same TIDs, partial failure/retry and repeated
stop. Intel's retained-station/TID BAID lookup and command-before-count/host
retirement order are the reference; this does not yet move timer/node actions
to their asynchronous main-workloop owner or qualify reset races.

## Checkpoint — 2026-09-10 22:12 UTC (September 11 locally)

PROGRESS: the actual IWM RX/TX BA and IWX legacy/BAID-ML RX command
boundaries now use retained station identity and distinct command receipts.
RUN-stop cleanup uses the old physical station, not the replacement node.
Station ID, MAC/color and command layout survive capability/node replacement;
IWM RX SSN/window are explicitly little-endian. Unknown transport or BAID
results retain uncertainty, while a definite RX-start resource refusal returns
ENOSPC without disabling ordinary station use. Retrying that refusal succeeds
in the actual-helper fixture. Nonexistent-station status is not treated as
an ordinary resource refusal. Shared IWX AP BAID commands remain independent
of the primary station and retain both firmware removal layouts.

Complete Linux/macOS aggregates pass: 581 actual station/BA/cleanup scenarios,
206 state-worker groups and 118 context/status cases, plus both three-family
AP aggregation contracts and management transactions. The old AP static guard
initially failed on the changed call spelling; it now checks the actual wide
host-command ID, payload, length and sender. The executable shared-helper
matrix covers AP start/stop, both versions, invalid input/BAID and transport
failure. These are complete command/helper bodies against real firmware wire
headers, with transport, locks, nodes and hardware reclamation as fixtures;
no full BA callback, real DMA or radio qualification is claimed.

Equal production fingerprint on source and guest mirror:
9ca862383098d0e282b8bdff96ed53dd0b3d9fc60f561bc19c7e88f040bc5c39.
Built UUID: F2014422-EC2D-3A5D-9FF9-7F7F678EB4EF.
Mach-O SHA-256:
da8e0989734ef0efcbd2881da6563a373e0eb514f229e2ff4ecdb50edd4fb4ee.
Guest regression/build log:
/Volumes/AIAMBuild/itlwm-88c0d3f9/DerivedData-join-failure-20260910/ba-wire-20260911.CCJ4uz,
SHA-256 f4d6490e5b722562e92f8a4241855d317ed709e47ab16501d1e9974bcc678dba.
All 1085 external symbols resolve; no _thread_call_cancel_wait dependency.
This candidate is built, not loaded or released. Public/loaded b5c6cfd8 and
user physical hosts are unchanged. Continue immutable BA ingress, retained
physical BAID ownership and asynchronous main-workloop publication, then the
actual producer/reset and queue-allocation lifetimes described below.

## FIX_CANDIDATE: separate owned BA firmware results from node callbacks

The cfd9fe5d BA workers still combine firmware commands with node mutation,
reorder-timer actions and generic ADDBA callbacks. The latter reach the main
gate synchronously. Before moving their completion, separate the command
boundary: capture the retained station receipt, return an explicit BAID/error,
and never select MAC/station IDs or command length from a replacement node.
An admitted BA reader uses its ADD-incarnation receipt; RUN-stop cleanup uses
an exclusive retained station modification. Both must reach the existing
raw transport admission edge. A transport/response ambiguity cannot be
reported as an absent BA resource or silently release the physical station.

Cover IWM old/v7 and new station layouts, IWX legacy ADD_STA and BAID-ML
versions, invalid/missing BAID responses, reset and same-hardware replacement.
Preserve AP callers of the shared IWX BAID helper. This command separation
alone does not close main-workloop callback ownership, BAID host-publication
ordering, TVQM uncertainty or IWM reset/drain. Continue the whole asynchronous
BA transaction and reset lifetime before any runtime promotion.

The September 11 user-status turn was read-only, not another functional
closure. Revalidation found an over-broad uncertainty path in this WIP:
ordinary RX ADDBA resource refusal would quarantine the entire station.
Intel v6.12 `iwl_mvm_fw_baid_op_sta` explicitly returns ENOSPC for
ADD_STA_IMMEDIATE_BA_FAILURE. Preserve the active station on that definite
start rejection, but retain uncertainty for transport loss, a successful
response without a usable BAID, nonexistent-station and unexpected statuses.
The BAID-ML response contains a bare BAID, not ADD_STA status bits; an invalid
allocation result is not evidence that no firmware resource was created.

## Checkpoint — 2026-09-10 21:47 UTC (September 11 locally)

This continuation is PROGRESS; the preceding user-status turn was read-only.
The following production changes build, but remain explicitly unqualified WIP.
The installed/public b5c6cfd8 image and all physical user hosts are unchanged.

Station removal now retains acknowledged drain/flush/queue-removal progress
across retries. IWM skips already retired aggregation queues and IWX retains
the original management queue ID. Fresh ADD/actual stop clear that progress.
Both families retain a separate ADD-incarnation reader receipt through the
whole primary TX body (ordinary, direct SAE and association paths) and BA task.
Cleanup closes admission and cannot submit while those readers remain. A new
ADD cannot reuse the station while an old reader is outstanding.

The state worker closes admission before lower cleanup and defers the exact
request, not a reconstructed SCAN. Last-reader exit resumes asynchronously;
the post-deferral level check covers exit-before-notification. Scan and station
reader waits have distinct reasons, so a scan terminal cannot prematurely
resume cleanup. Temporary RUN-to-ASSOC/RUN cleanup reopens only on the same
successful main-workloop commit; final removal stays closed. Ordinary old-peer
TX does not depend on the cancelled credential policy, preserving protected
leave until physical closure. Direct SAE retains its additional admission and
doorbell checks. IWX also retains its existing task-gate reference through
the constructor, so stop_internal must drain it before resetting the rings.

Full Linux and macOS aggregates pass: 243 actual station/helper scenarios,
206 actual state-worker/replay groups, and the existing 118 context/status
cases. Management, both direct-SAE transport and association compatibility
contracts pass. The station-use tests exercise the full production admission/
release helpers and cleanup chains, not the full packet/DMA constructor.
The state tests execute the full workers and deferral/reopen functions with
fixture lower hardware calls. IWX lifecycle counters are a fixture boundary;
neither test suite constitutes radio, reset-timing or immutable-node proof.
Both unchanged 2af6ee65 cleanup admissions compile and fail the active-reader
removal assertion; both unchanged state workers fail the deferred-cleanup
assertion. Each negative control exits 134, not a compile failure.

Final production fingerprint (equal source mirrors):
35d042f8d38cd7183321ccfa571b669f52cd7ef42d2ddec6fd3e789cc932cb7d.
Built UUID: FC69FF10-708B-327C-B243-D70DEAAA8DD7.
Mach-O SHA-256:
ee505e090f60b42fc7c192b9c997362857938635d532816473a39308f33b382e.
Guest full regression/build log:
/Volumes/AIAMBuild/itlwm-88c0d3f9/DerivedData-join-failure-20260910/station-users-final-20260911.s1l06p,
SHA-256 8d14b04bb88987a551e5f496117d03ca4af3de2bfd8193bcbd118b9406f238a2.
All 1085 external symbols resolve; no _thread_call_cancel_wait dependency.
The earlier a09d2e73b1d8 build is superseded. Read-only runtime identity remains
boot 9C8DDC74, loaded UUID 9D5A9332 and STA address 172.16.66.212.

### Required next production work before loading

IWM stop_device still resets all ring slots without draining these readers.
Do not add a blind synchronous wait: splnet/splx are no-ops in timeout.c,
taskq_thread invokes callbacks on its own thread, and timeout_add/del call
the main command gate synchronously. Both BA workers still reach such timer/
generic callbacks and mutate borrowed nodes after command waits. IWX's
existing stop/task barrier can therefore also wait on a worker that needs
the main gate. Separate immutable BA hardware work from asynchronous
main-workloop completion, then close/drain the actual IWM hardware producer
lifetime before DMA reset. Merely checking a flag at the doorbell cannot
protect data->m and ring DMA published earlier during construction.

Queue creation still needs independent exact ownership: persistent IWM base
queues versus aggregation queues, IWX management/monitor queues and TVQM
allocated carriers. A submitted-but-uncertain TVQM response cannot free its
DMA or enter allocation retry as if nothing were created. Retain the whole
AUTH/TIME_EVENT/SESSION_PROTECTION and failure-producer prerequisites below,
then qualify the exact built image through GUI/open/WPA2/SAE/S3/AP before
updating the release. These reader changes are not a whole-layer closure.

## FIX_CANDIDATE: close and retire station TX/BA constructors

The retry-progress candidate passes 224 production-function fixture cases,
but those cases do not admit a concurrent packet constructor. Both TX bodies
publish mbufs into shared rings before their doorbell, and ring reset walks
all slots, including unpublished slots. IWM ordinary TX and both BA tasks
therefore need a lifetime covering construction, not a final flag check.
Intel [v6.12 mvm/sta.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/sta.c)
remove_sta_queue_marking first withdraws the queue
mapping and then synchronize_net() drains existing TX readers before reuse.
Apple request retirement does not replace that physical producer boundary.

Add a station-incarnation use receipt, close admission before state-worker
cleanup, and defer the exact state request until existing constructors leave.
The last exit signals asynchronous replay; a level recheck after deferral
closes the lost-wakeup interval. No leaf covers DMA, crypto or callbacks and
the worker must not synchronously acquire the main gate. Cover ordinary,
direct-SAE and association TX, plus the BA task body. This is still WIP:
actual reset/drain, immutable BA callbacks, queue-allocation uncertainty and
the remaining whole-AUTH prerequisites must be proven before loading.

## FIX_CANDIDATE: station retirement progress and family queue lifetimes

The 2af6ee65 tree still restarts drain/flush/queue removal after any failed
REMOVE chain. A queue already acknowledged as removed is then removed again;
a failed final REMOVE_STA also unnecessarily restarts drain against possibly
absent firmware state. Preserve completed drain/flush/queue steps under the
retained station lifetime, not a transient command receipt. A fresh station
ADD and actual device stop reset this progress; a cleanup retry does not.
Record progress only after a successful response for the exact receipt and
generation, before another yielding command. Exercise a successful first queue
removal followed by a failed second removal, and final station-removal failure.

Do not unify incompatible hardware queue lifetimes: IWM init_hw creates base
queues before the first ADD_STA and before scan admission opens; those queues
survive station replacement. IWM BA queues are separate. IWX management and
monitor queues are created after ADD_STA, and dynamic TVQM allocation has its
own allocated carrier and firmware-assigned queue ID. Their creation/retirement
owners, uncertain DMA lifetime, TX/BA producer fencing and immutable AUTH
preparation remain the following work in this same layer. This progress fix
is not permission to promote the still-incomplete whole layer.

## Current checkpoint — 2026-09-10 21:09 UTC (September 11 locally)

This goal continuation is PROGRESS. The prior status turn was read-only.
The previous complete tested/built WIP was committed and pushed as c3a8a218;
it did not replace the release or installed image. This continuation adds
real primary STA ownership and the physical cleanup command chain in both
IWM and IWX, not another report-only checkpoint.

### Station ownership and actual cleanup commands

ADD/MODIFY reserves a distinct Station receipt after validating the retained
active MAC/binding, hardware generation, common attempt, peer and mode.
Station ID and command length survive node replacement; the complete wire
value is built before transport yields. Both MIMO builders use the supplied
peer's NSS. IWM prepares queue masks locally and publishes them only after an
accepted reply; a queue update includes its required modify-mask bit.
An unknown ADD reply retains the physical owner without pretending STA_ACTIVE.
Binding removal and state-worker occupancy account for that owner.

IWM's whole station-removal command chain carries the retained receipt through
drain-on, TXPATH_FLUSH, drain-off, queue disable and REMOVE_STA. Queue-disable
errors are no longer discarded. IWX reserves the station before its flush/
queue-disable/remove chain; TXFLUSH remains set on failed cleanup and is
released by successful retirement (or actual stop). IWX raw queue/flush
helpers recheck the receipt before applying response-side ring retirement.
Drain and REMOVE no longer select IDs or mode from the replacement node.
AUTH unwind visits an uncertain station before attempting binding removal.
Actual stop clears station ownership; logical request cancellation does not.

The real ADD/drain/remove/flush/queue-disable bodies compile against the
actual host-command and firmware packet definitions. The 217 ASan/UBSan cases
include both station modes, HT/VHT/HE command paths, definite/uncertain
failures, reentrant ADD, stale-before-send, reset/replacement at each cleanup
edge, IWX scheduler versions 0/3/unknown, missing/failed responses, balanced
reply allocations and stale response retirement. Raw transport, locks, node/
softc storage and ring reclamation are fixture boundaries, not radio evidence.
Both unchanged c3a8a218 ADD wrappers independently compile and fail the exact
foreign-attempt acceptance assertion (exit 134). The current wrappers pass
that same selector. Full Linux/macOS aggregates pass; the state suite was
then extended from 150 to 174 groups to include independent STA occupancy.
Adjacent management, AUTH/ASSOC, duplicate-state, AP/scan-handoff and three-
family AP aggregation contracts pass.

Production fingerprint, equal across source mirrors:
2dbd36559aeedcdc0cf84a0b5f669d652a665631d2c2b2776c100452e2976a28.
Built UUID: 35BEFBA7-CD27-3D66-8E30-BA5A70EAB506.
Mach-O SHA-256:
8b621a0a9dc674ddf11773048701e088aa1e55a9cde7b9fd5b44f0ecb95e0cdc.
Guest full regression/build log:
/Volumes/AIAMBuild/itlwm-88c0d3f9/DerivedData-join-failure-20260910/station-owner-20260911.0AuopG,
SHA-256 eba9de30bed24b8b30db96ca017c3051432ac38b7b507f4d0f06b42e0be042be.
All 1085 external symbols resolve; no _thread_call_cancel_wait dependency.
This build used the 150-group state suite; the subsequent 174-group extension
changes tests only, not the production fingerprint. The 174-group suite and
adjacent management/AUTH/ASSOC contracts subsequently passed on macOS too.
The final read-only check retained boot 9C8DDC74, loaded UUID 9D5A9332 and
the ordinary STA address 172.16.66.212.

### Required continuation — do not load or promote as whole-layer closure

Queue creation and partial cleanup progress are not yet independent owners.
IWM still has direct BA queue/STA mutations and no equivalent primary TXFLUSH
admission fence. IWX management enable changes first_data_qid before its
yielding queue command; monitor injection and AUTH unwind after queue enable
still need owned queue retirement. A cleanup retry must not blindly repeat
removal of already-retired queues. The retained station receipt is necessary
but does not itself close these producers or progress edges.

The IWX DELBA loop now rejects an already-replaced attempt, but its unlocked
callback boundary is not immutable main-workloop node ownership. Generic
ieee80211_stop_ampdu_tx already performs BA teardown at committed transitions;
reconcile that owner rather than retaining a worker callback into a mutable
node. TIME_EVENT/SESSION_PROTECTION still need independent receipts and real
terminals. Whole AUTH preparation, partial RUN unwind, key/SAE failure
producers and the previously listed scan/epoch/three-family cleanup gates
remain open. Then perform exact-image GUI/open/WPA2/SAE/S3/AP runtime checks.
The guest/public b5c6cfd8 image and user physical hosts remain untouched.

## FIX_CANDIDATE: primary station command identity

Both ADD_STA wrappers still accept any STA_ACTIVE bit, even for a different
attempt/peer. The IWM builder changes global queue masks before submission;
both MIMO builders borrow ic_bss instead of the supplied peer. Drain/remove
uses the replacement node's MAC ID/color and current mode. Extend retained
physical ownership to the primary station, preserving exact MAC/station ID,
wire length, mode and attempt through commands and error completion. Bind
admission to the retained active MAC/binding owners, and prevent their removal
while a station ADD is pending or uncertain. Unknown ADD completion cannot be
silently retried as if no resource existed.

Intel's v6.12 sta.c uses the retained mvm_sta MAC/station IDs for drain and
remove, checks transport before firmware status, and orders drain, flush,
queue retirement and station removal under its station lifetime. Preserve
that real dependency in our yielding HAL; an ACTIVE bit is not a lifetime.
Actual station wrappers and sender boundaries require tests with peer/mode/
generation replacement and rejected/uncertain results. Whole queue/time-event
ownership and main-workloop immutable AUTH preparation are still required;
this work is not a qualified on-air failure-recovery result by itself.

## Source checkpoint — 2026-09-10 20:48 UTC

The preceding user-status turn was read-only, not a new functional closure.
The latest full Linux and macOS payload aggregates pass, including 118 real
MAC/binding/status-reply cases, 37 IWM sender cases plus DMA failure, 40 IWX
sender cases, and the existing 150 state-transition/167 STA completion cases.
Both unchanged 0fb68ba6 status decoders compile and independently fail the
allocated-CMD_FAILED-reply retirement assertion (exit 134). Those negative
controls are expected failures, not failures of the corrected aggregate.

Physical context validation now reaches the actual command doorbell under
the existing ordered leaves. Rejected-before-send differs from uncertain
post-submission completion. Both failed-status reply allocations are returned.
Full guest build passed with all 1085 external symbols resolved and no
_thread_call_cancel_wait dependency. Production fingerprint on both mirrors:
e347f05a0ea98acf3592b407a34e455e96fe2f57cccc384333df95fb824ddbbe.
Built UUID: 3FC4D6D7-D139-3979-9CD3-998A886307C3.
Mach-O SHA-256:
81e613ef917d0764cf01a00c5d688ac887acb1e81c28d994cb12c48a4fe201d2.
Guest regression/build log:
/Volumes/AIAMBuild/itlwm-88c0d3f9/DerivedData-join-failure-20260910/context-doorbell-20260910.4Sdm04,
SHA-256 e684af7f0e515d6731c15f2f12688ad0f40fe71b5d122583e390f80ff3b66aaa.

This is an explicitly WIP source checkpoint, not a qualified release.
The guest still boots 9C8DDC74 with loaded UUID 9D5A9332 (b5c6cfd8).
The public asset remains b5c6cfd8. No installation, reboot or user-host
access occurred. Continue whole AUTH station/queue/time-event ownership and
immutable preparation, failure producers and physical retirement before
exact-image GUI/open/WPA2/SAE/S3/AP qualification and release promotion.

## FIX_CANDIDATE: owned firmware-context submission edge

STA integration exposed an unclosed prerequisite in the new context wrappers:
the raw sender captures the hardware generation on entry, after the wrapper
reserved its context. A reset in between can therefore submit old bytes to
new firmware; a same-hardware logical replacement during allocation can also
cross the current MAC/binding snapshot. Completion-side validation is too late.
The existing scan path already demonstrates the required physical pattern:
q0 (IWX) -> selected-BSS -> scan leaf, validated through the actual doorbell.

Carry a copied context receipt/kind and cleanup permission into the host-only
command carrier. The real sender must validate the physical receipt and exact
generation at submission, and additionally the common attempt for a live ADD/
MODIFY. Old-owner REMOVE/disassociation deliberately does not borrow the newest
attempt. Allocation, mapping, response waits and callbacks remain outside the
leaves. Record whether the doorbell was actually issued: rejected-before-send
is not an uncertain firmware resource, while a missing post-submit reply is.
Apply this to the real MAC/binding wrappers before reusing it for STA/queues/
time events. Compile full senders and full caller wrappers, testing entry/reset,
allocation/mapping replacement, failed admission and cleanup after supersession.
This continues the same whole AUTH layer; it is not a substitute for STA
ownership or immutable main-workloop AUTH preparation.

The full status-reply path also retains an allocated reply on CMD_FAILED:
both send_cmd_status functions return EIO before free_resp. Include that exact
error branch in the context-response work. Compile both full status decoders
against the real packet definitions, supply valid/failed/missing/malformed
replies and count returned packet ownership independently of context ownership.

## Current checkpoint — 2026-09-10 20:22 UTC

This continuation is PROGRESS. The preceding user-status turn was read-only,
not progress. New production IWM/IWX MAC/binding ownership and state-worker
cleanup routing are implemented, tested on Linux/macOS and fully built.
The whole failed-join layer remains uncommitted, unloaded and unpublished WIP;
HEAD/origin remain 0fb68ba6 and installed/public source remains b5c6cfd8.

### Retained physical contexts and replay routing

ItlFirmwareContextLease retains an independent command receipt, hardware
generation, attempt identity, peer/mode, MAC ID/color, PHY ID/color, LMAC and
wire length. Reservation precedes the yielding command; failed transport
retains Uncertain ownership, while a definite rejected binding ADD releases
only that uncreated resource. ADD cannot silently accept another peer/attempt.
Reentrant commands cannot borrow an in-flight slot. Successful REMOVE and
actual device stop are the physical release edges; logical state invalidation
does not clear these owners. Old-generation/old-receipt completions cannot
mutate a successor's owner, flags or saved command.

MAC REMOVE uses only retained ID/color and action, with the rest zero.
Binding REMOVE uses the retained PHY/LMAC/command length and an empty MAC
membership census. Neither requires current ic_bss, node PHY or opmode.
MAC disassociation uses the retained MAC command rather than a replacement
BSS's parameters. The common MAC builders now use their supplied node instead
of silently borrowing ic_bss. Binding/STA/time-event dependency checks prevent
premature MAC retirement. AUTH unwind propagates cleanup errors instead of
clearing active flags after failed removal; deauth also visits uncertain
MAC/binding owners with no ACTIVE flag.

Both state workers now inspect physical MAC/binding ownership when generic
state still says INIT/SCAN. The former SCAN-to-SCAN early jump no longer
bypasses cleanup. A successful cleanup is recorded in the copied request;
failure or supersession stops continuation. Normal successful retained
cleanup does not schedule reset.

### Evidence and exact image

The complete production MAC common/fill/wrapper and binding wrappers compile
against real firmware headers in 72 ASan/UBSan scenarios on Linux and macOS.
Kernel storage/locks, ACK-rate input and transport replies are explicit fixture
boundaries. Tests cover mode/peer/attempt/PHY replacement, wire bytes, both IWM
binding lengths, status/transport rejection, reentrancy, stale completions and
reset followed by a newly created successor. Eight independent unchanged
0fb68ba6 controls compile and fail the intended ADD/REMOVE assertions (exit
134), across both resource types and families. Legacy caller-side flag setting
is retained in those controls so they reach the old functional defect.

Actual occupancy/worker/commit/deferred-state tests pass 150 groups. The 58 new
groups cover retained active/uncertain contexts from INIT/SCAN to INIT/SCAN/
AUTH, pending-owner visibility, cleanup failure and supersession. The lower
deauth body is a fixture boundary in that state suite; these tests do not yet
prove the whole station/queue/time-event teardown. Full payload regressions
pass on both platforms, including all 13 failure payload cases, 167 actual STA
command cases and the existing scan/AP/SAE/PMF suites. Adjacent management,
duplicate-state and both AUTH/ASSOC transaction contracts also pass.

Final production fingerprint, equal on host and guest:
771b66cd6e016169ea805f3091b6ef41219dead466ea097739c9b299dea15d4b.
Built UUID: DB385105-9A7A-3589-8EBA-81D2E7376AFD.
Mach-O SHA-256:
d0a83705ce41e3ee4cc6bf65b78636247ee895230b18ca9fab976294721a2bcb.
Complete guest regression/build log:
/Volumes/AIAMBuild/itlwm-88c0d3f9/DerivedData-join-failure-20260910/firmware-context-replay-20260910.oqcRZn,
SHA-256 2895d8cbcd3c2187e932eb990aaab73ecd7daf998b86d82740586f6737abf4d6.
All 1085 external symbols resolve; no _thread_call_cancel_wait dependency.
The intermediate c83ac3919ecc build is superseded. All test/build handles
for this continuation are terminal.

### Required next work, not closed by these tests

Continue the same whole AUTH ownership layer: independent STA/queue and
TIME_EVENT/SESSION_PROTECTION receipts, stable removal identities, immutable
AUTH preparation and same-hardware supersession/recovery. Current STA removal
still derives mode/IDs from a node; IWX outer removal can touch the replacement
node's BA state. Time-event helpers still use a global UID and void error
paths. The IWX management queue helper chooses and mutates first_data_qid
before its yielding enable command. These must join the actual resource owner.

The selected-BSS leaf does not serialize the entire generic node-copy body:
join_begin sets a marker, then node_copy overwrites ic_bss outside that leaf.
Therefore the new short snapshot leaves are not proof of immutable whole-AUTH
inputs. Capture prepared command facts on the serialized main workloop without
making the drained worker synchronously wait for that gate. Do not shallow-copy
the generic node or put firmware waits inside a leaf. IWM rate initialization
also takes its own RS leaf and descends into rate/LQ work; it is not a pure
value capture suitable for an enlarged selected-BSS critical section.

The previously recorded first WCL scan preparation, post-submit epoch
reconciliation, IWN dual-band identity, failure producers and three-family
cleanup terminals remain required, then exact-image GUI/open/WPA2/SAE/S3/AP
runtime qualification, commit/push and release. At 20:22 the guest still has
boot 9C8DDC74, loaded UUID 9D5A9332 and STA 172.16.66.212. No installation,
reboot, physical-user-host access or release mutation occurred. User-owned
local Build/ is untouched; host free space is about 2.24 GiB.

## FIX_CANDIDATE: retained primary firmware-context identities

The current IWM/IWX MAC and binding ADD wrappers return success for any
ACTIVE flag, including a different selected BSS. Their REMOVE wrappers read
the replacement node/PHY; AUTH unwind then clears flags even after removal
fails. Preserve the actual command identity and uncertain completion separately
from the latest state request. Reserve before the yielding command, retain
transport-uncertain results, and retire only on successful removal or actual
device stop. ADD must not silently reuse another attempt's context. REMOVE
must work without a current BSS, using the retained identifiers; failed binding
removal must prevent premature MAC retirement.

The exact Apple candidate/abort owner analysis above supplies the upper
lifetime contract. Intel's Linux v6.12
[MAC removal](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/mac-ctxt.c)
uses only saved ID/color and REMOVE action and clears uploaded only on success.
Its [binding implementation](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/binding.c)
uses PHY ID/color, band-selected LMAC and a MAC membership census; removing
the last interface sends an empty membership list. Use the real local firmware
headers and compile both complete production wrappers in the tests. Include
node/PHY/opmode replacement during completion, unknown transport outcome,
firmware rejection, reset, reentrant submission and dependency-order failures.

This is implementation work inside the whole AUTH cleanup layer, not a new
closed release. Independent station/queue/time-event ownership, immutable AUTH
preparation, stale-request cleanup/recovery and all runtime gates remain required.

The first complete-wrapper implementation passes 72 actual-function cases and
the full Linux/macOS aggregates; its c83ac3919ecc image builds with all 1085
symbols resolved. Eight unchanged-source controls independently fail the
wrong-ADD/wrong-REMOVE assertions. Before treating even its replay boundary as
complete, change both state workers to inspect retained physical contexts
when generic state still says INIT/SCAN. The old SCAN-to-SCAN early jump must
not bypass that cleanup. Retain the copied request's successful cleanup bit
and stop on failure or supersession; do not turn ordinary retained cleanup
into an unconditional device reset. Compile the actual occupancy query and
worker paths and exercise active/uncertain/pending ownership.

## Previous checkpoint — 2026-09-10 19:46 UTC

This continuation is PROGRESS. The preceding user-status turn was read-only,
not progress; authoritative source, branch, guest and published asset were
revalidated before resuming. Exact deferred replay now passes the complete
regressions, and inspection of the full AUTH callees led to new production
STA command/flush corrections, actual-function negative controls and a full
guest build. The whole failed-join layer remains WIP: no closing commit, push,
AuxKC installation, on-air qualification or replacement release is claimed.
HEAD/origin remain `0fb68ba6`; installed/public source remains `b5c6cfd8`.

### Exact retained replay and truthful lower command results

Both IWM/IWX defer the exact copied SCAN state request, including ingress SSID,
WCL/common generations and successful RUN-stop/deauth steps. Resume validates
that same value under selected-BSS -> scan leaf and atomically moves Deferred
to Queued. It does not call common begin_scan, repeat preparation, allocate a
new state serial or borrow the latest join. Lifecycle admission pins scheduling;
a post-deferral level check closes the release-before-waiter window. Foreground
physical/AP occupancy defers before lower cleanup; background replacement keeps
its actual abort path. An AP-deferred WCL ticket is no longer also rejected.

Complete actual prepare/defer/resume/progress/worker/commit tests pass 92 groups
on Linux and macOS, including retained lower steps, supersession, hardware/common
identity invalidation, AP and background occupancy, stopped interfaces, denied
lifetime admission, exact WCL ticket cancellation and lost-wakeup ordering.

Both ADD_STA helpers now publish STA_ACTIVE only for a successful transport and
firmware status in the original hardware generation. REMOVE_STA failures retain
station ownership; IWM also retains aggregate maps after drain/flush/remove
failure and checks the real drain status. IWX's complete flush wrapper no longer
swallows either drain failure. Both teardown chains stop old-generation work
before the next command or mutation; IWX additionally preserves the successor's
TXFLUSH state and avoids old BA cleanup after a reset.

The new suite compiles complete production IWM add/drain/remove and IWX
add/drain/flush/remove methods against their real firmware headers. Its 167
scenarios pass with ASan/UBSan on both platforms. Boundaries substitute kernel
softc/node storage and transport replies, not the methods under test. Coverage
includes transport/status rejection, successful retry at that fixture boundary,
update and monitor modes, independent removal steps, nested TXFLUSH, unchanged
adjacent state and reset at each command/callback edge. Unchanged `0fb68ba6`
independently compiles and fails six negative controls: each family's add and
remove, IWM drain status, and IWX flush error propagation (all exit 134).
These tests do not establish safe retry of an uncertain on-air command without
the still-required independent physical resource owner.

### Built image, test boundaries and retained debt

Final production fingerprint, equal on host and guest:
`655647213b4999ab39545439085aadb4f24b7f0aca0ebf0020a293e849f85d1b`.
Built UUID: `F8F7B9F3-6B49-3F4F-9DF6-5ECBBA19D877`.
Mach-O SHA-256:
`4d83382286471cedf150b8e4e6382a4c99f436c033f5638dee7ebb67e892d047`.
Build-log SHA-256:
`400035f55dcdddd96991d8e0420f2450ec87eaf8fe6ef29641e911815e95fcc3`.
All 1085 external symbols resolve; no forbidden synchronous-drain dependency.
The earlier same-turn `c3111a85d54c` build is superseded. Guest Build holds
UNQUALIFIED WIP; the loaded UUID is still `9D5A9332`, boot `9C8DDC74`,
STA address `172.16.66.212`. User-owned local Build/ remains untouched.

Full Linux payload regressions and adjacent physical/standard scan, PLTI,
reassociation, AP publication/handoff, AUTH/ASSOC, management, duplicate-state
and scheduler-station ownership contracts pass. Focused scan suites also pass
in macOS: 33 value groups in both language modes, IWM sender 22 plus DMA failure,
IWX sender 25, admission 64, terminal/replay 58, AP exclusion 31, 530 complete
builders and 20,000 concurrent common-plan replacements. The macOS full aggregate
also completed successfully after fixing test-only Bash-3 empty-array handling,
SDK explicit_bzero/endian adapters and cleanup of compiler-generated dSYM bundles.
An intermediate run returned zero despite a Bash-3 unset-array diagnostic and
skipped failure-payload tests; it was explicitly rejected as qualification.
The final complete log contains the 13 failure-payload cases, actual controller
dispatch and IWN wiring passes as well as every later scan/STA suite. The one
earlier test.dSYM leftover was inspected and removed from its exact private
temporary directory. These portability fixes are verification maintenance, not
functional closure. All test/build/archive handles for this continuation have
reached terminal state.

Next: preserve the separate AUTH-created MAC/binding/STA/time-event owner across
same-hardware request supersession. Current AUTH error unwind still ignores some
binding/MAC removal failures, and several builders borrow current ic_bss even
when passed a different node. A copied latest request or hardware-generation
check alone is insufficient. IWM rate-scaling alloc/free names were inspected:
the former initializes embedded sc->lq_sta storage and the latter is empty;
there is no heap allocation there to invent or retire. Retain actual resource
and node/PHY facts, do not fabricate a generic node-copy lifetime.

Separate WCL InitialQueued first preparation, post-submit SCAN epoch
reconciliation, IWN dual-band identity, all failure producers and three-family
firmware/key/SAE cleanup terminals remain required before real GUI/open/WPA2/
SAE/S3/AP validation and publication. No physical host or other VM was touched.

The new-build disk floor was restored by archiving exactly one old offline
overlay after a 379-image no-child census, no-open checks and independently
matching source/remote SHA-256. Only its local qcow2 copy was removed; it is
recoverable from 10.7.6.112, with companion files and base unchanged. See
[the exact restore manifest](TAHOE_SCAN_REPLAY_OFFLINE_ARCHIVE_20260910.md).
Free space is approximately 2.25 GiB; no snapshot was removed.

## FIX_CANDIDATE: truthful STA command completion before AUTH resource ownership

The complete IWM/IWX AUTH and teardown callees expose a prerequisite defect:
`add_sta_cmd` sets STA_ACTIVE in the `else` of `!err && bad_status`, so a
transport error can mark a station installed. Both REMOVE_STA helpers clear
STA_ACTIVE even on error; IWM additionally erases its aggregate queue map on
failed drain, flush or remove. IWM's drain helper also ignores a firmware
status rejection. A retained AUTH resource owner cannot rely on these results.

The exact Apple 25C56 `WCLJoinManager::handleSendCandidateToDriver` retains
the accepted candidate only after successful driver admission; its independent
`handleJoinAbortComplete` consumes the retained request before terminal
notification. Those are upper lifetime contracts, not Intel command layouts.
Intel Linux v6.12 [`mvm/sta.c::iwl_mvm_sta_send_to_fw`](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/sta.c) separately returns a
transport error before checking ADD_STA status, and rejects non-success status.
Use the actual local firmware command definitions for both families.

Correct ADD_STA publication and REMOVE_STA retirement only after success, keep
IWM queue ownership on errors, check actual drain status, and reject old hardware
generation completions after every yielding removal step. Compile the complete
production add/drain/remove functions with the real firmware headers; test
transport/status failures, retry, update, inactive/monitor, individual cleanup
steps and reset at each command boundary. Unchanged HEAD must compile and fail
the same assertions. This prerequisite does not provide a separate same-hardware
AUTH resource owner, stable old-node data, TIME_EVENT retirement or whole queue
cleanup. Those remain required before failed-join runtime/release qualification.

Following the exact IWX caller chain found an additional hidden-success edge:
`iwx_drain_sta` returns a status rejection correctly, but `iwx_flush_sta`
ignores every drain error except ENXIO and overwrites the final error with zero.
The outer REMOVE_STA path can then advance despite that rejection. Include the
complete IWX flush/removal chain in this prerequisite: propagate both drain
errors and stop old-generation continuation before another command, BA cleanup
or TXFLUSH restoration. Existing inherited-TXFLUSH behavior is not a new retained
same-generation resource owner and must not be presented as one.

## FIX_CANDIDATE: exact deferred state with retained lower progress

The 18:57 continuation is verified progress, but ordinary busy/AP scan replay
still replaces its saved generation with a fresh `ieee80211_begin_scan` call.
The complete node.c prepare/next functions show that this repeats BSS cleanup,
epoch advancement, scan counters and channel preparation. Both lower workers
can also finish RUN-stop/deauth before the scan wrapper discovers occupancy;
their generic `ic_state` has not committed SCAN, so repeating the worker repeats
those calls. The reference JoinRequest/FSM retains candidate identity through
progression; it does not authorize borrowing the newest request after a yield.

Add a Deferred stage to the existing exact state lease, with explicit completed
lower-cleanup bits in its copied request. Mark successful steps only for the
same common/state/hardware owner. Deferral retains that value; resume validates
it under selected-BSS -> scan leaf and changes Deferred -> Queued without new
common scan preparation or a new state serial. Retain the existing task-lifetime
admission before scheduling outside the leaves. A post-deferral level check
must close the release-before-waiter lost-wakeup window without spinning.

Defer a foreground request before lower cleanup when a foreground/AP physical
owner blocks it; background replacement must retain its exact abort route.
Do not reject a WCL ticket which was actually retained for deferred execution.
Reset/supersession must retire only the old deferred value. Tests must compile
the real defer/resume and worker bodies and count cleanup, queueing, immutable
request data, stale owners, AP/BG release and reentrant replacement.

This does not by itself fix the separate WCL InitialQueued -> first common
prepare boundary, post-submit epoch reconciliation, or superseded AUTH-created
firmware MAC/binding/STA resources. Those whole-layer obligations remain open.

## Previous checkpoint — 2026-09-10 18:57 UTC

This continuation is PROGRESS: it completes production wiring of queued scan
facts into IWM/IWX physical admission and real command senders, updates the
actual-function regressions, and completes the full guest build. The preceding
user-status reply was read-only/no progress. The whole failed-join layer remains
WIP, not a closed functional release. HEAD/origin are still `0fb68ba6`, and the
installed/public image is still `b5c6cfd8`. No commit, push, AuxKC installation,
radio test or replacement release is claimed for this checkpoint.

### Queued ingress, physical owner and exact retirement

`ItlStateTransitionRequest` now retains the WCL scan generation, fresh common
join generation, owned SSID and home/away value. Both actual prepare methods
capture common identity and scan facts under selected-BSS -> scan-leaf order.
Foreground wrappers pass that request to reservation; background reservation
captures its own contemporaneous identity. Physical admission checks the exact
state serial/hardware generation, common identity and matching WCL ticket,
then stores one separate `scanCommandPolicy` with its physical receipt.

The old free-standing `capture` method was removed. The three top-level Intel
builders copy only the reserved policy; IWX versions 12/14 still receive that
same value. Actual IWM and IWX senders revalidate common/state/WCL ownership
under the selected-BSS and scan leaves through the real doorbell. IWX retains
q0 outside those leaves. Abort bypasses the latest-intent check deliberately:
an obsolete physical command still has to be retired. DMA allocation/mapping,
acknowledgement waits, callbacks and scheduling remain outside the new leaves.
The existing selected-BSS/SAE and selected-BSS/PMF handoff sections were inspected;
they release their leaves before state continuation or deferred q0 work.

Attach/reset initialize or clear the policy. Successful unsubmitted rejection
and actual terminal claim clear the matching policy; stale rejection cannot
erase a successor, and submitted timeout retains the quarantined physical
owner until reset. Started/rejected WCL notifications now require matching
policy/ticket generation, including the explicit pre-reservation rejection.

`deferScanCommand` receives and validates the exact request instead of copying
the latest common attempt. A stale worker cannot reserve deferred work for a
successor. Workers no longer drop SCAN merely because a legacy scanning flag is
set when no physical/AP owner actually occupies the path.

Two additional ingress defects were caught during integration. Fresh-join
participation must be retained from ingress, not inferred again after its phase
advances; phase/epoch/census changes now reject that reservation. Also the real
`ic_des_esslen` is `int`, not the old fixture's byte. Validation now precedes
narrowing, with explicit negative and >255 cases. The fixtures use the real
field type and extract the complete production WCL plan declaration.

### Verification, exact build and limitations

All focused suites pass under ASan/UBSan on Linux and inside the macOS guest:
33 value-lease groups in each C++ mode; actual IWM sender 22 scenarios plus
the separate DMA-failure case; actual IWX sender 25; actual prepare/reserve/
copy/owner bridge 64; actual terminal/defer/replay 58; actual AP resource
exclusion 31; queued-state/commit 68; complete firmware builders 530; and
20,000 common-plan replacements with four concurrent readers.

The sender fixtures intentionally stub the common-owner predicate and verify
that it gates the real doorbell under ordered leaves, while the admission
suite compiles that predicate and its producer/copy methods themselves. The
byte-layout fixture starts from an explicitly reserved value; it does not
silently recapture policy inside its builder-copy stub. These boundaries do
not constitute one end-to-end radio test. Unchanged `0fb68ba6` common and both
family builders still compile as negative controls and fail the unlocked-copy
or mixed-channel assertion respectively (134, not a compile error).

Full host payload regressions and physical/standard scan, reassociation, PLTI,
AP publication/handoff, both AUTH/ASSOC, management and duplicate-state
contracts pass. Initial source-contract failures selected a call instead of
the sender definition or retained old signatures; the selectors were corrected
and the ownership requirements strengthened, not removed. Darwin additionally
needed explicit endian helpers and cleanup of compiler-generated dSYM bundles.
The one earlier failed run's two private temporary dSYM bundles were removed;
the repeated guest suite reached its successful terminal without leftovers.

Final source fingerprint, independently equal on host/guest before and after
the build, is
`345d8230d796746772803ec6e6499f4eb5cefcd1ca9207e193861c9c8c7106eb`.
UUID: `C1CD3D19-D9E0-3365-AF2B-8A5225D9920B`.
Mach-O SHA-256:
`4bfa757381466b6327f91406a539dc59b68ca936ef6aa1a1a6dda5ee45378eb5`.
Build-log SHA-256:
`2d8c7e39fd1f7997ef477af0fc895f7ca854196f10b25c32020c24f46f5b0259`.
All 1085 external symbols resolve; no forbidden synchronous-drain dependency.
The earlier same-turn `6e97c7cb85af` build is superseded by the final signed-length
correction. Guest Build/Debug/Tahoe holds UNQUALIFIED WIP, not the loaded image.
User-owned local Build/ is untouched.

### Next full-lifetime boundaries (not closed by these tests)

`resumeScanCommand` still retains only a generation and calls generic
`ieee80211_begin_scan` after releasing its admission leaf. The actual common
prepare function cleans the BSS, advances its association epoch, updates modes
and scan counters, then invokes a new lower callback. Replaying that call can
repeat preparation already performed before a physical-busy yield, and its
generation check is not an atomic queued-owner claim. Retain the exact deferred
request and completed preparation stages, or move admission before those effects;
do not substitute another unlocked last-current check.

Post-submit SCAN cleanup legitimately advances the epoch, so a blanket old-epoch
check afterward still breaks bootstrap. AUTH also allocates MAC/binding/STA
resources through multiple yielding calls while generic state may remain SCAN;
a superseded request can leave resources which its successor's state-based
cleanup skips. Those resources need their own retained owner, separate from
latest queued intent. IWN dual-band identity, device-dependent command inputs,
all failure producers and three-family firmware/key/SAE cleanup terminals remain
in scope before GUI/open/WPA2/SAE/S3/AP runtime qualification and publication.

All test/build handles from this continuation are terminal. Read-only checks
retain guest boot `9C8DDC74`, loaded UUID `9D5A9332`, address `172.16.66.212`
and owned QEMU PID 361779. Host free space after the build is 1,580,335,104 bytes,
below the 1.5-GiB new-build floor but above the 1-GiB running floor; recheck and
recover only verified disposable task artifacts before the next build. This
does not block source/lifetime work. Physical host 22 and other VMs/base disks
were not touched.

## FIX_CANDIDATE: exact queued ingress to physical scan admission

The preceding 18:17 continuation is PROGRESS, with production plan changes,
positive/negative actual-function tests and a complete guest build. Its remaining
physical-owner gap is concrete: reserveScanCommand samples the newest common join,
and the builders choose WCL policy from the newest HAL phase. A queued predecessor
can therefore borrow its successor's plan or join result without hardware reset.

Retain scan-specific ingress facts in the copied state request: exact WCL ticket,
owned generic SSID and persistent home/away value. Capture common identity and
publish the queue under selected-BSS -> scan-leaf order. Foreground physical
admission must receive that exact request, validate it and the common identity
atomically under the same order, and copy only its matching WCL plan or common
join ledger SSID. Background admission captures its own contemporaneous identity.
Store the resulting policy separately with the physical serial; builders only
copy that reservation, never mutable phase or desired-SSID state.

The real sender must validate queued/common ownership at its doorbell boundary,
while still admitting exact abort of an obsolete physical owner. Lifecycle reset
and physical retirement must clear only their matching policy. Late start/reject
notifications cannot borrow a newer WCL ticket. Deferred replay and post-submit
SCAN publication still require full lifetime handling: node cleanup itself
advances the association epoch, so adding an unconditional old-epoch check after
that legitimate cleanup would break bootstrap. Do not mistake pre-submit identity
checks for completion of those later boundaries or the AUTH firmware-resource
retirement layer. All remain under the full GUI/S3/AP runtime release gate.

## Previous checkpoint — 2026-09-10 18:17 UTC

This continuation made production changes, added complete firmware-builder
regressions and completed another full guest kext build. It is PROGRESS, unlike
the preceding read-only status reply. The whole failed-join layer remains WIP;
there is no closing commit, push, installation or replacement release. HEAD and
origin remain `0fb68ba6`, with loaded/public source `b5c6cfd8`. User-owned local
`Build/` is untouched.

### Coherent common plan and single request-policy value

Common `ieee80211_wcl_scan_plan_stage`, snapshot and clear now serialize their
entire payload operation with `ic_pae_selected_bss_lock`. Missing lock fails
admission; inactive/missing snapshots return zeroed output. Exact-generation
retirement remains unchanged, and no callback, allocation or firmware operation
runs under that leaf. An atomic active/generation reread alone was insufficient
to protect the old plain memcpy from concurrent clear/restage.

New host-only `ItlScanCommandPolicy` retains the copied plan, an owned generic
SSID when no WCL plan is selected, and one persistent home/away scalar. Both
IWM LMAC/UMAC builders and IWX's version dispatcher capture it before command
allocation. IWX v12/v14 consume the same value passed by that dispatcher.
All three channel builders consume the parent's exact plan rather than taking
a second mutable snapshot. Missing requested WCL policy returns ECANCELED before
allocation instead of silently widening to a generic scan. Firmware layouts,
band-specific filtering, active wildcard/passive selection and existing generic
foreground/background defaults are preserved.

This does NOT yet bind the capture to the queued state's physical reservation:
the entry still chooses WCL mode from the mutable HAL phase. The generic SSID
copy is owned through allocation, but that does not prove it belongs to the
original queued ingress. Nor does this freeze NVM/channel inventory/probe-template
inputs through reset. Those remaining ownership boundaries must not be inferred
from the complete-command byte tests below.

### Actual verification and build identity

`test_scan_command_policy.sh` compiles the complete common functions and actual
IWM/IWX scan/version/size/layout/channel methods against both complete production
firmware register headers and actual host-command structures. External allocation,
version lookup, probe-template generation and sender calls are fixture boundaries;
the assembled command bytes are independently decoded and checked.

The 530 builder cases cover all five command families, legacy/adaptive layouts,
both legacy tails, foreground/background, directed/wildcard/passive/generic mode,
replacement or retirement during allocation, replacement at version lookup,
owned desired-SSID bytes, channel/band filtering, bounded channel capacity, absent
WCL policy, allocation/probe/sender errors and zero/sentinel dwell defaults.
The common fixture additionally performs 20,000 replacements with four concurrent
readers, validates every copied payload and asserts that every production memcpy
holds the leaf. All pass with ASan/UBSan on both Linux host and macOS guest.

Independent negative controls compile unchanged `0fb68ba6` common functions and
each family's complete old builders. Common fails the unlocked-copy assertion;
IWM and IWX each fail the mixed-channel-policy assertion (all exit 134). Initial
negative fixture compilation lacked legacy two-argument declarations and unused
lock annotations; those compile errors are not the proof. The corrected negative
fixture only adapts its call signature and omits the unrelated new host-serial
assertion; it does not rewrite the old production bodies. Initial decoder errors
for v6/v7 union offsets were corrected against the complete real headers before
the positive matrix passed.

Full host payload tests, physical/standard scan lifecycle, reassociation, PLTI,
AP publication/handoff, both AUTH/ASSOC, management transaction and exact-duplicate
contracts pass. Source assertions now check leaf-serialized copies and the same
policy reaching each consumer; no existing active-wildcard or band rule was
discarded. Darwin fixture compatibility (libc wipe helper, pre-existing macros,
Bash 3.2 empty-array handling and temporary dSYM cleanup) was verified by actually
running the focused suite inside the guest. Whitespace validation passes.

Host and guest independently matched source fingerprint
`506f2296ccb54965c6701107b015587841f634eeef7b9e963081064a14932d3c` before
the complete normal AP-capable build. UUID is
`989402BA-C625-3097-BDED-03DAFF655EA5`, Mach-O SHA-256
`733edbbeb15fa938cd089842529323ed1f829f0aeb9e1b40ad3feb3ca2cdf452`.
Build-log SHA-256 is
`8a48e3f2f3154dcccaa2db685fad74d057c4dbf5d3259b6ca469b10e9d7ab26e`.
All 1085 external symbols resolve against the guest BootKC and the forbidden
synchronous-drain dependency is absent. Both source fingerprints were rechecked
unchanged after the build. The guest staging output is this UNQUALIFIED WIP,
not its installed/public image. No AuxKC admission, load or on-air test occurred.

### Next implementation boundary remains open

Carry the exact immutable state request into foreground scan reservation and
deferred replay; capture policy for that reservation, not the newest mutable WCL
phase. For a fresh common join, use the already-owned ledger SSID/identity, not
a later `ic_des_essid` sample. A queued WCL successor must not retag a still-live
boot scan. Preserve the physical owner through abort/terminal and validate the
same owner at the real sender doorbell. IWN also needs its exact plan identity
retained across the second-band continuation, not just coherent individual
snapshots. Device-dependent command layout/channel/probe values must be retained
or protected through yielding allocation/reset, independently of request policy.

The superseded AUTH/RUN firmware MAC/binding/STA resource owner and inline IWX
TLC overlap remain separate from the latest queued intent. Complete actual
failure-producer wiring and three-family firmware/key/SAE cleanup terminals,
then the full GUI/open/WPA2/SAE/S3/AP runtime gates, before closing this layer.
No user blocker was encountered. All current test/build handles are terminal.
The read-only lab check retained boot `9C8DDC74`, loaded UUID `9D5A9332`;
host free space is 1.6 GiB (recheck the existing 1.5-GiB start floor before the
next build). Physical host 22, other VM/base disks and loaded kext were untouched.

## FIX_CANDIDATE: coherent scan policy through firmware assembly

The 17:50 status turn was read-only, not progress. The next source audit found
two concrete hazards on the still-open physical scan path. The common plan's
active/generation recheck does not serialize its plain payload memcpy against
clear followed by restaging; it is a data race, not a coherent snapshot.
Serialize stage, snapshot and exact clear with the existing selected-BSS leaf,
whose attach/detach lifetime already covers these consumers. No allocation,
firmware call or callback may run under that leaf.

Both IWM channel builders and the IWX channel builder currently take another
mutable WCL snapshot after the command header has selected its SSID and dwell
times. IWX v12/v14 dispatch takes yet another one. Freeze bounded request values
once before command allocation and pass that same value through version dispatch
and channel construction. A missing requested WCL plan must reject the command,
not silently widen it to a generic scan. Preserve all firmware wire layouts,
active wildcard/passive semantics, band filtering and default dwell policies.
Actual production-function tests must force replacement between header and
channel construction and independently exercise concurrent common plan access.

This is a required part of the whole failed-join layer, not closure: physical
reservation must still inherit the exact queued request; generic desired-SSID
capture needs that ingress identity, and firmware resource retirement, failure
producers and live open/WPA2/SAE GUI/S3/AP qualification remain required.

## Current checkpoint — 2026-09-10 17:40 UTC

This continuation changed production code and completed two more full guest
builds. The preceding status reply was read-only, not a closed functional layer.
The full failed-join change remains WIP: no closing commit, push, installation
or replacement release. HEAD remains `0fb68ba6`, loaded/public source
`b5c6cfd8`. User-owned local `Build/` remains untouched.

### Queued state identity and asynchronous generic commit

Both real newstate ingress/worker pairs now retain one immutable 64-bit serial,
hardware generation, state/argument and atomically copied common join sequence,
active generation and selected-BSS epoch. Reset invalidates the slot without
reusing its serial. Duplicate suppression compares that entire request and keeps
SCAN/AUTH exceptions; the mutable legacy ns fields are only compatibility data.
The value stages are Prepared, Queued, Executing, Pending and Committed, so a
second producer/take/callback cannot replay an already claimed result.

Worker lower results reach a provider-less IOInterruptEventSource on the existing
main workloop. No worker waits for that gate. The consumer claims a copied value,
revalidates it on the workloop and calls the generic state machine there, where
its normal if_start can enter the recursive TX gate. Inline AUTH-to-ASSOC uses
the same owner/claim path only when already in that gate; IWX still submits TLC
first. Superseded results and reentrant replacements do not request recovery.
An actual current lower/generic failure closes physical/state admission before
queueing the existing reset worker. Source withdrawal precedes detach drains;
an already retained source can only signal its detached event object.

IWM workers and enqueuers now hold its existing lifetime counter through their
actual use. This lifetime admission intentionally permits the closed SAE phase:
initial SCAN is required before iwm_init opens SAE TX. Physical/state admission
still requires the reopened scan epoch, and permanent detach rejects the lease.
Detach drains and removes queued newstate callbacks before freeing rings.
IWM's pre-existing RUN diagnostic boundaries are preserved, with the final
commit marker moved to its actual main-workloop consumer.

### Verification and exact build identities

`test_state_transition_request.sh` compiles the actual common identity getter,
both HAL mailbox/lifecycle methods, both dispatchers and both full ingress/worker
bodies. Its 68 ASan/UBSan scenarios cover supersession before queue/after lower
work/before delivery, cancellation/epoch/generation change, one-shot claims,
inline and queued ASSOC, IWX TLC reentrancy, error quarantine, replacement during
failure/generic callbacks, initialization before SAE admission, stop/detach,
late event-source references and allocation/admission failures. External IOKit,
firmware and generic state operations are fixture boundaries, not radio proof.

The unchanged `0fb68ba6` ingress/workers were independently compiled and run for
each family against the same fixture. Both reached and failed the outside-main-
gate generic-commit assertion (exit 134). An initial negative compile lacked two
legacy fixture declarations; that compile failure is not the negative proof.
The corrected 68-scenario run and the full payload suite pass. Adjacent AUTH/
ASSOC, management transaction, duplicate request, public initial-BSSID, driver-
resident IWM SAE/PMF, status-30 preparation, AP firmware resources, IWX epoch retry,
AP handoff, three-family reset ordering and beacon-loss contracts also pass.

Two stale source expectations were corrected from evidence, not removed: IWM
BTM admission already shipped in `09202d0b` with its driver-resident roam hook,
and the PLTI scan test's prefix accidentally selected the new controlled wrapper
instead of the complete owned end-scan body. Their actual capability/hook and
AUTO_JOIN/SAE hold assertions are retained at the correct producers. Moving
generic state publication likewise moves the failure/management-drain checks
to the exact mailbox consumer.

Host and guest content fingerprints matched before both complete builds:

- Snapshot `74030a1fae273baf29edb5b429b5cb1aec6a54f2397c3ebd03966254f171aa61`:
  UUID `3C1EEAD9-D9E0-3A08-BA87-24E3F4DC26A4`, Mach-O SHA-256
  `cfbe0c8f190feebce39ad3f0fdbfab942fb49122abb598d2db1359463ce18e00`.
- Final snapshot `e5a6fd398c1f6ccb964631da4e3611243b9597e52be08e2b1c97565e876267fd`:
  UUID `815A3A8F-6379-35EF-BE36-2B7A6DA5DAE2`, Mach-O SHA-256
  `09f74f6656035461f085cac6fb1bd90e08bc8bf050f218989a379a21d3b0ae43`.

Both resolve all 1085 external symbols against the guest BootKC and reject the
forbidden synchronous-drain dependency. Final build-log SHA-256 is
`5823b94cddb34d88ed5036b204d91aa769c909ff51c0eaf0ca0702171d23047e`.
The final source fingerprint was rechecked unchanged after the build.
The staged guest output is this unqualified WIP, not its installed kext.

### Next required boundary — actual lower resources, not just publication

The copied state identity is not yet carried into physical foreground scan
reservation, immutable scan-plan construction or deferred replay. SCAN's special
state publication still occurs in its lower wrapper; bootstrap init waits for
it before SAE opens, so moving that publication onto the main gate requires
auditing the init wait, not another blocking worker-to-gate call.

Likewise the latest queued intent is not itself the owner of resources already
created by a superseded AUTH/RUN operation. Generic state may still say SCAN
while that old worker has installed MAC/binding/STA resources. Before the next
AUTH is admitted, the actual old lower owner must finish/retire those resources;
checking only ic_state or dropping the obsolete generic callback is insufficient.
Inline IWX TLC must also not overlap a still-executing older lower transaction.
Carry exact ownership through all yielding firmware leaves and AP handoff
completion. These are required implementation steps, not user blockers.

Actual AUTH/ASSOC/watchdog/SAE/PAE failure producers, complete three-family
tagged firmware/key/SAE cleanup and received DEAUTH/DISASSOC sequencing remain
open as recorded below. New code must pass the whole open/WPA2/SAE GUI/S3/AP
runtime gates before this layer closes. Read-only lab verification at 17:39 UTC
retained boot `9C8DDC74`, loaded UUID `9D5A9332` and STA 172.16.66.212.
No host-22 access, other VM/base mutation, kext unload or reboot occurred.

## FIX_CANDIDATE: queued lower-state request identity

IWM/IWX currently read `ns_nstate/ns_arg` as unrelated mutable fields and only
test SHUTDOWN after yielding firmware calls. Another accepted join or an inline
AUTH→ASSOC can supersede that worker without changing the hardware generation.
The old worker can then publish its state, enqueue a management frame or reset
the new request. The existing join ledger and selected-BSS epoch provide the
identity; retain their atomic value snapshot in a non-wrapping lower-state
serial. Queue publication/take and reset invalidation use the scan leaf.

The final generic state commit must run on the existing main command gate and
recheck the immutable request there, rather than checking on a worker and
calling the generic state machine later outside ingress serialization. Keep
AUTH→ASSOC on its originating gate, including IWX's required TLC submission.
Firmware resource operations remain outside leaf locks. Recheck after yielding
lower operations and before scheduling recovery, so obsolete work cannot
publish or reset a successor. Exact scan-plan admission, failure-cleanup tokens
and hardware resource retirement remain part of the same whole-layer gate.

The final delivery must not use blocking `runAction` from the state worker.
IWX stop closes task admission and drains that worker while the caller may own
the main workloop gate; waiting for the gate from the drained worker forms a
cycle. The shipped IOKit `IOInterruptEventSource` explicitly supports a null
provider and client `interruptOccurred` for asynchronous workloop delivery.
Use that source for a copied, one-shot result, withdraw it before detach drains,
and recheck request identity on the main workloop. Inline AUTH-to-ASSOC can
consume the same mailbox immediately only when already inside that workloop.
It must still supersede older queued work and preserve IWX TLC-before-ASSOC.
Lower failures quarantine state/scan admission before requesting hardware
recovery, so the deferred reset cannot race an admitted lower successor.

## Current checkpoint — 2026-09-10 17:00 UTC

This continuation made production changes and completed two full guest kext
builds. The preceding user-status turn was read-only, not implementation
progress. HEAD/origin remain `0fb68ba6`; the whole failed-join layer is still
uncommitted and unqualified on-air. User-owned local `Build/` was not touched.
The source is now buildable; older statements below about two-argument scan
calls and disconnected final notifications are historical and superseded.

### Connected physical scan/abort and AP admission

Both IWM/IWX actual foreground/background wrappers reserve a host serial before
calling their firmware builders. Sender publication, upper readiness and final
RX notification reconcile through that same serial. RX preserves UMAC UID and
completed/aborted status; iteration notifications are not terminals. Physical
flags, WCL tickets and exact abort waiters retire before upper callbacks.
Abort commands carry the same host-only serial through the real sender guard;
the blocking waiter uses the matching sleep mutex, serial and hardware epoch.
A timeout quarantines the physical owner rather than clearing another waiter.

The background-only abort decision is now inside that leaf reservation. It
sees a reserved/unready background command even before BGSCAN publication and
returns EBUSY without blocking its original readiness producer. It cannot abort
a replacement foreground scan. Ordinary foreground scans retain the accepted
successor when the old background producer is still busy.

The shared value owner now also has an exact AP serial, using the same
never-wrapping host sequence. Both real lower AP start routines reserve it
before their first PHY/beacon/MAC command. Neither scans nor WCL initial or
background admission can overlap it. Only successful lower stop releases it;
ambiguous start/stop errors quarantine admission and schedule existing hardware
reset. Software runtime clearing is not treated as firmware retirement. AP
stop/readiness paths preserve pending ordinary join replay, including IWX's
queued-start interval and IWM's first-command/still-Idle interval.

### Tests and actual build evidence

The focused suite passed with ASan/UBSan: 33 value scenario groups in each of
C++11/C++17, 20 actual IWM sender scenarios plus the dedicated DMA failure
case, 23 actual IWX sender scenarios, 26 actual admission/reset/AP helper groups,
50 actual terminal/replay/background-abort groups, and 31 actual lower AP
start/stop groups. The latter compile the complete production resource methods,
model only external firmware operations and exercise every start/stop command
error position, pre-doorbell scan occupancy, first AP command while stage is
Idle, retained quarantine, and both beacon/MAC orderings. They do not claim
firmware-side effects or radio timing from these models.

The optional `SCAN_AP_RESOURCE_NEGATIVE_REF=0fb68ba6` control compiled the old
AP resource methods against the same fixture and failed the first physical
ownership assertion (exit 134). Corrected methods pass. Full payload tests,
physical/standard WCL scan lifecycle, reassociation/roam, default AP publication,
AP firmware-resource, IWX lower-epoch retry, bounded AP handoff and three-family
retained-recovery ordering contracts passed. A legacy handoff test's flag-clear
assertion moved to the actual atomic physical claim, preserving its ordering
check before the AP completion callback. Whitespace validation passed.

Two WIP source snapshots were independently hashed on host and guest using
sorted source-v2 file paths and SHA-256 contents, including untracked source
headers. The normal committed-tree identity helper deliberately does not hash
WIP, so these builds use the explicit fingerprint override rather than falsely
identifying the dirty guest checkout as its old HEAD.

- First snapshot `2144b227dce3c0669bc51991cc662aa79d3afd878a597068a9db5fd8ce118408`
  built UUID `6576B97B-6C5C-3D00-B39D-12F539263355`, Mach-O
  `9f54d4c63e5973f606e8aed4fe5ffe821cfd808a3a533ae538f4a2b1027dc8f0`.
- Final AP-admission snapshot
  `0125633abc190405977d7d1c2bbc95923836049e2eb83ff6dd45c5f35e909ae3`
  built UUID `12FE4196-43FB-3A14-85C6-899700BD1FB5`, Mach-O
  `fd2e5462155143b465932e18ccead7eed6f62f710c8dc66df1f3d3c0d5301878`.

Both complete builds passed all 1085 BootKC external-symbol resolutions and
the forbidden synchronous-drain dependency check. The final guest build log
SHA-256 is `ba82ee69a497fde8e0754a87b6e5f6b1f7852c39136cf174230ec1a65110d526`.
The final source fingerprint was rechecked unchanged after the build. No AuxKC
admission, install, unload, reboot, new live radio test or public release was
performed. The guest staging output now contains this UNQUALIFIED WIP binary;
it must not be confused with the installed or published image.

### Remaining whole-layer work

The next implementation boundary is exact 64-bit queued newstate provenance:
`ns_nstate/ns_arg` still lack a request token, and the scan wrappers' hardware
receipt checks alone do not fence a newer accepted join across yielding calls.
Deferred replay's common validation is also outside the eventual queued state
commit. The AP handoff completion still needs exact physical-serial provenance,
and complete after-yield AP/state resource retirement is not proved by the new
admission-only tests. These are implementation debts, not user blockers.

IWM/IWX still need tagged failed-join cleanup callbacks before passing their
physical join generation into `end_scan_owned`; otherwise NO_NETWORKS would
wait for absent cleanup participants. Actual AUTH/ASSOC/watchdog/SAE/PAE failure
producers, complete lower firmware/key cleanup (including IWN's currently
asynchronous general ASSOC/KEYS cleanup), and received DEAUTH/DISASSOC CB20→D5
sequencing remain required. Full open/WPA2/SAE, GUI, S3 and AP runtime gates
remain unchanged. No partial fixture/build result closes this layer.

Read-only lab verification at 17:00 UTC retained boot `9C8DDC74`, loaded UUID
`9D5A9332` and STA address 172.16.66.212. Public/loaded source is still
`b5c6cfd8`. Host free space is 1.8 GiB; no physical host-22 access, other VM,
base-disk mutation or deletion occurred. All test/build handles in this
checkpoint reached terminal status; there is no unresolved wait to resume.

## FIX_CANDIDATE: reserved background-scan cancellation

The physical scan receipt is now connected through the IWM/IWX foreground and
background wrappers, RX final notifications, exact terminal claim, abort
doorbells and abort waiter. Those integrations supersede the historical
16:05 checkpoint below. The full payload suite and 32 actual terminal/replay
scenario groups passed, but no complete kext build or runtime qualification
has yet been performed for this unfinished layer.

The next verified gap is the background-abort callback's legacy BGSCAN flag
test. That flag is published only after a sleeping command sender returns;
an already reserved/submitted background scan can therefore be misreported
as absent. Conversely, a separate flag check followed by a generic abort can
hit a replacement foreground command. Move background-only admission into
the exact scan-leaf reservation. An unready background producer remains busy,
not falsely completed and not synchronously waited on while its caller must
still publish readiness. Foreground scan requests retain their accepted
successor when this producer is busy. Tests must compile the actual abort
entry points and exercise both pre-doorbell and pre-readiness intervals,
foreground replacement, final completion, timeout and reset.

Queued newstate provenance, AP reservation, three-family failure producers
and terminal cleanup remain required parts of the same functional layer.
This is not a closing commit or a replacement release.

## FIX_CANDIDATE: one physical admission boundary for AP and scan

The lower AP start routines check SCANNING/BGSCAN before their first yielding
firmware command. A scan reservation exists earlier than those flags, and the
AP resource stage is likewise still Idle during its first command. The two
checks therefore do not provide the exclusion established by Linux MVM's
serialized AP/scan resource transactions. Use the same scan leaf to reserve
an exact AP serial before the first firmware command; scan reservation rejects
that AP owner until successful lower stop or reset. A failed command/teardown
retains the AP reservation and quarantines admission until actual hardware
reset, rather than trusting a software runtime reset as firmware retirement.
Reset invalidates both kinds without reusing the host serial. No new AP
capability, radio service or external-client qualification is implied.

## User-facing boundary and route

Route: REUSE_REFERENCE_DECOMP, with common net80211/Intel terminal glue.
The published `b5c6cfd8` correction removes the reproduced stale WCL connection
after a failed roaming replacement. Its subsequent fresh saved-WPA3 attempt
still reached the wrong-password BSS and waited until airportd's association
timeout. Another saved WPA2 profile eventually restored service. Full exact
timings and external SAE rejection are in
[the failed-roam qualification](TAHOE_FAILED_ROAM_WCL_TEARDOWN_20260910.md).

Airportd's chosen channel-13 scan record is not by itself the final driver
candidate. The separate WCL candidate selector populates and filters a list,
adds deny-list/preference information, sorts it and supplies candidates to
WCLJoinRequest. Its comparator uses deny-list status, preference and adjusted
RSSI. An external stronger same-SSID bad-password AP can therefore be relevant
to that later selection even when the earlier userspace record names another
BSS. No live ingress identity observation yet proves which selector supplied
the bad BSSID in this run; a local target-override bug is not established.

The next functional unit is the complete candidate-specific failed-join
terminal and native progression to another candidate, not a new SSID retry
timer or an unconditional override of the reference candidate selection.

## Exact reference recovery and limitations

The input is this guest's 25C56 BootKC, SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.
The read-only batch covers 373 exact nm-bounded functions of JoinAdapter,
WCLJoinManager, WCLJoinRequest, WCLJoinCandidate, WCLJoinCandidateSelector,
their FSM bases and direct status/publication neighbors. It used 40 actual
parallel decompiler interfaces on the 48-CPU reference host. At admission,
72 GiB was available and the sampled interval had no swap-in/out or I/O wait;
historical swap occupancy was not mistaken for current swapping.

Output is retained at reference host `10.7.6.112` under
`/home/dima/Projects/ghidra_output/aiam_roam_policy_25C56_20260910.Pthh3J/kernel-join-candidate-fsm-v2/`.
Manifest SHA-256:
`cd018d7c86a96dbbcbb238dad69e739ad2bb3e501e86ff75382ca1659934ee4f`.
All submitted functions produced decompiler results; that is not an assertion
that every inferred C type or branch is correct. The simulated-join-substate
function retains an invalid data-read warning and is not claimed complete.
OSMetaClass/OSObject constructors, memcpy and named returning helpers had
incorrect no-return metadata in the saved database. The batch corrects those
annotations only in its discarded read-only analysis transaction. Real panic
and stack-check failure remain no-return.

The exact initializer at `0xffffff80021d5362..0xffffff80021d53e4` assigns
config `0xffffff80023e2d68`, table `0xffffff80023dcf80`, actions
`0xffffff80023dd040`, states `0xffffff80023dd150`, events
`0xffffff80023dd180` and packed dimensions `0x11061000`: six states,
16 events and 17 actions, initial state zero. Init at `0xffffff80021d16e2`
supplies 20 subscriptions starting at `0xffffff80023dd210`.

Reading that table from the saved Ghidra memory produced damaged state/event
strings and an invalid cell. A separate read-only Mach-O fileset reader maps
all 206 headers and 1451 file-backed segments, rejects conflicting mappings,
decodes only verified level-zero kernel-cache chained pointers, and reads the
unmodified original binary. It recovered every one of the 96 cells and all
20 subscriptions with valid ASCII names and dimensions. The raw 192 table
bytes hash to
`65e53cbb409911490916de1c3a68d61bbd98bf7ea5e63effbca7f9caed133b82`.
The full raw extraction and reader are retained alongside the batch as
`join-fsm-raw-table.tsv` (local evidence copy), `join-fsm-table.tsv` (the rejected
Ghidra-memory output), and `read-join-fsm-raw.py`. The original binary, not
the rejected memory table, is authoritative for these cells.

## Recovered failure sequence

`JoinAdapter::handleAuth` at `0xffffff800155c41c` updates its exact BSSID/peer
record, maps actual firmware status/reason and publishes `0xd3` with the
28-byte auth/association payload when authentication failed. It does not
require successful association or RUN for this failure publication.
`handleAssoc` at `0xffffff800155c89e` owns the analogous association result.

`handleSetSSID` at `0xffffff800155cd1e` publishes the separate 20-byte `0xd4`
first-beacon result. On failure it invokes `sendConnectComplete` at
`0xffffff800155d346`, which requires its active, not-yet-completed ledger,
copies the status plus ten candidate records into a zero-initialized 164-byte
`0xd5` message, then retires the active/completion flags. A success-only
connection-complete payload or a bare BSSID is not an equivalent failure.

The raw subscription table maps `0xd3` to `authAssocCompleteEventHandler`
at `0xffffff80021d30b2`, `0xd4` to the first-beacon handler at
`0xffffff80021d313a`, and `0xd5` to `connectCompleteEventHandler` at
`0xffffff80021d31bc`. The last requires an exact 164-byte payload.
From IN_PROGRESS, auth/association complete enters ASSOC_DONE. Connection
complete from either state enters CONNECT_COMPLETE and runs
`handleJoinConnectComplete` at `0xffffff80021d4f08`.

That action updates the candidate result and asks `isJoinProcessDone`
(`0xffffff80021d2dcc`). If the failed request has another admitted candidate,
the native FSM emits TRY_NEXT_CANDIDATE (or its defined temporary-rejection
delay variant) and re-enters IN_PROGRESS; exhausted/completed requests enter
IDLE through JOIN_COMPLETE. HALTED/ABORTED states ignore late ordinary join
results and retain their separate abort-complete owners. Thus adding only
`0xd3` would leave out the terminal that actually drives ordinary progression.

`WCLJoinRequest::fillAssocCandidatesList` at `0xffffff80021fc3d6` chooses its
current candidate and delegates to `addAssocCandidates` at
`0xffffff80021fc854`. The latter writes count `+0x218`, BSSID `+0x220`, paired
MAC `+0x226` and channel `+0x22c` using stride 0x12. These are distinct from
the original public-request/context BSSID and must not be conflated.

## Proven local gap and remaining implementation gate

The current controller's auth/association completion explicitly admits only
validated S_ASSOC success. Its connect-complete builder requires S_RUN and
produces a zero-success record. The management timeout instead advances the
association epoch, revokes the selected transaction and enters SCAN; it only
publishes the separate reassociation failure when such a roam owner exists.
There is no ordinary failed-join event in that lower-to-controller dispatch.
This is a confirmed missing reference-facing failure path, but an on-air
after-fix test is still required to prove the effect on observed recovery.

Before implementation, complete the status/extended-reason mapping (including
`mapBcomSsidEventToAppleStatus` at `0xffffff8001625be7`), the first-beacon and
connect-record field lifetimes, all local AUTH/ASSOC/key/preflight failure
producers, and the cleanup ordering across epoch cancellation and a new join.
Preserve accepted association-comeback handling and distinguish explicit
abort/off/sleep from an actual candidate failure. A borrowed node/credential
pointer or callback for an old epoch must never reach the next connection.

Verification must exercise production functions with stale/duplicate/reentrant
negative cases and preserve the existing success/roam/PMF contracts across
IWN/IWM/IWX source paths. Exact-image runtime must repeat the controlled bad
SAE BSS with usable same-ESS alternatives, observe the actual incoming WCL
candidate and terminal sequence, and show native recovery with DHCP and
bidirectional traffic without off/on. WPA2/open failure controls, GUI saved
profiles, real S3 and native AP regressions remain in scope. No production
change or new failure-progression runtime pass is claimed in this report.

## Completed SET_SSID mapping and named ingress observation

The follow-up batch `kernel-join-candidate-fsm-v3` submitted 374 exact
functions and created 40 decompiler interfaces. Its manifest SHA-256 is
`ec02fbd4ad0f7e5dfbfda7d82085f07689ec9988c118858d848012d0b697c1aa`.
The original input/project remain unchanged. The simulated-join-substate
warning above remains; it is not part of the ordinary failed-join route.

`mapBcomSsidEventToAppleStatus` at `0xffffff8001625be7` does not blindly
overwrite a prior AUTH/ASSOC error with a generic SET_SSID failure. It maps
the firmware status, changes mapped success with a nonzero reason to generic
failure, and updates an unset status or a success/failure disagreement. When
both old and new results are failures, it preserves the earlier specific
status. The neighboring mapper at `0xffffff8001625b82` embeds the table in
instructions, not the damaged Ghidra data segment: raw statuses 0..14 map to
`0,16,16,1,16,1,1,1,16,16,16,16,1,1,16`; others map to 1.

The AUTH timeout/no-ACK secondary states are 1002/1001; ASSOC uses
1004/1003. Actual peer status is distinct from the firmware event status:
the former can override the 16-bit overall result, while the mapped raw
event/status pairs occupy their own candidate fields. SET_SSID raw status 3
uses secondary 1000. The Broadcom-authored
[Linux firmware-event definitions](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fweh.h)
identify 3 as NO_NETWORKS, 4 as ABORT and 5 as NO_ACK. Thus secondary 1000
here must not be called an explicit user abort, and 1001/1003 must not be
assigned to every peer rejection. The WCL abort-complete event remains its
separate owner. An initial draft's names were corrected before implementation.
Neither errno ETIMEDOUT nor an IEEE status may simply be copied into all
these fields.

Candidate records have stride 0x44: BSSID at +0, paired MAC at +6, AUTH
seen/status/reason at +0x0e/+0x10/+0x14, ASSOC seen/status/reason at
+0x18/+0x1c/+0x20, and first-beacon seen/status/reason at +0x24/+0x28/+0x2c.
The 0xd3 payload combines AUTH and ASSOC facts; 0xd4 reports first-beacon
facts. The ten 16-byte records in 0xd5 copy the ASSOC pair at +0x1c/+0x20,
not the first-beacon pair. No unsupported phase is fabricated as a received
response in the proposed Intel translation.

The remaining named live fact is the actual first WCL candidate versus the
target passed to `ieee80211_sae_wcl_request_begin`. Existing native logs name
an earlier userspace candidate and external captures name the on-air peer;
neither exposes both sides of this ingress boundary. The read-only bounded
observer `join-candidate-ingress.d` therefore reads only the already recovered
count, first/context/paired BSSIDs, channel and auth selector, and the direct
SAE begin BSSID. It records the optional WNM retarget decision and function
results. It never reads the credential window, retains pointers or writes
kernel state. Its purpose is to determine whether WCL supplied the bad BSS
or an Intel-side retarget changed it, not to infer the missing FSM from logs.
No new production source change is included in this observation checkpoint.

An independent Capstone decode of all original bytes in the six exact
AUTH/ASSOC/SET_SSID/connect/mapping symbol intervals verifies these field
copies and mapping branches without the saved Ghidra listing's gaps. The
read-only fileset parser rejects conflicting mappings and requires every
requested interval to be present and fully decoded. Output
`join-contract-original-bytes.asm` has SHA-256
`0544a7c0d951e6ed090ab620179cc1283d3c425445caafed2a4c15ecf236f8dd`;
the reader `read-join-contract-raw.py` has SHA-256
`957963e06c05835297046251b1070cfa6905e28b352d736c6460544ef5320343`.
In particular, the original `sendConnectComplete` instructions at
`0xffffff800155d3bc..0xffffff800155d400` confirm the ten ASSOC-result copies.
This supplements, rather than assumes completeness of, the generated C.

## Exact-image ingress result: bad BSSID already supplied by WCL

The same published `b5c6cfd8` image and boot were retained: boot UUID
`9C8DDC74-0D6C-49D7-9200-4DE18FBC3C76`, loaded kext UUID
`9D5A9332-924A-3D77-8615-7E1CF1C73CB1`. The host AX211 supplied the controlled
same-SSID wrong-password SAE AP `80:e4:ba:20:ef:fa` on channel 9; the real
OpenWrt alternatives remained available.

The first address-directed request at 13:40:26 UTC was superseded by another
WCL scan, not an actual failed join. The first 180-second observer ended at
13:42:43 with zero errors and no matching association ingress. System
Settings was still running after the earlier S3, and its ordinary AppleScript
quit did not return. Only that guest application and the blocked quit helper
were terminated at 13:42:39; no wireless daemon or WindowServer was restarted.
The already queued framework request then ran. This fixture intervention
does not close the stalled post-S3 GUI or prove the source of every scan.

The second observer ran 13:42:54–13:45:55 UTC, ended with zero errors and
empty stderr, and recorded successful WPA2 ingress/0xd3 controls. Its setup
gap means the preceding failed-roam ingress is not used as a captured proof.
A subsequent ordinary `networksetup` LabAP join at 13:43:58 provides the
complete relevant ingress observation:

- At 13:44:02, WCL supplied count 1, auth selector 0x1000, first BSSID
  `80:e4:ba:20:ef:fa`, paired MAC zero and chanspec 0x1009. Its separate
  context BSSID was `ee:c1:c8:97:d5:13`, not the first candidate.
- WNM retarget returned zero. Direct SAE begin received exactly
  `80:e4:ba:20:ef:fa` and created generation 19. WCL ingress returned success;
  real AUTH began at 13:44:05 and returned to SCAN at 13:44:10.
- The external AP recorded Algorithm-3 Commit/Confirm and Confirm mismatch,
  without station authorization. The native request still waited until
  13:44:12.440 and reported association timeout -3905.
- Its retry at 13:44:12 again supplied the same bad first BSSID, with no WNM
  override, and created generation 20. This accepted request never reached
  a captured AUTH edge before the native timeout at 13:44:22.839. Therefore
  an AUTH/ASSOC-only correction would leave the pre-AUTH scan branch open.
- The command reported API error -3912 at 13:44:23. A later automatic attempt
  created generation 21 on that same WCL-supplied bad BSSID at 13:44:29,
  entered AUTH, and returned to SCAN at 13:44:34. Native timeout followed at
  13:44:39.815.
- Automatic selection of the other saved WPA2 network arrived at 13:44:43.
  It associated at 13:44:47 and reached DHCP BOUND at 13:44:49.568, with
  publication at 13:44:50.388. A 20/20 source-bound 1400-byte check passed
  while the bad AP was still on air. No off/on or reboot was used.

This rules out a local BSSID override for these observed requests. The
correct next change is the reference-shaped failed-attempt ledger and native
candidate progression, including accepted pre-AUTH scan exhaustion, not a
forced replacement of WCL's selected target. Firmware completion, peer IEEE
status, local admission failure and explicit cancellation must remain distinct.

The fixture's normal bounded timeout completed cleanup at 13:46:13 UTC
(outer status 124). It restored the host's exact ordinary managed profile,
172.16.66.226 address and wired default route. A separate host-to-guest
1400-byte check passed 20/20 to the actual recovered 172.16.66.212 address.
No physical user host, other VM or base disk was changed. Both observers and
all traffic/fixture jobs reached terminal state. This is diagnostic evidence
on the old published image, not an after-fix qualification.

## Implementation boundary identified by the lower-path audit

The failed value must be owned from accepted WCL/public ingress, not first
created only after AUTH: generation 20 above has no AUTH snapshot. A selected
attempt can later add its exact association epoch and AUTH/ASSOC facts.
Timeout, peer rejection, exhausted candidate scan, local SAE failure and PAE
failure need an exact request-specific terminal; an explicit abort/off/sleep
must instead retire that request without manufacturing a candidate failure.

The current IWN management timeout advances the association epoch before
asking for SCAN. IWN's SCAN path performs another selected-node cleanup and
bypasses generic `sc_newstate`. IWM/IWX queue state work; their scan owners
also commit SCAN outside generic `sc_newstate`. The SAE workers in all three
families scrub engine/transport ownership separately from that transition.
Thus neither a callback immediately before cleanup nor an untagged callback
after generic `ieee80211_newstate` is a complete three-family solution.

The implementation must carry the exact failed request through its real lower
cleanup terminal, then publish only to its still-matching controller ledger.
Capturing a fresh epoch after a yielding cleanup could instead bless a newer
join; polling for S_SCAN alone has the same ownership ambiguity. No borrowed
node/credential pointer, independent retry timer or forced BSSID is needed.
Production source is still unchanged at this checkpoint; the next unit is the
complete ingress-to-failure-terminal implementation and its runtime gates.

## FIX_CANDIDATE: request-owned failure retirement

Route remains REUSE_REFERENCE_DECOMP for candidate results/publication, with
Intel-specific cleanup ownership. Implement one credential-free request ledger
from accepted ingress through discovery, AUTH, ASSOC and key completion. Bind
the selected epoch only once; a fresh accepted request receives a never-reused
generation. Record the actual failure stage/cause without converting every
local error into an IEEE peer rejection.

The failure value has explicit cleanup participants. Its producer, the lower
state transition and (when present) the SAE worker each retire only their own
matching request. Publish after all participants complete; a later public
request, explicit abort or power boundary invalidates the old value. IWM/IWX
must carry that generation in queued state work instead of sampling the
current generation after a sleep. Discovery exhaustion uses the actual final
physical-scan terminal, not another retry timer.

Adapt the complete controller and lower paths as one functional change,
including ordinary Open/WPA2 and SAE, while retaining source-preserving roam,
association comeback and successful completion. Test the production value
state machine, stale/reentrant cleanup, actual producer glue and exact payload
bytes before build/load; the controlled wrong-password multi-BSS run and
GUI/S3/AP regressions remain release gates. Intermediate implementation and
unit-test progress are not a functional closure or a releasable image.

## In-progress implementation and source verification, 14:32 UTC

This is an uncommitted implementation checkpoint, not a new runtime-qualified
fix. The loaded and public image remains `b5c6cfd8`; no install, reboot or
physical-user-host operation occurred during this implementation interval.

The common credential-free ledger now exists in `ieee80211_join_attempt.h`
and is embedded under the selected-BSS leaf lock. Its never-wrapping accepted
request generation is separate from the association epoch. Accepted public,
ordinary WCL and direct-SAE ingress arm it before cached selection/scan resume;
the actual selected-BSS publication binds its epoch. Existing controller
AUTH/ASSOC/RUN success edges record their facts. Explicit public/WCL leave,
abort, power-off, INIT and final selected-lock destruction cancel the request.
PMK maintenance cancels only its nonzero WCL generation, preserving the
independent public completion lease. Direct-SAE failed ingress retains its
local generation across a potentially yielding scan call, so its rollback
cannot clear a newly accepted upper owner.

The common bridge records a failure value and waits for separately named
producer/lower/SAE cleanup participants. Only the exact final participant can
claim its result. The callback is outside the leaf lock, and the controller
revalidates the same request generation and SSID/BSSID before publishing.
The controller now has the actual failure event handler and stage-specific
28-byte auth/association, 20-byte first-beacon and 164-byte connect-complete
builders. The final candidate record carries ASSOC status/reason; pre-AUTH
exhaustion does not fabricate AUTH, and key failure does not republish
AUTH/ASSOC or first-beacon events. IEEE peer status, received deauthentication
reason and local errno are distinct fields in the local ledger.

The supplicant result interpretation was cross-checked with the complete
`handleSupplicantEvent` decompile and Broadcom's
[PSK_SUP status/reason definitions](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fweh.h).
The generic failed-key terminal maps to overall 1014; a real handshake timeout
uses secondary 1005. Producer integration must still retain any more specific
available error rather than treating every SAE abort as a peer status or every
local error as a timeout. No source below emits the new failure event yet.

IWN has begun the full-width cleanup-token path: its historical newstate
callback forwards token zero to the unchanged ordinary implementation, while
the dedicated failure callback carries the exact generation. The tagged SCAN
branch rechecks after node cleanup and retires only LOWER; it does not start an
autonomous next scan ahead of WCL. This is not yet a complete IWN cleanup path:
existing active-scan/preflight early returns still require exact-terminal
replay, and real producers have not been connected. IWM/IWX queue transport,
SAE worker retirement and PAE failure cleanup remain to implement as part of
this same functional layer.

A pre-AUTH terminal must also carry the identity captured when the physical
scan was admitted. Sampling the current join generation only in end_scan would
misattribute an older or superseded scan to a newer request. IWN's final scan
notification already spans its 2.4/5-GHz continuation in one lease; all three
families advertise SCANALLBAND. Only the exact non-aborted association-directed
final census may report NO_NETWORKS. Controlled WCL foreground/handoff scans
and initial-discovery census-only terminals must retain their existing hold.

Verification completed on the current source:

- 174 production value-state cases in C11 and C++17 under ASan/UBSan.
- Extracted production leaf-lock bridge, including a new identical-network
  request between final cleanup and controller delivery, stale epoch rejection,
  duplicate participants and cancellation.
- Ten exact-byte failure fixtures and the actual controller publication
  function, exercising both WCL/public owners, message order and stale/duplicate
  rejection. This does not emulate physical radio or firmware cleanup.
- Full `test_payload_builders.sh` and adjacent IWN direct-SAE, public BSSID pin,
  software-PMF, post-PLTI, PLTI scan-resume and APSTA scheduler contracts pass.
  Newstate static checks follow the implementation and separately require the
  legacy wrapper's token-zero forwarding. A pipefail/early-grep exit 141 in the
  roam harness was removed by consuming the full matched pipeline; the 98
  production roaming cases and all their assertions remain unchanged.

Next is the actual lower producer/cleanup integration and source tests, then
the complete functional commit/push/build/load/on-air qualification. These
passing source checks do not close failed-join progression and do not authorize
publishing the intermediate image as a finished fix.

## Physical scan ownership and first producer integration, 15:10 UTC

The ordinary failed-join implementation remains one uncommitted functional
layer. This checkpoint adds production integration, not a new released image.
The preceding status-report turn made no functional progress; the continuation
revalidated the worktree and resumed implementation. User `Build/` remains
untouched. No guest install, reboot, radio operation or physical-host-22 access
was performed.

IWN now captures an accepted ordinary join generation when reserving its
physical generic foreground scan. It does not obtain that generation from
current state at completion. The lease retains it across the 2.4/5-GHz
continuation and copies it into the exact terminal value. Controller-owned
discovery/background scans and the initial hardware census have no such
receipt. A received aborted terminal uses controlled cleanup, not a fresh
candidate selection or fabricated NO_NETWORKS result.

`ieee80211_end_scan_owned` retains the existing controlled/initial-census,
late-RUN, SAE handoff and roaming holds. Its historical wrappers pass token
zero. After the generic scan callback, an old receipt cannot consume a newly
accepted request. At real all-band no-candidate exhaustion, the exact current
unbound join now records NO_NETWORKS and stops the old autonomous scan loop.
This is the first actual failure producer wired into the new common ledger.

The IWN terminal finishes its physical lease before acknowledging PRODUCER
or enqueueing failed-association cleanup. A dedicated full-width cleanup
token shares the existing replay task lifetime but does not overwrite a new
scan's replay intent or abort an unrelated scan. The token is monotonic,
waits for actual physical idleness, and is cleared by hardware invalidation
and detach. The worker rechecks the request before/after epoch cancellation
and rechecks physical admission before processing any subsequent scan intent.

Following lower SCAN cleanup, a separate engine-worker token waits for the
real private credential/engine retirement. It cannot acknowledge while an
engine owner, crypto object, deferred credential cancellation, active SAE TX
descriptor or queued TX terminal remains. A cancelled doorbelled frame is
still firmware-owned: even its otherwise silent TX_DONE now wakes the waiting
engine worker. The final engine acknowledgement is outside both leaf locks,
after worker-local buffers are scrubbed and before its TX-lifecycle admission
is released. Detach unpublishes the new lower callback and cancels the common
request before draining replay work.

Verification on these sources:

- The existing 174 production value cases pass as C11 and C++17 with ASan/UBSan.
- The extracted common leaf bridge additionally checks scan receipt capture,
  initial-census/RUN exclusion, supersession/cancellation, successful AUTH
  binding before an old scan terminal, and no invented AUTH/ASSOC facts.
- Twenty-five cases compile the real IWN reservation, band continuation,
  terminal claim/finish and cleanup dequeue functions. They check full-width
  identity, early/duplicate terminals, reset disposition and stale work.
- Real IWN engine-retirement helpers are compiled and tested with active
  engine, cancellation, DMA and queued-terminal holds; source wiring checks
  require the actual TX_DONE wake and worker-lifetime ordering. These fixtures
  model values and locks, not firmware, task scheduling or over-the-air service.
- Thirteen exact-byte payload cases and actual controller dispatch pass,
  including the received-deauth correction below. The complete payload suite
  and adjacent physical/standard scan, reassociation, beacon-loss, direct SAE
  transport, PMK continuation and ingress contracts pass. Static body selectors
  follow the new owned implementation while retaining wrapper and generic
  completion requirements; no behavioral assertion was removed.

Remaining implementation gates are still substantial: real AUTH/ASSOC/key
failure producers; producer-specific SAE/PAE causes; IWM/IWX physical receipt
and queued cleanup transport; received-deauth publication/owner ordering;
and full reset/after-yield cleanup fencing. In particular, IWN's general
tagged lower path still needs its ASSOC/KEYS firmware/PAE completion audit:
an asynchronous RXON submission cannot by itself prove firmware retirement.
The newly connected no-candidate path must not be treated as coverage of those
other stages. Full kext build and live failure/candidate-advance, GUI, S3 and
AP regressions remain required before a functional closing commit/release.

## Exact Core deauth/disassoc producer contract

The matching 25C56 BootKC was additionally analyzed read-only on the reference
host. The initial 60-function batch and the expanded 95-function Core,
NetAdapter, JoinAdapter and WCLDeauthDisassoc batch each created 40 actual
parallel decompiler interfaces; all submitted functions returned results.
The available-memory/resource checks showed no ongoing swap-in/out. The
functions were short (expanded-batch individual work up to about 3.8 seconds),
so the subsequent utilization poll was already outside the native work burst.
Decompiler C remains non-authoritative for inferred signatures and inlining.

The expanded package is retained at reference host
`/home/dima/Projects/ghidra_output/aiam_roam_policy_25C56_20260910.Pthh3J/join-core-events-v2/`.
Manifest SHA-256:
`4fd72ee9d171dad17de8ceba1e38e5a7c4a4230992dc563cf6772302778373e0`.
A separate fileset/Capstone reader decoded all 95 nm-bounded original byte
ranges, rather than relying on the saved Ghidra memory image. Output SHA-256:
`f8fd608a38b5c85096874923d5b7e1e16de56710f9cf327ced90fe9bccb80f48`;
reader SHA-256:
`270ab3d622623f095d7eb3b334dc8408935bc568c1b5dab339c67ab3aa20138a`.

`Core::handleDeauthEvent`, `handleDisassocEvent`, `handleDeauthData` and
`handleDisassocData` call `sendDeauthDissasocEvent` at
`0xffffff80015bbd04`, then process extended event data. The original
instructions at `0xffffff80015bbf2a` onward prove this separate sequence:

1. Post selector `0xcb`, length 20: mapped event status/reason at offsets 0/4,
   the received 16-bit IEEE reason at 8, BSSID at 10 and deauth flag at 16.
   Event reason uses the `0xe0823000` namespace for values through 45, not the
   AUTH/ASSOC peer-status namespace.
2. Set JoinAdapter's overall result at `+0x280` to the received 16-bit reason,
   or 12 when it is zero, then call `sendConnectComplete` at `0x155d346`.
   This producer does not create an AUTH, ASSOC or SET_SSID event. Existing
   candidate ASSOC facts and the secondary result are retained.

The new value ledger/payload builder has been corrected accordingly: a
PEER_REASON terminal leaves AUTH/ASSOC facts untouched and requests only D5,
with the real reason/default 12. This is preparation for the actual received
frame integration, not a claim that the current upper deauth handler is fixed.
That handler still clears both join owners before publishing its old carrier.
The complete correction must capture the actual deauth/disassoc subtype and
publish the exact CB then D5 sequence together after cleanup, rather than
letting asynchronous CB handling cancel the owner before a delayed D5. The
ordinary established-link D8 and explicit leave/abort paths must remain
separate. Review of the newly recovered WCL consumer/lifecycle neighborhood
continues before changing that handler.

## Optional-engine transport retirement and queued-scan audit, 15:30 UTC

The preceding user-status turn was read-only and made no implementation
progress. This continuation revalidated the dirty source tree, preserved the
user's `Build/`, and corrected a real safety hole in the unfinished IWN join
cleanup. It is still not a completed functional layer or a releasable image.

IWN attachment requires the TX lifecycle and descriptor locks, but permits
allocation of the separate direct-SAE engine lock to fail. Therefore the
absence of that optional engine does not prove absence of a legacy SAE TX
descriptor or queued terminal. The earlier immediate SAE acknowledgement in
this branch was unsound. A separate full-width token now lives under the
mandatory TX leaf and is consumed only by the existing TX worker, after its
local terminal value is scrubbed, with no active descriptor or queued event.
The worker still holds the common TX lifecycle admission during publication.
Stop, cancellation at reset/detach, and descriptor purge discard that token;
a stale token cannot acknowledge a replacement join.

The cancelled TX_DONE edge wakes the proper engine or no-engine transport
worker. Pre-doorbell cancellation and unsubmitted-frame retirement also wake
it because those frames will never receive TX_DONE. The no-engine TX worker
does not reschedule itself merely because DMA remains active: it waits for
the actual release edge. The engine and TX workers use the same single-thread
`systq`; the current taskq implementation confirms that a dequeued local TX
value cannot be concurrently consumed by the engine worker before its scrub.

The focused production-extraction suite passes under ASan/UBSan, including
the existing 174 C/C++ value cases and 25 physical-lease cases. Added tests
hold the no-engine participant behind each of stop, live DMA and queued
terminals; check exact and stale generations, monotonic enqueue, duplicate
delivery, absent TX storage, and callbacks outside both leaves. Actual-source
wiring checks require cancellation wakeups before lifecycle release, forbid
the no-engine self-requeue loop, and require reset token clearing. The full
payload suite also completed successfully (`join-no-engine-payload.log` in
the private evidence directory). These remain source tests, not a radio test.

The IWM/IWX physical-admission audit changes the next implementation boundary:

- Both higher scan wrappers publish SCANNING and their WCL started edge only
  after a potentially sleeping command sender returns. A final firmware
  notification can therefore precede that publication. A new receipt needs
  a reserved/submitted/upper-ready/terminal lifecycle, including a pending
  early terminal; simply storing the join generation after command return is
  insufficient.
- The current UMAC builders use UID zero, and receivers discard the UID and
  status when calling endscan. Intel's upstream
  [scan consumer](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/mvm/scan.c)
  indexes an allocated UID table, preserves STOPPING, and treats the final
  notification independently of command acknowledgement. The long host join
  generation must not be substituted for an arbitrary firmware UID.
- IWM's `err == 1` exception in foreground/background wrappers is not proof
  of an existing physical command or coalescing: the complete command sender
  returns local errno / `msleep` results. The earlier working-note assumption
  that this was a scan-coalescing contract is withdrawn.
- The actual IWM large-command DMA-mapping failure frees its mbuf but reaches
  `out` with `err == 0`. This is a false successful-submission edge and needs
  correction with the physical-admission integration. The IWX v12/v14 probe
  construction failure also returns without freeing its allocated request.
  These findings are not yet patched or counted as closed.
- Exact abort admission/terminal identity, AP handoff, initial-census holds,
  and reset erasure must participate in the same physical receipt. A terminal
  handler must not clear a newer command's flags or abort waiter after an
  upper callback has admitted that command.

No IWM/IWX receipt implementation, real AUTH/ASSOC/key producer integration,
or received-frame CB/D5 integration was completed in this continuation. The
general IWN firmware/PAE retirement and reset/after-yield audit remains open.
HEAD/origin and the loaded/public `b5c6cfd8` image are unchanged. The owned
QEMU PID 361779 remains live and host free space is 1.9 GiB. No install,
reboot, radio operation, host-22 access, or unrelated-VM operation occurred.

## Physical command admission implementation checkpoint — 16:05 UTC

The previous status-only turn made no implementation progress. This interval
adds real IWM/IWX command-transport integration within the same unfinished
failed-join layer; it is not a newly closed/released functional slice.

`ItlScanCommandLease` separates reservation, hardware submission, upper
readiness and final notification. It carries the immutable host join generation
separately from the firmware UID. A terminal claim also requires the exact host
serial, so a copied old dispatch cannot consume a newer ready terminal. Reset
epochs and command serials do not wrap; pre-submission rejection alone may
discard a reservation. An ambiguous post-doorbell transport error quarantines
admission until the existing reset worker performs hardware recovery.

Both actual command senders now validate the scan receipt at the hardware
publication boundary and hold the scan leaf through the doorbell. IWX takes
q0 then the scan leaf before transferring mapped storage into a q0 slot; it
releases the scan leaf before the existing q0 trace and unlock. IWM retains its
large-command mbuf locally until publication. Its DMA-map failure now returns
ENOMEM, and pre-doorbell error cleanup releases the local mbuf, response
storage and any separately acquired NIC wake ownership. A changed hardware
generation after mapping is rejected before acquiring that wake ownership.
Post-doorbell timeout retains DMA ownership for actual completion/reset.

Attach initializes the lease before callbacks are exposed. Stop/detach closes
its admission; successful init reopens it only against the reset-epoch snapshot
taken before the yielding hardware initialization and the exact hardware
generation. Actual reservation/rejection methods capture the unbound accepted
join outside the scan leaf and exclude boot census, background scans, selected
epochs and WCL-owned scans. The optional absent-engine IWN cleanup from the
preceding checkpoint remains unchanged.

The IWX v12/v14 request builders also now free their allocated request when
probe construction fails. These two error branches are source-corrected but
are not separately exercised by the sender fixtures below.

New `test_scan_command_lease.sh` compiles production code with ASan/UBSan:

- 33 value-state scenario groups in each of C++11/C++17, including all six
  ACK/readiness/terminal orderings for LMAC/UMAC and complete/aborted results,
  stale/duplicate identities, reset, abort and counter exhaustion;
- complete production IWM sender: 16 scenarios plus a dedicated DMA-failure
  check, including allocation/NIC failure, reset during mapping, stale serial,
  early terminal, timeout retention and ordinary non-scan commands;
- complete production IWX q0 sender: 20 scenarios, including q0 slot pressure,
  stop/detach admission, allocation/cursor/map failure, exact lock order,
  reset, early terminal and retained timed-out storage;
- the actual four reservation/reset/rejection bridge methods from both
  families: 20 scenario groups with lock/callback separation and stale tokens.

All passed. The unchanged HEAD IWM sender independently compiled against the
same fixture and failed the dedicated DMA error assertion (exit 134, no core
file); the corrected sender passes. The initial full payload run stopped on
five old exact-signature selectors. Updating only those selectors to include
the new serial parameter preserved their channel/SSID/dwell assertions; the
full payload suite then passed, including all earlier join/IWN tests and the
new sender tests. The management-transaction contract and diff whitespace
check also passed. These fixtures are not a full kext build or radio result.

Important remaining integration is explicit: foreground/background wrappers
still call their old two-argument builders, so the whole worktree is not yet
buildable. RX final-notification handlers and `endscan` still use the old
terminal path. `readyScanCommand`/`noteScanCommandTerminal` are prepared but
not yet connected or qualified; their deferred dispatch must carry the exact
serial into `endscan`, and physical/WCL/flags/abort ownership must be claimed
before any upper callback. Do not enable the new IWM/IWX NO_NETWORKS producer
until their request-tagged cleanup callbacks exist. Queued successor replay,
abort-command publication, AP admission during reservation, general AUTH/
ASSOC/PAE/SAE failure producers and received CB/D5 sequencing remain part of
the original whole-layer completion gate, not scope exclusions.

Read-only lab verification at 16:02:48 UTC retained boot `9C8DDC74`, loaded
UUID `9D5A9332`, and STA address 172.16.66.212. Public source remains
`b5c6cfd8`; no install, reboot, radio operation or host-22 access occurred.

The subsequent physical-scan contract initially selected the newly added IWM
detach call instead of the reset method definition. Anchoring that selector to
the complete definition (as already done for IWX) fixes the test extraction;
both families additionally require the new physical admission close. Physical
WCL scan lifecycle, standard scan lifecycle and reassociation/roam scan
contracts then all completed successfully. No test/build process remains live
at this checkpoint. Full-source compilation and on-air qualification are still
unperformed for this unfinished worktree, regardless of these fixture passes.
