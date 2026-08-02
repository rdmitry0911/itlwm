# Tahoe IWX driver-resident SAE owner — 2026-08-01

## Reference boundary

Tahoe 25C56's `AppleBCMWLANCore::checkForWPA3SAESupport()` queries lower
feature `0x41`. The recovered
`AppleBCMWLANJoinAdapter::programPMK(wsec_pmk&)` then sends BCM firmware ioctl
`0x10c` with a 264-byte `wsec_pmk` value. The local reference material is in:

- `ghidra_output/AppleBCMWLANCoreMac_decompiled.c`, functions
  `checkForWPA3SAESupport` and `isSaeH2eEnabled`;
- `ghidra_output/CR-479-pmk-delivery-channel-20260515/`
  `AppleBCMWLANJoinAdapter_programPMK_decompiled.c` and `MANIFEST.txt`.

The ioctl is a BCM firmware ABI and must not be fabricated on Intel. Its IWX
equivalent is a lower, driver-resident owner which places a verified SAE PMK
in net80211's host-side `ic_psk`, binds it to the selected BSS and permits the
ordinary Association Request doorbell only while the same public completion
identity remains current.

## Implemented owner

The selected API-68 IWX runtime now owns the entire lower transaction:

1. A bounded WCL `CIPHER_PWD` record is staged in a scrubbed private slot.
2. The selected-BSS `S_AUTH` hook consumes it into the existing in-kext
   HNP/H2E SAE engine.
3. Commit and Confirm use IWX's real management descriptor and native TX_DONE
   path. Driver tickets occupy an independent high-bit cancellation domain.
4. Peer Confirm validation claims the SAE PMK and scalar-derived PMKID under
   the selected-BSS and engine leaves; it does not route through an Agent,
   external-PMK installer or fake BCM ioctl.
5. The queued Association Request is checked before header trim and again at
   the real IWX `qid << 16 | cur` WRPTR write. A stale PMK/BSS owner cannot
   cross that doorbell.
6. Stop, detach and sleep/reset close callbacks, cancel the owner, scrub
   transient credentials and drain the engine task before private locks are
   released.

Feature `0x41` is now published through a stable HAL capability only when the
attached backend has the complete driver-resident path. The diagnostic direct
SAE stimulus remains IWN-only.

## Deliberate scope

Admission remains limited to the five already audited new-format API-68 MFP
configurations (AX211 GF normal/long, AX210 TY, AX411 GF4 normal/long). Other
IWX firmware/config objects stay closed. Initial pure SAE/transition
association and targeted multi-AP credential replay share the same owner. For
an explicit WCL roam or confirmed 802.11v target, IWX copies an active
credential only after exact ESS/BSSID/port-valid checks, crosses the
association epoch, binds a new request generation to the selected cached node
and starts the same driver-resident SAE engine. A missing or stale credential
preserves the working source link and fails the roam request.

This follows the recovered `AppleBCMWLANCore::setWCL_REASSOC` boundary in
`ghidra_output/aiam_applebcm_reassoc_25C56_20260801/reassoc.txt`: the reference
updates the roam policy and submits a bounded firmware roam scan, selecting
V3, V1 or legacy command format by firmware-interface version. Intel has no
BCM reassoc command ABI, so the common real background scan supplies the
target and the IWX host owner performs the equivalent lower retarget; it does
not fabricate a same-BSS reassociation frame or immediate success.

The laboratory VM has physical IWN, not an IWX device. Its whole-kext runtime
can prove IWN regression safety and sleep/wake recovery, but it cannot be
reported as physical AX210/AX211/AX411 on-air evidence. Physical IWX validation
therefore remains required before calling this hardware-proven.
