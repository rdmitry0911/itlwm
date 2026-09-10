# Failed roaming target and WCL association teardown — 2026-09-10

## Reproduced boundary

The loaded `c19ab0db` image correctly withdrew BSD carrier and IPv4 on the
controlled on-air SAE Confirm mismatch, but airportd still considered the
old network associated. Its immediate auto-join attempts were rejected by
that predicate. Full timing, independent AP rejection, eventual automatic
other-profile recovery and packet checks are recorded in
[the carrier qualification](TAHOE_ROAM_CARRIER_CONTINUITY_20260910.md).

No new runtime instrumentation was needed to identify this boundary: the
existing lower scan/SAE logs, external hostapd and current-boot native network
events expose both the actual failure and the inconsistent consumer state.

## Reference contract and remaining reconstruction

Route: REUSE_REFERENCE_DECOMP. Exact 25C56
`AppleBCMWLANNetAdapter::handleLink` at `0xffffff800152590a` publishes an
independent 16-byte WCL `0xd8` link indication. It carries the event BSSID,
link-state byte, interface-type byte and normalized reason. A failed roam
is not represented merely by the reassociation-result bulletin.

`WCLRoamManager::setReassocFail` at `0xffffff8002104928` clears its request
bookkeeping and consumes the bulletin; it does not run WCLNetManager's
connection teardown. `WCLNetManager::linkDownInd` at `0xffffff80020edee8`
has an explicit reason-5 branch for authentication/association failure when
roaming to a new target. That path delegates to `leaveNetworkCommand` at
`0xffffff80020ed43e`. Its terminal `linkDownComplete` publishes the
20-byte `WCL_LINK_STATE_UPDATE` IOC and retires the framework association
state. The independent roam-manager link-down event also retires its timer,
locks and temporary policy state.

The local watchdog sends the existing reassociation-failure event and enters
SCAN. The generic carrier path publishes parent/BSD link-down, while the
separate WCL link-down producer is currently reached by explicit deauth,
beacon-loss, disassociate and power-off paths. It is not reached by the failed
roaming-target path. Clearing the driver's BssManager is not a replacement
for WCLNetManager's own teardown.

One saved decompilation incorrectly treated the exact kernel `_memcmp`
symbol at `0xffffff8000104450` as no-return, truncating ordinary branches.
The symbol was independently verified against this guest's own BootKC.
A new read-only batch covers all 308 exact symbol-bounded functions of
WCLNetManager, WCLRoamManager and their FSM bases, with that metadata corrected
and 40 actual parallel decompiler interfaces. All 308 jobs completed; two
country-code extension functions retain bad-instruction warnings and are not
claimed complete. The active link-loss, leave, completion and FSM functions
were checked separately. Manifest SHA-256:
`9acf702e497267127a4e160de3e12e3ea20e3c55a99f302c99038e2edf344a0c`.

The first configuration-table reader failed because the config at
`0xffffff80023e2c20` is BSS storage initialized at module load, not populated
file data. The exact initializer `0xffffff80020f2b66..0xffffff80020f2be8`
assigns the static context at `0xffffff80023db620` and dimensions 7 states,
11 events and 22 actions. Decoding the matching level-zero BootKC chained
pointers recovered all 77 cells and 56 subscriptions. The `0xd8` subscription
reaches `linkStatusInd`, whose link-state byte selects LINK_UP or LINK_DOWN_IND.
From WAITING_FOR_CONNECT_COMPLETE, WAITING_FOR_IP, LINK_UP and SLEEP, the latter
invokes `linkDownInd`. LEAVE_NETWORK then enters DEAUTH/SLEEP_DEAUTH and calls
`leaveNetwork`; LINK_DOWN_COMPLETE runs the terminal cleanup. A repeated
LINK_DOWN_IND in DEAUTH, SLEEP_DEAUTH or LINK_DOWN is ignored. Local producer
one-shot ownership is still required; consumer tolerance is not permission to
publish events for a newer association. Table SHA-256:
`a566a2f25cace5dd33e4516d86e10cdd34f44dfcaead15235b8ba8451b24760b`.

## FIX_CANDIDATE

The required correction is a one-shot, epoch-owned failed-replacement
notification to WCL, in addition to ordinary BSD carrier retirement. It must
be sent only after the old association has actually been replaced/lost;
no-target or superseded scans that retain the source must not be converted
into disconnections. A late backend callback must not detach a newer join.
Existing deauth/beacon-loss publications must not gain a duplicate terminal.
The selected failed target identity and the event's lifetime across the
controller gate must be explicit; a generic reason-9 internal disassociate
or a shortened timeout is not a substitute for the reference event.

Source correction, actual-production-function regression with an unchanged
negative control, full adjacent three-family tests, build, exact-image load
and repeated successful/failed on-air roaming remain required. There is no
new production change or qualified release at this checkpoint.

## Implemented candidate and source checks

The common association cancellation now consumes the exact same-ESS carrier
reservation while holding the selected-BSS leaf lock, before invalidating that
selected value. It copies only BSSID and the post-cancellation publication
epoch. After the lock and existing credential-revocation callbacks have been
released, the controller revalidates epoch and BSSID inside its command gate
and queues the independent 16-byte WCL link-down indication with normalized
reason 5. No new timer, synthetic reassociation result, forced framework state
or credential-preservation exception was introduced.

Direct lower/preflight failures and explicit common carrier retirement consume
the same one-shot reservation. A stale backend epoch cannot retire a newer
lease. These direct paths finish their ordinary BSD publication before the
new event's potentially yielding delivery, and cannot apply another DOWN after
that callback. Cancellation fences retain their pre-existing lower state
transition owners. Source-retaining scans have no replacement reservation and
produce no new link-loss indication. An authorized successful RUN retires the
reservation without a failure event; early IWX port authorization still waits
for its committed RUN.

Public disassociate, WCL leave/join-abort, radio off, RUN deauthentication and
firmware beacon loss retire the replacement-only notification lease before
their existing terminal path. A fresh public/WCL association also disarms it.
Pre-RUN deauthentication has no eligible RUN-only link indication, so an actual
lost replacement there retains the new common failure terminal.

The expanded ASan/UBSan regression compiles the production cancellation body,
snapshot/claim helpers, BSS replacement, carrier bridge and gated WCL producer.
Its 98 cases include duplicate failures, cancellation before invalidation,
reentrant epoch/BSSID replacement, same-BSSID newer joins, explicit cancellation,
successful completion, missing lock/invalid identity rejection and the exact
zero-initialized WCL payload. Dependency fixtures model state and callback
boundaries, not hardware or crypto. The unchanged `a9918b4f` cancellation body
compiles with the same fixture and fails the reservation-retirement assertion
(exit 134); the corrected code passes. The full payload suite and adjacent
SAE, association-comeback, initial-BSSID, WCL reassociation, three-family beacon
loss, link-context and public-disassociate contracts pass.

Build, exact-image load and repeated successful/failed on-air roaming are still
required before this candidate can replace the published `964a90b3` image.
