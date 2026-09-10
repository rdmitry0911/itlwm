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
and 40 actual parallel decompiler workers requested. Its results must be
checked before relying on a complete teardown/duplicate-event contract.

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
