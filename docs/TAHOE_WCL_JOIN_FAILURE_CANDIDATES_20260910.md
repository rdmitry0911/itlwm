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
