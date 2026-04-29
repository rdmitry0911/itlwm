# AGENT HANDOFF - itlwm / macOS Tahoe

created_at: 2026-04-28 09:44:11 EEST +0300
workspace: `/Users/bob/Projects/itlwm`
branch_at_handoff: `master`
head_at_handoff: `d3a07c2abccac863e1909aa562051a6ee5687245`
active_request: `CR-173`
active_request_status: `PENDING_STRUCTURAL_REVIEW`

## 1. Read First

The next agent must start by reading:

- `docs/AGENT_EXECUTION_PROTOCOL_ITLWM.md`
- `docs/WORKFLOW_ITLWM.md`
- `docs/REVIEWER_PROTOCOL_ITLWM.md`
- `commit-approval/requests/CR-173-packet-scratch-field-map-batch.md`
- `docs/tahoe_signal_chain_audit.md`
- `docs/tahoe_discrepancy_inventory.md`
- `analysis/ANALYSIS_REPORT_2026-04-23.md`

Current task direction from the user:

- Continue restoration cycles for remaining macOS Tahoe Wi-Fi layers.
- Prioritize the layers most likely to unblock STA connection quickly.
- Work in batches, not one tiny divergence per reboot.
- If a decompile-confirmed 1:1 divergence is found near the active blocker, include it in the same batch.
- Do not wait for runtime failure if a confirmed 1:1 divergence is already proven.
- AP mode matters and must not be ignored, but active STA/EAPOL/key/datapath blocker remains the immediate runtime blocker.

## 2. Protocol Constraints

Hard rules:

- No code patch without a filled `FIX_CANDIDATE` under the protocol.
- No speculative fixes, guesses, fallback paths, forced success, retry loops, replay, delay, polling, masking, suppression, or "try and see" changes.
- Allowed justification classes are only:
  - `REFERENCE_ALIGNMENT_FIX`
  - `SYSTEM_CONTRACT_FIX`
  - `DIAGNOSTIC_INSTRUMENTATION`
- Stage 1 reviewer approval is required before install/runtime collection.
- Stage 2 reviewer approval is required before commit.
- Old requests are not "fixed"; if scope or diff changes, create a new request that supersedes the old one.
- Do not unload the loaded driver. User repeatedly stated unloading causes panic.
- If install is approved, it is enough to remove the old `/Library/Extensions/AirportItlwm.kext` and copy the new one there. The user said `kmutil` is not required for this workflow.
- Logs generally require `sudo`.

Important Stage rule for the current state:

- `CR-173` is Stage 1 only.
- There is no known reviewer decision for `CR-173`.
- Until `APPROVED_FOR_AFTER_FIX_RUNTIME` exists, do not install the CR-173 build and do not collect after-fix runtime for it.
- Until final `APPROVED` with `allow_commit_now: YES`, do not commit.

## 3. Worktree State At Handoff

`git status --short --branch` showed:

- `## master...origin/master [ahead 14]`
- Many tracked and staged-as-intent additions/modifications are present.
- `commit-approval/` and `kext-backups/` are untracked.
- `MacKernelSDK/` is ignored and must not be treated as part of the CR diff.

Important dirty files include:

