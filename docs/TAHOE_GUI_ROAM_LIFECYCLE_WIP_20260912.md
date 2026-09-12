# GUI roam lifecycle implementation checkpoint — 2026-09-12

Status: **WIP, not a qualified driver fix or release**. The user requested a
commit/push of the current work followed by handoff to another agent. Do not
install this checkpoint on physical 10.90.10.22 or publish it as a new alpha.
The last qualified/published production source remains `5e98d6406245d53b49a0592b5675f6ba739fb61d`.

## Implemented candidate

The common net80211 owner now snapshots real source uptime/RSSI/channel at
admission and target BSSID/RSSI/channel at actual selection. The selected
node is not retained in an upper event. `ieee80211_wcl_reassoc_prepare()`
performs the target-stage transition under the selected-BSS lock and invokes
the upper callback only after unlocking. It rechecks ownership afterward.

The detached event carries cumulative observed stages. The controller gate
claims those stages by request serial and association epoch and emits:

- accepted scan: `0x89`, 12 bytes;
- selected target preparation: `0x8b`, 12 bytes;
- existing reassociation reply: `0x49` success or `0xcf` command error;
- overall roam completion: `0x50`, 168 bytes.

The terminal can publish an unreported real prefix if it overtakes a delayed
progress callback. It does not invent the optional `0x8a` scan-end event.
Serial/epoch checks suppress the remainder after a replacement, including a
replacement during `0x49` publication. A same-serial nested completion also
prevents a resumed progress callback from publishing preparation after done.

This is not yet a complete reference payload implementation. Only observed
uptime/RSSI/channel/BSSID/OUI fields are populated in the overall carrier.
Capability flags, channel specification, authentication/AKM/PHY and optional
firmware substate fields still require reconciliation. The host request has
no Broadcom firmware reason: the candidate uses the reference's generic
unavailable value, not a fabricated low-RSSI or initial-association reason.

## Executed checks of this checkpoint

| Check | Result | Scope |
|---|---|---|
| `WCL_REASSOC_REQUIRE_LIFECYCLE=1 bash scripts/test_wcl_reassoc_failure_retirement.sh` | exit 0 | 25 original ownership cases, six new actual-code ordering cases, all four lifecycle requirements |
| `bash scripts/test_net80211_join_bss_tx_teardown.sh` | exit 0 | Existing actual replacement/TX/cache teardown fixture |
| `bash scripts/test_net80211_roam_carrier.sh` | exit 1 | Test fixture lacks the new observation member, stage constant and helper declarations; no execution |
| `bash scripts/test_tahoe_wcl_reassoc_roam_scan_contract.sh` | exit 1 | Static test still requires target-stage assignment directly in scan completion; assignment moved into the new common preparation helper |
| `bash scripts/test_reassoc_deferred_bss.sh` | exit 1 | Known pre-copy/source-drain liveness scenario 1 remains red |
| `BSS_SWITCH_EXPECT_DEFECTS=1 bash scripts/test_reassoc_deferred_bss.sh` | exit 0, both negative cases 134 | Reproduces known liveness failures 1 and 24; **not a green driver gate** |

The new six ordering cases execute synchronous scan failure, synchronous
target success, a terminal overtaking queued progress, nested failure during
start, replacement during start, and replacement during reassociation success.
The original terminal selector/payload assertions remain separately checked;
new progress/done messages are counted independently. The four lifecycle
requirements no longer seed reference-side start/preparation: actual common
production functions drive those events.

These are Linux ASan/UBSan source fixtures, with explicit firmware and epoch
callback boundaries. They are not on-air tests, a full source aggregate, a
Tahoe build, or evidence of loaded image correctness. Final checks were run
with direct exit-status capture, not an unchecked tee pipeline. The shell's
negative-case “core dumped” text does not mean a core artifact was retained:
the scripts set `ulimit -c 0`.

## Remaining implementation work

1. Reconcile the carrier metadata with actual Intel observations and the
   reference consumers. Do not manufacture unavailable firmware statistics.
2. **Public `setWCL_SCAN_REQ` still cancels an accepted roam.** No arbitration
   fix is implemented in this checkpoint. Preserve a legitimate queued fresh
   scan without borrowing a predecessor's results or silently suppressing GUI
   requests. The existing IWN after-abort handoff queue is not a general queue
   behind a live roam. IWM/IWX need the same public semantics.
3. Audit interrupt/taskq/controller-gate ordering for the new progress path;
   selected-BSS unlocking alone does not prove absence of cross-owner deadlock.
   Audit cumulative prefix reentrancy, hard cancellation, rejected admission,
   observation retirement, and now-stale contract comments/old claim helper.
4. Adapt the two affected test fixtures without weakening their requirements;
   preserve the separately known deferred-BSS red cases. Run full relevant
   source regressions and an exact Tahoe build/import audit.
5. Install only into the owned disposable laboratory guest, verify loaded
   identity, then test native GUI scan/roam overlap and repeated security,
   saved-profile and recovery paths with first-loss endpoint evidence.
   Only a runtime-qualified functional fix can replace the published kext.

## Reference and evidence locations

Original confirmed defect and producer/FSM evidence:
`docs/TAHOE_ROAM_SCAN_SUPERSESSION_20260912.md`.

Additional exact read-only reference package on `10.7.6.112`:
`/home/dima/Projects/ghidra_output/aiam-roam-supersession-5995e24caa-20260912.x4SovM`.
It terminated at 11:20:49 UTC; 91 functions, 40 requested workers and 40 actual
decompiler interfaces, patched tool `5995e24caa`. Manifest SHA256:
`ce2cac9db82eed8be5e9138ba8de9e7cca14a4c206a2aa0cb8f1654538760d6d`.

All eight newly selected metadata/policy functions were read completely:
`WCLAdaptiveRoam::handleRoamEventConfiguration`,
`IO80211BssManager::getAssociatedAuthType`,
`IO80211BSSBeacon::getCurrentBSSAKMs`,
`AppleBCMWLANCore::getBssPhyModde`,
`WCLRoamManager::getCurrentPhyModde`, and beacon getBand/getChanSWSpec/getRSSI.
The adaptive consumer only inspects success status before resetting its
configuration. The auth/AKM mappings use Apple's upper-auth bits; PHY values
include 0x10/0x80/0x100, not a newly invented enum. No metadata code was added
after these reads because the user requested this handoff checkpoint.

Also read completely: WCLScanManager sendRequest/handleScanRequest and
WCLNetManager handleRoamDoneEvent. A lower scan error is returned, and the
scan timer starts only on success; no universal retry-on-busy follows from
those functions. The existing pending queue handles scan replacement, not
automatically every request rejected by an unrelated roam.

Working evidence: `/dev/shm/aiam-gui-roam-lifecycle-20260912.2AEEv7`.
Durable handoff archive:
`/home/dima/Projects/itlwm/aiam-gui-roam-lifecycle-handoff-20260912.HyoPu3`.
The subsequent agent-handoff document records its completed manifest and
authoritative source/production/runtime identities. Earlier completed radio
archives remain immutable; no new RF evidence was produced for this patch.
