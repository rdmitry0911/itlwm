# WCL failed-join candidate progression — 2026-09-10

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