- `AirportItlwm/AirportItlwmAPSTAInterface.hpp`
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
- `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
- `AirportItlwm/AirportItlwmV2.cpp`
- `AirportItlwm/AirportItlwmV2.hpp`
- `AirportItlwm/AirportSTAIOCTL.cpp`
- `AirportItlwm/TahoeAssociationContracts.hpp`
- `AirportItlwm/TahoeControllerContracts.hpp`
- `AirportItlwm/TahoeHiddenInterfaceContracts.hpp`
- `AirportItlwm/TahoeQosDynsarContracts.hpp`
- `include/Airport/IO80211NetworkPacket.h`
- `include/Airport/IO80211SapProtocol.h`
- `include/Airport/IOSkywalkNetworkPacket.h`
- `include/Airport/apple_private_spi.h`
- `scripts/build_tahoe.sh`
- many `docs/reference/*.md`
- many `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/*.yaml`

Recent commits at handoff:

- `d3a07c2 AirportItlwm: attach Skywalk queues to workloop`
- `8e05ddf AirportItlwm: instrument Tahoe assoc data-path gate`
- `b3e7732 AirportItlwm: instrument Tahoe current-link probe ingress`
- `fe953b4 Accept registry diagnostic control writes`
- `6832356 Keep packet diagnostics strictly default-off`
- `fec7479 Add registry mailbox diagnostics`
- `24692fe Revert "Add passive console diagnostics for CR-052"`
- `818cb34 Revert "Defer diagnostic userclient off boot path"`

## 4. Active Commit Requests

Requests present:

- `commit-approval/requests/CR-170-io80211-network-packet-pool-class-batch.md`
- `commit-approval/requests/CR-171-ioskywalk-network-packet-size-batch.md`
- `commit-approval/requests/CR-172-io80211-network-packet-surface-batch.md`
- `commit-approval/requests/CR-173-packet-scratch-field-map-batch.md`

Supersession chain:

- `CR-173` supersedes `CR-172`
- `CR-172` supersedes `CR-171`
- `CR-171` supersedes `CR-170`

Latest decision check returned no files for `CR-170` through `CR-173`:

```sh
find commit-approval/decisions -maxdepth 2 -type f -name '*CR-17[0-9]*' -print
```

Therefore the active request is still:

- request: `CR-173`
- stage: `STAGE_1_STRUCTURAL`
- status: `PENDING_STRUCTURAL_REVIEW`
- action allowed now: wait for reviewer decision, or if changing scope/diff, create a new superseding request.
- action not allowed now: install, runtime test, or commit as if approved.

## 5. Current Runtime Symptom

Known runtime state from the latest user reports and collected evidence:

- Driver loads.
- Wi-Fi networks are visible in UI.
- Joining a test network starts but does not complete.
- Sometimes the UI briefly shows a connected icon.
- There is no internet access.
- Then the network disconnects.
- Test networks used include `CONTROL_STA_NETWORK`, `HelloSky`, and earlier `btn-vno`.
- User clarified Wi-Fi is `en0`; USB Ethernet is `en1`.

Important telemetry from earlier runtime evidence:

- RX EAPOL reaches IO80211 input.
- EAPOL TX/key/RSN progression is absent.
- `eapol_rx=8`
- `eapol_tx=0`
- `IO80211RSNDone=No`
- No `setCIPHER_KEY` before AP deauth reason 15.

Interpretation:

- Scan/UI visibility is currently not the primary blocker.
- The active blocker is after RX EAPOL input and before supplicant/key/RSN completion.
- Packet scratch, IO80211 input handoff, peer-manager, key path, and datapath completion remain the highest-value audit area.

## 6. Build And Artifact State

Latest CR-173 verification performed before handoff:

- `git diff --check`: PASS
- targeted YAML parse for `113` and `114`: PASS
- `./scripts/build_tahoe.sh`: PASS
- BootKC symbol verification: PASS, all 884 undefined symbols resolve
- `./scripts/build_regdiag.sh`: PASS

CR-173 artifact:

- path: `commit-approval/artifacts/CR-173-packet-scratch-field-map-batch.diff`
- line count: `24688`
- changed files: `88`
- sha256: `469da6a29b79ca8552c0759ad1719920bb7da9834ea8a9c68fdd5e73976a89c1`

Build evidence:

- Tahoe evidence: `commit-approval/build_evidence/CR-173-build-tahoe-packet-scratch-field-map-20260427.txt`
- Tahoe evidence sha256: `5943c58f01bbf66ff0148b66406c027632afcc8a304a11cd93a7f4d883f8d915`
- Regdiag evidence: `commit-approval/build_evidence/CR-173-build-regdiag-packet-scratch-field-map-20260427.txt`
- Regdiag evidence sha256: `c998ffcfd4e91090b46d7889c4a3d746cf588ce2df6fb6cd63c366def7283381`

Build outputs:

- kext: `Build/Debug/Tahoe/AirportItlwm.kext`
- kext executable sha256: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext UUID: `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag: `Build/Debug/Tahoe/airport_itlwm_regdiag`
- regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

## 7. What Has Been Implemented In The Current Batch

CR-170:

- Added `include/Airport/IO80211NetworkPacket.h`.
- Added `AirportItlwmIO80211PacketPool` in `AirportItlwm/AirportItlwmV2.cpp`.
- Pool `newPacket(...)` allocates system `IO80211NetworkPacket` through `OSMetaClass::allocClassWithName("IO80211NetworkPacket")`.
- Pool calls inherited `initWithPool`.
- TX/RX pools now use this packet pool.
- It does not synthesize Apple packet scratch.

CR-171:

- Removed stale `mReserved` from tracked `include/Airport/IOSkywalkNetworkPacket.h`.
- Added `static_assert(sizeof(IOSkywalkNetworkPacket) == 0x78)`.
- Did not include or track ignored `MacKernelSDK` mirror.

CR-172:

- Expanded local `include/Airport/IO80211NetworkPacket.h` with decompile/export-proven surface:
  - `OSDeclareDefaultStructors(IO80211NetworkPacket)`
  - `enum IO80211NetworkTXStatus : UInt32`
  - `getPacketType`
  - `getVirtualAddress`
  - PTM/timestamp/status methods
  - `getBufferSize`
  - two `prepareWithQueue` overloads
  - `static_assert(sizeof(IO80211NetworkPacket) == 0x78)`
- No runtime object behavior was changed.

CR-173:

- Expanded `include/Airport/apple_private_spi.h::packet_info_tag` to name recovered Apple scratch fields while preserving total size `0x98`.
- Named offsets:
  - `+0x14` `rx_completion_marker`
  - `+0x18` `tid`
  - `+0x29` `service_class`
  - `+0x48` `bus_address`
  - `+0x50` `virtual_address`
  - `+0x74` `packet_signature`
  - `+0x80` `tx_status`
  - `+0x8a` `flow_queue_idx`
  - `+0x90` `ac_dup_flags`
- Added static offset assertions.
- No packet ownership behavior was changed.

## 8. Rejected Path: Local C++ Packet Subclass

A direct local subclass was attempted before CR-173 submission:

- `AirportItlwmIO80211NetworkPacket : IO80211NetworkPacket`
- local scratch pointer
- packet-owned scratch-like behavior

Result:

- `xcodebuild` succeeded.
- `./scripts/build_tahoe.sh` failed BootKC symbol verification.

Unresolved non-exported parent virtuals:

- `IOSkywalkPacket::getDataOff()`
- `IOSkywalkPacket::getDataLength()`
- `IOSkywalkPacket::getDataOffset()`
- `IOSkywalkPacket::getPacketBuffers(IOSkywalkPacketBuffer**, unsigned int)`
- `IOSkywalkPacket::setDataOffAndLen(long long, unsigned long long)`
- `IOSkywalkPacket::getMemoryDescriptor()`
- `IOSkywalkPacket::getPacketBufferCount()`
- `IOSkywalkPacket::getDataVirtualAddress()`
- `IOSkywalkPacket::getDataIOVirtualAddress()`

The subclass patch was reverted before `CR-173`.

Implication:

- Do not reattempt naive C++ inheritance from `IO80211NetworkPacket` unless the non-exported parent virtual problem is solved with decompile-proven construction semantics.
- Do not raw-write `packet+0x78`; `IO80211NetworkPacket` local/system class size is `0x78`.

This rejection is documented in:

- `docs/reference/AppleBCMWLAN_packet_scratch_field_map_2026_04_27.md`
- `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/114_packet_scratch_field_map_2026_04_27.yaml`
- `commit-approval/requests/CR-173-packet-scratch-field-map-batch.md`

## 9. Highest-Priority Next Technical Work

Start with the active blocker layer:

1. Audit `IO80211InfraInterface::inputPacket`.
2. Audit `IO80211PeerManager::inputPacket`.
3. Audit `IO80211PeerManager::skywalkInputPacket`.
4. Determine whether IO80211 consumers require packet-owned scratch, or whether explicit `packet_info_tag *` handoff satisfies every relevant system-facing touchpoint.
5. If packet-owned scratch is required, find an Apple-compatible construction strategy that does not produce unresolved non-exported `IOSkywalkPacket::*` virtual references.
6. Continue auditing EAPOL TX/key/RSN handoff after IO80211 RX input.
7. Include nearby confirmed 1:1 divergences in the same batch if they are in the same causal layer.

Do not implement:

- forced EAPOL TX
- forced key install
- forced `IO80211RSNDone`
- deauth masking
- replay/re-emit without producer-side reference proof
- timing delay
- retry loop
- polling loop
- broad fallback path

## 10. APSTA / SoftAP Status

AP mode remains important to the user.

Current state:

- Many APSTA docs/layout witnesses were added through YAML `93` through `96` and later APSTA reference docs.
- Runtime APSTA owner creation is still not fully enabled.
- Previous notes that APSTA/SoftAP was "separate owner-layer" should not be used to postpone it indefinitely.
- If Apple requires a class, owner-object, queue, flags, or structure, the target direction is to restore it in full 1:1 correspondence, not to avoid it because it is larger than a "safe" fix.

Priority guidance:

- Finish the active STA/EAPOL/key/datapath blocker first because it is the shortest path to functional connectivity.
- Then resume APSTA owner-layer reconstruction with the same decompile-backed 1:1 discipline.
- If APSTA divergence is discovered adjacent to an active touched layer and is fully proven, it may be included in the batch.

## 11. Remote Decompile Host

User-provided host:

```sh
ssh dima@192.168.40.116
```

Useful paths on remote host:

- `/srv/project/ghidr*/`
- `/srv/project/ghidra_output/`

Known decompile sources referenced in current CRs:

- `/srv/project/ghidra_output/AppleBCMWLANBusInterfacePCIeMac_decompiled.c`
- IO80211Family decompile output, exact path to verify on host before use.

Use decompile as primary truth. YAML and local reference docs are secondary unless they cite exact decompile evidence.

## 12. Useful Local Commands

Check current request decisions:

```sh
find commit-approval/decisions -maxdepth 2 -type f -name '*CR-17[0-9]*' -print | sort
```

Check worktree:

```sh
git status --short --branch
```

Build Tahoe kext:

```sh
./scripts/build_tahoe.sh
```

Build diagnostic utility:

```sh
./scripts/build_regdiag.sh
```

Run diff hygiene:

```sh
git diff --check
```

Find current EAPOL/runtime artifacts:

```sh
find commit-approval/runtime_evidence -type f | sort | tail -120
```

Use runtime logs only through `sudo` when needed.

## 13. If Reviewer Approves CR-173 Stage 1

If a decision file appears with `status: APPROVED_FOR_AFTER_FIX_RUNTIME` for exact `CR-173` HEAD/diff/text:

1. Do not change HEAD or request text before runtime.
2. Re-run build if needed:

```sh
./scripts/build_tahoe.sh
./scripts/build_regdiag.sh
```

3. Do not unload the currently loaded kext.
4. Install by removing old `/Library/Extensions/AirportItlwm.kext` and copying `Build/Debug/Tahoe/AirportItlwm.kext` there, if approved by the workflow.
5. Ask user to reboot and reproduce with Wi-Fi `en0`.
6. Collect after-fix runtime evidence through `sudo`.
7. Update or create Stage 2 request evidence without changing claim scope or patch scope.
8. Wait for final reviewer `APPROVED` and `allow_commit_now: YES`.
9. Only then commit.

## 14. If CR-173 Is Rejected Or Scope Must Change

Do not edit CR-173 as a "fix".

Instead:

1. Continue decompile/static audit in the active layer.
2. Fill a new `FIX_CANDIDATE`.
3. Update `analysis/ANALYSIS_REPORT_2026-04-23.md` or create a fresh dated analysis file if appropriate.
4. Update missing YAML/reference docs for new findings.
5. Create a new request, for example `CR-174-...`, that supersedes `CR-173`.
6. Create a new artifact diff and build evidence.
7. Keep the claim scope narrow but batch all confirmed adjacent 1:1 divergences.

## 15. Things Not To Lose

- The known-good baseline history started from restore/CR-052 work where networks were visible.
- The user has repeatedly seen regressions when diagnostic layers touched boot/init behavior.
- Current diagnostics must remain behavior-neutral and default-off.
- Network visibility is currently preserved; do not risk scan/UI regressions with unrelated changes.
- The target is not cosmetic similarity. The target is semantic compatibility with macOS Tahoe system contracts and Apple reference behavior.
- If Apple uses an owner object, queue, flag, or hidden class, the correct direction is to restore it, but only with proof and with build/runtime safety gates.
